/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

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

typedef int (*rpc_callback_fn_t)(io_proxy_t *io_proxy, rpcmsg_t *msg);

extern vka_t _vka;

/************************* PCI typedefs begin here **************************/

typedef struct pcidev {
    unsigned int idx;
    io_proxy_t *io_proxy;
} pcidev_t;

/************************** PCI typedefs end here ***************************/

/************************** PCI externs begin here **************************/

extern vmm_pci_space_t *pci;

/*************************** PCI externs end here ***************************/

/*********************** main declarations begin here ***********************/

static shared_irq_line_t irq_line;

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
    int slot = ioreq_start(pcidev->io_proxy, VCPU_NONE, AS_PCIDEV(pcidev->idx),
                           dir, offset, size, value);
    assert(ioreq_slot_valid(slot));

    io_proxy_backend_notify(pcidev->io_proxy);

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

static int pcidev_register(vmm_pci_space_t *pci, io_proxy_t *io_proxy)
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
    pcidev->io_proxy = io_proxy;

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

static int handle_pci(io_proxy_t *io_proxy, rpcmsg_t *msg)
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
        err = pcidev_register(pci, io_proxy);
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

static int handle_mmio(io_proxy_t *io_proxy, rpcmsg_t *msg)
{
    if (QEMU_OP(msg->mr0) != QEMU_OP_IO_HANDLED) {
        return 0;
    }

    int err = ioreq_finish(io_proxy, msg->mr1);
    if (err) {
        return -1;
    }

    return 1;
}

static int handle_control(io_proxy_t *io_proxy, rpcmsg_t *msg)
{
    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_START_VM:
        io_proxy->ok_to_run = 1;
        sync_sem_post(&io_proxy->backend_started);
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

int rpc_run(io_proxy_t *io_proxy)
{
    rpcmsg_queue_t *q = io_proxy->rx_queue;

    while (!rpcmsg_queue_empty(q)) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            return -1;
        }
        int rc = 0;
        for (rpc_callback_fn_t *cb = rpc_callbacks; !rc && *cb; cb++) {
            rc = (*cb)(io_proxy, msg);
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

static memory_fault_result_t mmio_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie)
{
    io_proxy_t *io_proxy = cookie;

    unsigned int dir = SEL4_IO_DIR_READ;
    uint64_t value = 0;

    if (!is_vcpu_read_fault(vcpu)) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        uint64_t mask = get_vcpu_fault_data_mask(vcpu) >> s;
        value = get_vcpu_fault_data(vcpu) & mask;
        dir = SEL4_IO_DIR_WRITE;
    }

    int err = ioreq_start(io_proxy, vcpu, AS_GLOBAL, dir, paddr, len, value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return FAULT_ERROR;
    }

    io_proxy_backend_notify(io_proxy);

    /* Let's not advance the fault here -- the reply from QEMU does that */
    return FAULT_HANDLED;
}

static int irq_init(vm_t *vm)
{
    static bool done = false;

    if (!done) {
        int err = shared_irq_line_init(&irq_line, vm->vcpus[BOOT_VCPU],
                                       VIRTIO_PLAT_INTERRUPT_LINE);

        if (!err) {
            done = true;
        }
    }

    return done ? 0 : -1;
}

int libsel4vm_io_proxy_init(vm_t *vm, io_proxy_t *io_proxy)
{
    io_proxy_init(io_proxy);

    vm_memory_reservation_t *reservation;

    reservation = vm_reserve_memory_at(vm, io_proxy->mmio_addr,
                                       io_proxy->mmio_size, mmio_fault_handler,
                                       io_proxy);
    ZF_LOGF_IF(!reservation, "Cannot reserve virtio MMIO region");

    int err = irq_init(vm);
    if (err) {
        ZF_LOGE("irq_init() failed");
        return -1;
    }

    err = io_proxy_set_callback(io_proxy);
    if (err) {
        ZF_LOGE("io_proxy_set_callback() failed");
        return -1;
    }

    /* load_linux() eventually calls fdt_generate_vpci_node(), which blocks
     * unless backend is already running. Therefore we need to listen to start
     * signal here before proceeding to load_linux() in VM_Arm.
     */
    ZF_LOGI("waiting for PCI backend");
    io_proxy_wait_for_backend(io_proxy);
    ZF_LOGI("PCI backend up, continuing");

    return 0;
}
