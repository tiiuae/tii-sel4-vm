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

typedef int (*rpc_callback_fn_t)(rpcmsg_t *msg);

extern void *ctrl;
extern void *iobuf;

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

volatile int ok_to_run = 0;

static sync_sem_t backend_started;

extern vka_t _vka;

extern const int vmid;

static void intervm_callback(void *opaque);

static void wait_for_backend(void);

static vm_t *vm;

/************************* PCI typedefs begin here **************************/

typedef struct pcidev {
    unsigned int idx;
} pcidev_t;

/************************** PCI typedefs end here ***************************/

/************************** PCI externs begin here **************************/

extern vmm_pci_space_t *pci;

/*************************** PCI externs end here ***************************/

/*********************** main declarations begin here ***********************/

static shared_irq_line_t irq_line;

static void backend_notify(void);

/************************ main declarations end here ************************/

/*********************** PCI declarations begin here ************************/

static pcidev_t *pci_devs[16];
static unsigned int pci_dev_count;

/************************ PCI declarations end here *************************/

/*************************** PCI code begins here ***************************/

static inline uint64_t pci_cfg_start(pcidev_t *pcidev, unsigned int dir,
                                     uintptr_t offset, size_t size,
                                     uint64_t value)
{
    int slot = ioreq_start(iobuf, VCPU_NONE, AS_PCIDEV(pcidev->idx), dir,
                           offset, size, value);
    assert(ioreq_slot_valid(slot));

    backend_notify();

    int err = ioreq_wait(&value);
    if (err) {
        ZF_LOGE("ioreq_wait() failed");
        return 0;
    }

    return value;
}

#define pci_cfg_read(_pcidev, _offset, _sz) \
    pci_cfg_start(_pcidev, SEL4_IO_DIR_READ, _offset, _sz, 0)
#define pci_cfg_write(_pcidev, _offset, _sz, _val) \
    pci_cfg_start(_pcidev, SEL4_IO_DIR_WRITE, _offset, _sz, _val)

static uint8_t pci_cfg_read8(void *cookie, vmm_pci_address_t addr,
                             unsigned int offset)
{
    if (offset == 0x3c) {
        return VIRTIO_PLAT_INTERRUPT_LINE;
    }
    return pci_cfg_read(cookie, offset, 1);
}

static uint16_t pci_cfg_read16(void *cookie, vmm_pci_address_t addr,
                               unsigned int offset)
{
    return pci_cfg_read(cookie, offset, 2);
}

static uint32_t pci_cfg_read32(void *cookie, vmm_pci_address_t addr,
                                  unsigned int offset)
{
    return pci_cfg_read(cookie, offset, 4);
}

static void pci_cfg_write8(void *cookie, vmm_pci_address_t addr,
                           unsigned int offset, uint8_t val)
{
    pci_cfg_write(cookie, offset, 1, val);
}

static void pci_cfg_write16(void *cookie, vmm_pci_address_t addr,
                            unsigned int offset, uint16_t val)
{
    pci_cfg_write(cookie, offset, 2, val);
}

static void pci_cfg_write32(void *cookie, vmm_pci_address_t addr,
                            unsigned int offset, uint32_t val)
{
    pci_cfg_write(cookie, offset, 4, val);
}

static int pcidev_register(vm_t *vm, vmm_pci_space_t *pci)
{
    pcidev_t *pcidev = calloc(1, sizeof(*pcidev));
    if (!pcidev) {
        ZF_LOGE("Failed to allocate memory");
        return -1;
    }

    vmm_pci_address_t bogus_addr = {
        .bus = 0,
        .dev = 0,
        .fun = 0,
    };

    vmm_pci_config_t config = {
        .cookie = pcidev,
        .ioread8 = pci_cfg_read8,
        .ioread16 = pci_cfg_read16,
        .ioread32 = pci_cfg_read32,
        .iowrite8 = pci_cfg_write8,
        .iowrite16 = pci_cfg_write16,
        .iowrite32 = pci_cfg_write32,
    };

    vmm_pci_entry_t entry = vmm_pci_create_passthrough(bogus_addr, config);
    vmm_pci_add_entry(pci, entry, NULL);

    pcidev->idx = pci_dev_count;

    ZF_LOGI("Registering PCI device %u", pcidev->idx);

    pci_devs[pci_dev_count++] = pcidev;

    return 0;
}

static int pcidev_intx_set(unsigned int intx, bool level)
{
    /* #INTA ... #INTD */
    if (intx >= 4) {
        ZF_LOGE("intx %u invalid", intx);
        return -1;
    }

    return shared_irq_line_change(&irq_line, intx, level);
}

static int handle_pci(rpcmsg_t *msg)
{
    int err = 0;

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_SET_IRQ:
        err = pcidev_intx_set(msg->mr1, true);
        break;
    case QEMU_OP_CLR_IRQ:
        err = pcidev_intx_set(msg->mr1, false);
        break;
    case QEMU_OP_REGISTER_PCI_DEV:
        err = pcidev_register(vm, pci);
        break;
    default:
        return 0;
    }

    if (err) {
        return -1;
    }

    return 1;
}

/**************************** PCI code ends here ****************************/

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
    ZF_LOGI("waiting for PCI backend");
    wait_for_backend();
    ZF_LOGI("PCI backend up, continuing");
}

static int handle_mmio(rpcmsg_t *msg)
{
    if (QEMU_OP(msg->mr0) != QEMU_OP_IO_HANDLED) {
        return 0;
    }

    int err = ioreq_finish(iobuf, msg->mr1);
    if (err) {
        return -1;
    }

    return 1;
}

static int handle_control(rpcmsg_t *msg)
{
    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_START_VM:
        ok_to_run = 1;
        sync_sem_post(&backend_started);
        break;
    default:
        return 0;
    }

    return 1;
}

static rpc_callback_fn_t rpc_callbacks[] = {
    handle_mmio,
    handle_pci,
    handle_control,
    NULL,
};

static int rpc_run(rpcmsg_queue_t *q)
{
    while (!rpcmsg_queue_empty(q)) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            return -1;
        }
        int rc = 0;
        for (rpc_callback_fn_t *cb = rpc_callbacks; !rc && *cb; cb++) {
            rc = (*cb)(msg);
        }
        if (rc == -1) {
            return -1;
        }
        if (rc == 0) {
            ZF_LOGW("Unknown RPC message %u", QEMU_OP(msg->mr0));
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

static void backend_notify(void)
{
    intervm_source_emit();
}


static inline void qemu_read_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    int err;

    err = ioreq_start(iobuf, vcpu, AS_GLOBAL, SEL4_IO_DIR_READ, paddr, len, 0);
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

    err = ioreq_start(iobuf, vcpu, AS_GLOBAL, SEL4_IO_DIR_WRITE, paddr, len, value);
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
