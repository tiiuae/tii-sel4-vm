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

int vmm_pci_mem_device_qemu_read(void *cookie, uintptr_t offset, size_t size, uint32_t *result);
int vmm_pci_mem_device_qemu_write(void *cookie, uintptr_t offset, size_t size, uint32_t value);

extern void *ctrl;
extern void *memdev;

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

volatile int ok_to_run = 0;

static sync_sem_t handoff;
static sync_sem_t qemu_started;

extern vka_t _vka;

extern unsigned long linux_ram_base;

static void intervm_callback(void *opaque);

static vmm_pci_config_t make_qemu_pci_config(void *cookie);

static vm_t *vm;

void qemu_initialize_semaphores(vm_t *_vm)
{
    vm = _vm;

    if (linux_ram_base != 0x48000000) {
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

static bool handle_async(rpcmsg_t *msg)
{
    switch (QEMU_OP(msg->mr0)) {
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

static void intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(intervm_callback, opaque);
    assert(!err);

    for (;;) {
        rpcmsg_t *msg = rpcmsg_queue_head(rx_queue);
        if (!msg) {
            break;
        }
        if (handle_async(msg)) {
            rpcmsg_queue_advance_head(rx_queue);
        } else {
            sync_sem_post(&handoff);
	    break;
        }
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

static inline void qemu_doorbell_ring(void)
{
//    ZF_LOGD("VMM ringing QEMU's doorbell");
    intervm_source_emit();
}

static inline void vmm_doorbell_wait(void)
{
//    ZF_LOGD("VMM waiting for doorbell");
    sync_sem_wait(&handoff);
//    ZF_LOGD("QEMU rang VMM's doorbell");
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

int vmm_pci_mem_device_qemu_read(void *cookie, uintptr_t offset, size_t size, uint32_t *result)
{
    assert(size >= 0 && size <= 4);

    rpcmsg_t *msg = rpcmsg_queue_tail(tx_queue);
    assert(msg);

    msg->mr0 = QEMU_OP_READ | qemu_index(offset, cookie);
    msg->mr1 = offset;
    msg->mr2 = size;
    rpcmsg_queue_advance_tail(tx_queue);

    qemu_doorbell_ring();
//    ZF_LOGD("waiting for reply");
    vmm_doorbell_wait();

    msg = rpcmsg_queue_head(rx_queue);
    assert(msg);
    memcpy(result, &msg->mr3, size);
    if (QEMU_OP(msg->mr0) != QEMU_OP_READ || msg->mr1 != offset || msg->mr2 != size) {
        ZF_LOGE("message does not match");
        rpcmsg_queue_dump("rx", rx_queue, rx_queue->head);
        printf("-----------\n");
        rpcmsg_queue_dump("tx", tx_queue, tx_queue->tail);
        printf("-----------\n");
    }

    rpcmsg_queue_advance_head(rx_queue);

    uint32_t value = 0;
    memcpy(&value, result, size);
    if (!debug_blacklisted(offset)) {
        ZF_LOGD("offset=%"PRIx64" size=%d value=%08"PRIx32, offset, size, value);
    }

    return 0;
}

int vmm_pci_mem_device_qemu_write(void *cookie, uintptr_t offset, size_t size, uint32_t value)
{
    if (!debug_blacklisted(offset)) {
        ZF_LOGD("offset=%"PRIx64" size=%zu value=%08"PRIu32, offset, size, value);
    }
    assert(size >= 0 && size <= sizeof(seL4_Word));

    rpcmsg_t *msg = rpcmsg_queue_tail(tx_queue);
    assert(msg);

    msg->mr0 = QEMU_OP_WRITE | qemu_index(offset, cookie);
    msg->mr1 = offset;
    msg->mr2 = size;
    memcpy(&msg->mr3, &value, size);
    rpcmsg_queue_advance_tail(tx_queue);

    qemu_doorbell_ring();
//    ZF_LOGD("waiting for reply");
    vmm_doorbell_wait();

    msg = rpcmsg_queue_head(rx_queue);
    assert(msg);
    if (QEMU_OP(msg->mr0) != QEMU_OP_WRITE || msg->mr1 != offset || msg->mr2 != size) {
        ZF_LOGE("message does not match");
        rpcmsg_queue_dump("rx", rx_queue, rx_queue->head);
        printf("-----------\n");
        rpcmsg_queue_dump("tx", tx_queue, tx_queue->tail);
        printf("-----------\n");
    }
    rpcmsg_queue_advance_head(rx_queue);

    return 0;
}

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
    
        msg = rpcmsg_queue_head(rx_queue);
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

#define QEMU_READ_OP(sz) do {                                           \
    uint32_t result;                                                    \
    (void) vmm_pci_mem_device_qemu_read(cookie, offset, sz, &result);   \
    return result;                                                      \
} while (0)

#define QEMU_WRITE_OP(sz) (void) vmm_pci_mem_device_qemu_write(cookie, offset, sz, val);

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
    uint32_t data = 0;

    if (paddr == 0x09000018) {
        /* UART flag register: both RX and TX FIFOs empty */
        set_vcpu_fault_data(vcpu, 0x90);
        return;
    }

    err = vmm_pci_mem_device_qemu_read(NULL, paddr, len, &data);
    if (err) {
        ZF_LOGE("Failure performing read from QEMU");
    }

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    set_vcpu_fault_data(vcpu, data << s);
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
        qemu_putc_log((unsigned char) value);
        return;
    }

    err = vmm_pci_mem_device_qemu_write(NULL, paddr, len, value);
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

    advance_vcpu_fault(vcpu);
    return FAULT_HANDLED;
}

void qemu_init(vm_t *vm, void *cookie)
{
}

DEFINE_MODULE(qemu, NULL, qemu_init)
