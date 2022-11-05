/*
 * Copyright 2022, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <camkes.h>
#include <vmlinux.h>
#include <sync/sem.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <sel4vm/guest_ram.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/boot.h>
#include <sel4vm/guest_irq_controller.h>

#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include "sel4-qemu.h"

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtio/virtio_plat.h>

#define VIRTIO_QEMU_PLAT_INTERRUPT_LINE VIRTIO_CON_PLAT_INTERRUPT_LINE

int qemu_start_read(vm_vcpu_t *vcpu, void *cookie, uintptr_t offset, size_t size);
int qemu_start_write(vm_vcpu_t *vcpu, void *cookie, uintptr_t offset, size_t size, uint32_t value);

extern void *ctrl;
extern void *memdev;

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

volatile int ok_to_run = 0;

static sync_sem_t handoff;
static sync_sem_t qemu_started;

extern vka_t _vka;

//extern unsigned long linux_ram_base;

extern const int vmid;

static void intervm_callback(void *opaque);

static vmm_pci_config_t make_qemu_pci_config(void *cookie);

static vm_t *vm;

void qemu_initialize_semaphores(vm_t *_vm)
{
    vm = _vm;

    if (vmid == 0) {
        ZF_LOGI("Skipping QEMU module initialization on driver VM.");
        return;
    }

    if (sync_sem_new(&_vka, &handoff, 0)) {
        ZF_LOGF("Unable to allocate handoff semaphore");
    }
    if (sync_sem_new(&_vka, &qemu_started, 0)) {
        ZF_LOGF("Unable to allocate handoff semaphore");
    }

    ZF_LOGI("handoff EP: %"SEL4_PRIx_word"\n", handoff.ep.cptr);
    ZF_LOGI("qemu_started EP: %"SEL4_PRIx_word"\n", qemu_started.ep.cptr);

    if (intervm_sink_reg_callback(intervm_callback, ctrl)) {
        ZF_LOGF("Problem registering intervm sink callback");
    }

    ZF_LOGI("Initialized communication with QEMU");
}

typedef struct virtio_qemu {
    unsigned int iobase;
//    virtio_emul_t *emul;
//    struct console_passthrough emul_driver_funcs;
    ps_io_ops_t ioops;
    unsigned int idx;
} virtio_qemu_t;

extern vmm_pci_space_t *pci;
extern vmm_io_port_list_t *io_ports;

static ps_io_ops_t ops;

static vmm_pci_entry_t vmm_virtio_qemu_pci_bar(virtio_qemu_t *qemu)
{
    vmm_pci_address_t bogus_addr = {
	    .bus = 0,
	    .dev = 0,
	    .fun = 0,
    };
    return vmm_pci_create_passthrough(bogus_addr, make_qemu_pci_config(qemu));
}

static void virtio_qemu_ack(vm_vcpu_t *vcpu, int irq, void *token) {}

virtio_qemu_t *virtio_qemu_init(vm_t *vm, vmm_pci_space_t *pci)
{
    int err = ps_new_stdlib_malloc_ops(&ops.malloc_ops);
    ZF_LOGF_IF(err, "Failed to get malloc ops");

    virtio_qemu_t *qemu;
    err = ps_calloc(&ops.malloc_ops, 1, sizeof(*qemu), (void **)&qemu);
    ZF_LOGF_IF(err, "Failed to allocate virtio qemu");

    vmm_pci_entry_t qemu_entry = vmm_virtio_qemu_pci_bar(qemu);
    vmm_pci_add_entry(pci, qemu_entry, NULL);

    err =  vm_register_irq(vm->vcpus[BOOT_VCPU], VIRTIO_QEMU_PLAT_INTERRUPT_LINE, &virtio_qemu_ack, NULL);
    if (err) {
        ZF_LOGE("Failed to register console irq");
        return NULL;
    }

    return qemu;
}

static virtio_qemu_t *pci_devs[16];
static unsigned int pci_dev_count;

static void register_pci_device(void)
{
    ZF_LOGI("Registering PCI device");

    virtio_qemu_t *virtio_qemu = virtio_qemu_init(vm, pci);
    if (!virtio_qemu) {
        ZF_LOGF("Failed to initialise virtio qemu");
    }

    pci_devs[pci_dev_count] = virtio_qemu;
    pci_devs[pci_dev_count]->idx = pci_dev_count;
    pci_dev_count++;
}

int irq_ext_modify(vm_vcpu_t *vcpu, unsigned int source, unsigned int irq, bool set);

static int vcpu_finish_qemu_read(rpcmsg_t *msg);
static int vcpu_finish_qemu_write(rpcmsg_t *msg);

static bool handle_async(rpcmsg_t *msg)
{
    int err;

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_READ:
        err = vcpu_finish_qemu_read(msg);
        if (err) {
            return false;
        }
        break;
    case QEMU_OP_WRITE:
        err = vcpu_finish_qemu_write(msg);
        if (err) {
            return false;
        }
        break;
    case QEMU_OP_SET_IRQ:
        irq_ext_modify(vm->vcpus[BOOT_VCPU], msg->mr1, VIRTIO_QEMU_PLAT_INTERRUPT_LINE, true);
        break;
    case QEMU_OP_CLR_IRQ:
        irq_ext_modify(vm->vcpus[BOOT_VCPU], msg->mr1, VIRTIO_QEMU_PLAT_INTERRUPT_LINE, false);
        break;
    case QEMU_OP_START_VM:
        ok_to_run = 1;
        sync_sem_post(&qemu_started);
        break;
    case QEMU_OP_REGISTER_PCI_DEV:
        register_pci_device();
        break;
    default:
        return false;
    }

    return true;
}

static rpcmsg_t *peek_sync_msg(rpcmsg_queue_t *q)
{
    for (;;) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            return NULL;
        }
        if (!handle_async(msg)) {
            return msg;
        }
        rpcmsg_queue_advance_head(q);
    }
}

static void intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(intervm_callback, opaque);
    assert(!err);

    rpcmsg_t *msg = peek_sync_msg(rx_queue);
    if (msg) {
        sync_sem_post(&handoff);
    }
}

void wait_for_host_qemu(void)
{
    // camkes_protect_reply_cap() ??
    /* TODO: in theory it should be enough to wait for the semaphore
     * but seems that any virtio console input makes the wait operation
     * complete without the corresponding post operation ever called.
     * It could be some kind of programming error but first we need
     * to study seL4 IPC in more depth.
     */
    do {
        ZF_LOGI("qemu_started sem value = %d", qemu_started.value);
        int err = sync_sem_wait(&qemu_started);
        ZF_LOGI("sync_sem_wait rv = %d", err);
        ZF_LOGI("qemu_started sem value = %d", qemu_started.value);
    } while (!ok_to_run);
}

