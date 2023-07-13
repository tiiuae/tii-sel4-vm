/*
 * Copyright 2022, 2023, Technology Innovation Institute
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

#include <tii/shared_irq_line.h>

#include "sel4-qemu.h"
#include "trace.h"
#include "ioreq.h"

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

#define VIRTIO_PLAT_INTERRUPT_LINE VIRTIO_CON_PLAT_INTERRUPT_LINE

extern void *ctrl;
extern void *iobuf;

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

volatile int ok_to_run = 0;

static sync_sem_t backend_started;

extern vka_t _vka;

extern const int vmid;

static void intervm_callback(void *opaque);

static vmm_pci_config_t make_qemu_pci_config(void *cookie);

static void wait_for_backend(void);

static vm_t *vm;

/*********************** main declarations begin here ***********************/

static shared_irq_line_t irq_line;

/************************ main declarations end here ************************/

static void user_pre_load_linux(void)
{
    ioreq_init(iobuf);

    if (sync_sem_new(&_vka, &backend_started, 0)) {
        ZF_LOGF("Unable to allocate handoff semaphore");
    }

    if (intervm_sink_reg_callback(intervm_callback, ctrl)) {
        ZF_LOGF("Problem registering intervm sink callback");
    }

    /* load_linux() eventually calls fdt_generate_vpci_node(), which blocks
     * unless backend is already running. Therefore we need to listen to start
     * signal here before proceeding to load_linux() in VM_Arm.
     */
    ZF_LOGI("waiting for virtio backend");
    wait_for_backend();
    ZF_LOGI("virtio backend up, continuing");
}

typedef struct virtio_qemu {
    unsigned int iobase;
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

virtio_qemu_t *virtio_qemu_init(vm_t *vm, vmm_pci_space_t *pci)
{
    int err = ps_new_stdlib_malloc_ops(&ops.malloc_ops);
    ZF_LOGF_IF(err, "Failed to get malloc ops");

    virtio_qemu_t *qemu;
    err = ps_calloc(&ops.malloc_ops, 1, sizeof(*qemu), (void **)&qemu);
    ZF_LOGF_IF(err, "Failed to allocate virtio qemu");

    vmm_pci_entry_t qemu_entry = vmm_virtio_qemu_pci_bar(qemu);
    vmm_pci_add_entry(pci, qemu_entry, NULL);

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

static int pci_intx_set(unsigned int intx, bool level)
{
    /* #INTA ... #INTD */
    if (intx >= 4) {
        ZF_LOGE("intx %u invalid", intx);
        return -1;
    }

    return shared_irq_line_change(&irq_line, intx, level);
}

static bool handle_pci(rpcmsg_t *msg)
{
    int err = 0;

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_SET_IRQ:
        err = pci_intx_set(msg->mr1, true);
        break;
    case QEMU_OP_CLR_IRQ:
        err = pci_intx_set(msg->mr1, false);
        break;
    case QEMU_OP_REGISTER_PCI_DEV:
        register_pci_device();
        break;
    default:
        return false;
    }

    if (err) {
        ZF_LOGF("fatal error");
        /* no return */
    }

    return true;
}

static bool handle_async(rpcmsg_t *msg)
{
    int err;

    if (handle_pci(msg)) {
        return true;
    }

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_IO_HANDLED:
        if (ioreq_mmio_finish(vm, iobuf, msg->mr1))
            return false;
        break;
    case QEMU_OP_START_VM: {
        ok_to_run = 1;
        sync_sem_post(&backend_started);
        break;
    }
    default:
        return false;
    }

    return true;
}

static int rpc_run(rpcmsg_queue_t *q)
{
    while (!rpcmsg_queue_empty(q)) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            return -1;
        }
        if (!handle_async(msg)) {
            return -1;
        }
        rpcmsg_queue_advance_head(q);
    }

    return 0;
}

static void intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(intervm_callback, opaque);
    assert(!err);

    err = rpc_run(rx_queue);
    if (err) {
        ZF_LOGF("rpc_run() failed, guest corrupt");
        /* no return */
    }
}

