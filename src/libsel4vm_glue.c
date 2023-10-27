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

#include "trace.h"
#include "ioreq.h"

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

typedef int (*rpc_callback_fn_t)(io_proxy_t *io_proxy, unsigned int op,
                                 rpcmsg_t *msg);

/************************* PCI typedefs begin here **************************/

#define PCI_INTX_IRQ_BASE ((VIRTIO_CON_PLAT_INTERRUPT_LINE) + 1)

/************************** PCI typedefs end here ***************************/

/************************** PCI externs begin here **************************/

extern vmm_pci_space_t *pci;

/*************************** PCI externs end here ***************************/

/*********************** main declarations begin here ***********************/

static int ioack_vcpu_read(seL4_Word data, void *cookie);
static int ioack_vcpu_write(seL4_Word data, void *cookie);

/************************ main declarations end here ************************/

/*************************** PCI code begins here ***************************/

#define pci_cfg_read(_pcidev, _offset, _sz) \
    pci_cfg_ioreq_native(_pcidev, SEL4_IO_DIR_READ, _offset, _sz, 0)
#define pci_cfg_write(_pcidev, _offset, _sz, _val) \
    pci_cfg_ioreq_native(_pcidev, SEL4_IO_DIR_WRITE, _offset, _sz, _val)

static uint8_t pci_cfg_read8(void *cookie, vmm_pci_address_t addr,
                             unsigned int offset)
{
    switch (offset) {
    case PCI_INTERRUPT_LINE:
        /* Map device interrupt to INTx lines */
        return PCI_INTX_IRQ_BASE + pci_map_irq(cookie, PCI_INTA);
    case PCI_INTERRUPT_PIN:
        /* Map device interrupt to INTx pin */
        return pci_map_irq(cookie, PCI_INTA) + 1;
    default:
        return pci_cfg_read(cookie, offset, 1);
    }
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

static int pcidev_register(pcidev_t *pcidev, void *pci_bus_cookie)
{
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

    vmm_pci_space_t *pci = pci_bus_cookie;

    vmm_pci_entry_t entry = vmm_pci_create_passthrough(bogus_addr, config);
    vmm_pci_address_t addr;
    int err = vmm_pci_add_entry(pci, entry, &addr);
    if (err) {
        ZF_LOGE("vmm_pci_add_entry() failed (%d)", err);
        return -1;
    }

    return addr.dev;
}

/**************************** PCI code ends here ****************************/

static int handle_control(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    switch (op) {
    case RPC_MR0_OP_NOTIFY_STATUS:
        io_proxy->status = msg->mr1;
        sync_sem_post(&io_proxy->status_changed);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    return RPCMSG_RC_HANDLED;
}

static rpc_callback_fn_t rpc_callbacks[] = {
    handle_mmio,
    handle_pci,
    handle_control,
    NULL,
};

int rpc_run(io_proxy_t *io_proxy)
{
    rpcmsg_t *msg;

    while ((msg = rpcmsg_queue_head(io_proxy->rpc.rx_queue)) != NULL) {
        unsigned int state = BIT_FIELD_GET(msg->mr0, RPC_MR0_STATE);

        if (state == RPC_MR0_STATE_COMPLETE) {
            rpc_assert(!"logic error");
        } else if (state == RPC_MR0_STATE_RESERVED) {
            /* emu side is crafting message, let's continue later */
            break;
        }

        int rc = RPCMSG_RC_NONE;
        for (rpc_callback_fn_t *cb = rpc_callbacks;
             rc == RPCMSG_RC_NONE && *cb; cb++) {
            rc = (*cb)(io_proxy, BIT_FIELD_GET(msg->mr0, RPC_MR0_OP), msg);
        }

        if (rc == RPCMSG_RC_ERROR) {
            return -1;
        }

        if (rc == RPCMSG_RC_NONE) {
            if (BIT_FIELD_GET(msg->mr0, RPC_MR0_STATE) != RPC_MR0_STATE_PROCESSING) {
                ZF_LOGE("Unknown RPC message %u", BIT_FIELD_GET(msg->mr0, RPC_MR0_OP));
                return -1;
            }
            /* try again later */
            return 0;
        }

        msg->mr0 = BIT_FIELD_SET(msg->mr0, RPC_MR0_STATE, RPC_MR0_STATE_COMPLETE);
        rpcmsg_queue_advance_head(io_proxy->rpc.rx_queue);
    }

    return 0;
}

static int ioack_vcpu_read(seL4_Word data, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    set_vcpu_fault_data(vcpu, data << s);
    advance_vcpu_fault(vcpu);

    return 0;
}

static int ioack_vcpu_write(seL4_Word data, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    advance_vcpu_fault(vcpu);

    return 0;
}

static memory_fault_result_t mmio_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie)
{
    io_proxy_t *io_proxy = cookie;

    unsigned int dir = SEL4_IO_DIR_READ;
    seL4_Word value = 0;

    if (!is_vcpu_read_fault(vcpu)) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word mask = get_vcpu_fault_data_mask(vcpu) >> s;
        value = get_vcpu_fault_data(vcpu) & mask;
        dir = SEL4_IO_DIR_WRITE;
    }

    int err = ioreq_start(io_proxy, vcpu->vcpu_id, ioack_vcpu_read,
                          ioack_vcpu_write, vcpu, AS_GLOBAL, dir, paddr, len,
                          value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return FAULT_ERROR;
    }

    /* ioreq_start() flushes queue from VMM to device, we do not need to */

    return FAULT_HANDLED;
}

int libsel4vm_io_proxy_init(vm_t *vm, io_proxy_t *io_proxy)
{
    io_proxy_init(io_proxy);

    io_proxy->pcidev_register = pcidev_register;
    io_proxy->pci_bus_cookie = pci;

    vm_memory_reservation_t *reservation;

    /* TODO: here we allocate a region for all devices provided by backend,
     * whereas we should consult capability list of the PCI device to find
     * out the virtio control plane details -- in other words, do it in
     * pcidev_register() -- at that point these configuration variables
     * can be factored out.
     */
    reservation = vm_reserve_memory_at(vm, io_proxy->ctrl_base,
                                       io_proxy->ctrl_size, mmio_fault_handler,
                                       io_proxy);
    ZF_LOGF_IF(!reservation, "Cannot reserve vspace for virtio control plane");

    int err = pci_irq_init(vm->vcpus[BOOT_VCPU], PCI_INTX_IRQ_BASE);
    if (err) {
        ZF_LOGE("pci_irq_init() failed");
        return -1;
    }

    err = io_proxy_run(io_proxy);
    if (err) {
        ZF_LOGE("io_proxy_run() failed");
        return -1;
    }

    io_proxy_wait_until_device_ready(io_proxy);

    return 0;
}