/* VM0 does not have this */
int WEAK vmm_caller_call(unsigned int arg1, unsigned int arg2)
{
    return 0;
}

static inline void qemu_doorbell_ring(void)
{
//    ZF_LOGD("VMM ringing QEMU's doorbell");
    vmm_caller_call(0, 0);
}

static bool debug_blacklisted(uintptr_t addr)
{
    return true;
    if (addr >= 0x09000000 && addr < 0x09001000) {
        return true;
    }
    return false;
}

static unsigned int qemu_index(uintptr_t addr, void *cookie)
{
    if (addr >= (1ULL << 20))
	    return QEMU_PCIDEV_MASK;

    virtio_qemu_t *qemu = cookie;
    return qemu->idx << QEMU_PCIDEV_SHIFT;
}

static unsigned int seq_id;

int qemu_start_read(vm_vcpu_t *vcpu, void *cookie, uintptr_t offset, size_t size)
{
    assert(size >= 0 && size <= sizeof(seL4_Word));

    rpcmsg_t *msg = rpcmsg_queue_tail(tx_queue);
    assert(msg);

    seL4_Word vcpu_id = vcpu ? vcpu->vcpu_id : QEMU_VCPU_NONE;

    msg->mr0 = QEMU_OP_READ | qemu_index(offset, cookie) | QEMU_ID_FROM(seq_id++) | (vcpu_id << QEMU_VCPU_SHIFT);
    msg->mr1 = offset;
    msg->mr2 = size;
    rpcmsg_queue_advance_tail(tx_queue);

    qemu_doorbell_ring();

    return 0;
}