static void wait_for_backend(void)
{
    // camkes_protect_reply_cap() ??
    /* TODO: in theory it should be enough to wait for the semaphore
     * but seems that any virtio console input makes the wait operation
     * complete without the corresponding post operation ever called.
     * It could be some kind of programming error but first we need
     * to study seL4 IPC in more depth.
     */
    do {
        ZF_LOGI("backend_started sem value = %d", backend_started.value);
        int err = sync_sem_wait(&backend_started);
        ZF_LOGI("sync_sem_wait rv = %d", err);
        ZF_LOGI("backend_started sem value = %d", backend_started.value);
    } while (!ok_to_run);
}

static inline void backend_notify(void)
{
    intervm_source_emit();
}

static inline uint32_t qemu_pci_start(virtio_qemu_t *qemu, unsigned int dir,
                                      uintptr_t offset, size_t size,
                                      uint32_t value)
{
    int slot = ioreq_pci_start(iobuf, qemu->idx, dir, offset, size, value);
    assert(ioreq_slot_valid(slot));

    backend_notify();

    value = ioreq_pci_finish(iobuf, slot);

    return value;
}

#define qemu_pci_read(_qemu, _offset, _sz) \
    qemu_pci_start(_qemu, SEL4_IO_DIR_READ, _offset, _sz, 0)
#define qemu_pci_write(_qemu, _offset, _sz, _val) \
    qemu_pci_start(_qemu, SEL4_IO_DIR_WRITE, _offset, _sz, _val)

static uint8_t qemu_pci_read8(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    if (offset == 0x3c) {
        return VIRTIO_PLAT_INTERRUPT_LINE;
    }
    return qemu_pci_read(cookie, offset, 1);
}

static uint16_t qemu_pci_read16(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    return qemu_pci_read(cookie, offset, 2);
}

static uint32_t qemu_pci_read32(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    return qemu_pci_read(cookie, offset, 4);
}

static void qemu_pci_write8(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint8_t val)
{
    qemu_pci_write(cookie, offset, 1, val);
}

static void qemu_pci_write16(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint16_t val)
{
    qemu_pci_write(cookie, offset, 2, val);
}

static void qemu_pci_write32(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint32_t val)
{
    qemu_pci_write(cookie, offset, 4, val);
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

static inline void qemu_read_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    int err;

    err = ioreq_mmio_start(iobuf, vcpu, SEL4_IO_DIR_READ, paddr, len, 0);
    if (err < 0) {
        ZF_LOGF("Failure starting mmio read request");
    }
    backend_notify();
}

static inline void qemu_write_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    uint32_t mask;
    uint32_t value;
    int err;

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    mask = get_vcpu_fault_data_mask(vcpu) >> s;
    value = get_vcpu_fault_data(vcpu) & mask;

    err = ioreq_mmio_start(iobuf, vcpu, SEL4_IO_DIR_WRITE, paddr, len, value);
    if (err < 0) {
        ZF_LOGF("Failure starting mmio write request");
    }
    backend_notify();
}

static memory_fault_result_t qemu_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie)
{
    if (is_vcpu_read_fault(vcpu)) {
        qemu_read_fault(vcpu, paddr, len);
    } else {
        qemu_write_fault(vcpu, paddr, len);
    }

    /* Let's not advance the fault here -- the reply from QEMU does that */
    return FAULT_HANDLED;
}

static void qemu_init(vm_t *_vm, void *cookie)
{
    vm_memory_reservation_t *reservation;

    vm = _vm;

    if (vmid != 0) {
        reservation = vm_reserve_memory_at(vm, PCI_MEM_REGION_ADDR, 524288,
                                           qemu_fault_handler, NULL);
        ZF_LOGF_IF(!reservation, "Cannot reserve virtio MMIO region");

        int err = shared_irq_line_init(&irq_line, vm->vcpus[BOOT_VCPU],
                                       VIRTIO_PLAT_INTERRUPT_LINE);
        if (err) {
            ZF_LOGF("shared_irq_line_init() failed");
            /* no return */
        }

        user_pre_load_linux();
    }
}

DEFINE_MODULE(qemu, NULL, qemu_init)