int qemu_start_write(vm_vcpu_t *vcpu, void *cookie, uintptr_t offset, size_t size, uint32_t value)
{
    if (!debug_blacklisted(offset)) {
        ZF_LOGD("offset=%"PRIx64" size=%zu value=%08"PRIu32, offset, size, value);
    }
    assert(size >= 0 && size <= sizeof(seL4_Word));

    rpcmsg_t *msg = rpcmsg_queue_tail(tx_queue);
    assert(msg);

    seL4_Word vcpu_id = vcpu ? vcpu->vcpu_id : QEMU_VCPU_NONE;

    msg->mr0 = QEMU_OP_WRITE | qemu_index(offset, cookie) | QEMU_ID_FROM(seq_id++) | (vcpu_id << QEMU_VCPU_SHIFT);
    msg->mr1 = offset;
    msg->mr2 = size;
    memcpy(&msg->mr3, &value, size);
    rpcmsg_queue_advance_tail(tx_queue);

    qemu_doorbell_ring();

    return 0;
}

static inline vm_vcpu_t *vcpu_from_msg(rpcmsg_t *msg)
{
    unsigned int vcpu_id = QEMU_VCPU(msg->mr0);
    if (vcpu_id == QEMU_VCPU_NONE) {
        return NULL;
    }
    return vm->vcpus[vcpu_id];
}

static int vcpu_finish_qemu_read(rpcmsg_t *msg)
{
    uint32_t data = (uint32_t) msg->mr3;

    vm_vcpu_t *vcpu = vcpu_from_msg(msg);
    if (vcpu == NULL) {
        return -1;
    }

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    set_vcpu_fault_data(vcpu, data << s);

    advance_vcpu_fault(vcpu);

    return 0;
}

static int vcpu_finish_qemu_write(rpcmsg_t *msg)
{
    vm_vcpu_t *vcpu = vcpu_from_msg(msg);
    if (vcpu == NULL) {
        return -1;
    }

    advance_vcpu_fault(vcpu);

    return 0;
}

#if 0
static void qemu_putc_log(char ch)
{
    logbuffer_t *lb = logbuffer;

    if ((lb->sz + 1) < sizeof(lb->data)) {
        lb->data[lb->sz++] = ch;
    }

    if (ch == 0x0a) {
        rpcmsg_t *msg = rpcmsg_queue_tail(tx_queue);
        assert(msg);

    	rpcmsg_t original = *msg;

        msg->mr0 = QEMU_OP_PUTC_LOG;
        rpcmsg_queue_advance_tail(tx_queue);

        qemu_doorbell_ring();
        vmm_doorbell_wait();
    
        msg = peek_sync_msg(rx_queue);
        if (QEMU_OP(msg->mr0) != QEMU_OP_PUTC_LOG) {
            ZF_LOGE("message does not match");
            rpcmsg_queue_dump("rx", rx_queue, rx_queue->head);
            printf("-----------\n");
            rpcmsg_queue_dump("tx", tx_queue, tx_queue->tail);
            printf("-----------\n");
        }
        rpcmsg_queue_advance_head(rx_queue);

        lb->sz = 0;
    }
}
#endif

#define QEMU_READ_OP(sz) do {                                           \
    uint32_t result;                                                    \
    (void) qemu_start_read(NULL, cookie, offset, sz);   \
    sync_sem_wait(&handoff); \
    rpcmsg_t *msg = rpcmsg_queue_head(rx_queue); \
    result = (uint32_t) msg->mr3; \
    rpcmsg_queue_advance_head(rx_queue); \
    return result;                                                      \
} while (0)

#define QEMU_WRITE_OP(sz) do { \
    (void) qemu_start_write(NULL, cookie, offset, sz, val); \
    sync_sem_wait(&handoff); \
    rpcmsg_queue_advance_head(rx_queue); \
} while (0)

static uint8_t qemu_pci_read8(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    if (offset == 0x3c)
        return VIRTIO_CON_PLAT_INTERRUPT_LINE;
    QEMU_READ_OP(1);
}

static uint16_t qemu_pci_read16(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    QEMU_READ_OP(2);
}

static uint32_t qemu_pci_read32(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    QEMU_READ_OP(4);
}

static void qemu_pci_write8(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint8_t val)
{
    QEMU_WRITE_OP(1);
}

static void qemu_pci_write16(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint16_t val)
{
    QEMU_WRITE_OP(2);
}

static void qemu_pci_write32(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint32_t val)
{
    QEMU_WRITE_OP(4);
}

static vmm_pci_config_t make_qemu_pci_config(void *cookie)
{
    return (vmm_pci_config_t) {
        .cookie = cookie,
        .ioread8 = qemu_pci_read8,
        .ioread16 = qemu_pci_read16,
        .ioread32 = qemu_pci_read32,
        .iowrite8 = qemu_pci_write8,
        .iowrite16 = qemu_pci_write16,
        .iowrite32 = qemu_pci_write32,
    };
}

static void qemu_read_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    int err;

    if (paddr == 0x09000018) {
        /* UART flag register: both RX and TX FIFOs empty */
        set_vcpu_fault_data(vcpu, 0x90);
        return;
    }

    err = qemu_start_read(vcpu, NULL, paddr, len);
    if (err) {
        ZF_LOGE("Failure performing read from QEMU");
    }
}

static void qemu_write_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    uint32_t mask;
    uint32_t value;
    uint8_t bar;
    int err;

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    mask = get_vcpu_fault_data_mask(vcpu) >> s;
    value = get_vcpu_fault_data(vcpu) & mask;

    if (paddr == 0x09000000) {
        /* UART data register */
        printf("%c", (unsigned char) value);
#if 0
        qemu_putc_log((unsigned char) value);
#endif
        return;
    }

    err = qemu_start_write(vcpu, NULL, paddr, len, value);
    if (err) {
        ZF_LOGE("Failure writing to QEMU");
    }
}

static uint32_t get_fault_data(vm_vcpu_t *vcpu)
{
    return get_vcpu_fault_data(vcpu) & get_vcpu_fault_data_mask(vcpu);
}

memory_fault_result_t external_fault_callback(vm_t *vm, vm_vcpu_t *vcpu, uintptr_t paddr, size_t len, void *cookie)
{
    if (paddr < 0x60000000 || paddr >= 0x60100000) {
        if (paddr < 0x09000000 || paddr >= 0x09001000) {
            return FAULT_UNHANDLED;
        }
    }

    if (is_vcpu_read_fault(vcpu)) {
        qemu_read_fault(vcpu, paddr, len);
    } else {
        qemu_write_fault(vcpu, paddr, len);
    }

    /* Let's not advance the fault here -- the reply from QEMU does that */
    return FAULT_HANDLED;
}

void qemu_init(vm_t *vm, void *cookie)
{
}

int vmm_callee_call(unsigned int arg1, unsigned int arg2)
{
    seL4_Send(vm->guest_endpoint, seL4_MessageInfo_new(0, 0, 0, 0));
    return 0;
}

DEFINE_MODULE(qemu, NULL, qemu_init)
