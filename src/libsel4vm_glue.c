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

#include <fdt_custom.h>
#include <tii/shared_irq_line.h>

#include "trace.h"
#include "ioreq.h"

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

#define PCI_NUM_SLOTS   (32)
#define PCI_NUM_PINS    (4)

#define INTERRUPT_PCI_INTX_BASE ((VIRTIO_CON_PLAT_INTERRUPT_LINE) + 1)

typedef int (*rpc_callback_fn_t)(io_proxy_t *io_proxy, unsigned int op,
                                 rpcmsg_t *msg);

extern vka_t _vka;

/************************* PCI typedefs begin here **************************/

typedef struct pcidev {
    unsigned int dev_id;
    unsigned int backend_pcidev_id;
    io_proxy_t *io_proxy;
} pcidev_t;

/************************** PCI typedefs end here ***************************/

/************************** PCI externs begin here **************************/

extern vmm_pci_space_t *pci;

/*************************** PCI externs end here ***************************/

/*********************** main declarations begin here ***********************/

static shared_irq_line_t pci_intx[PCI_NUM_PINS];

/************************ main declarations end here ************************/

/*********************** PCI declarations begin here ************************/

static pcidev_t pci_devs[PCI_NUM_SLOTS];
static unsigned int pci_dev_count;

/************************ PCI declarations end here *************************/

/*************************** PCI code begins here ***************************/

/* Interrupt mapping
 *
 * On ARM 0-31 are used for PPIs and SGIs, we don't allow injecting those.
 * Instead we use that range for virtual PCI devices:
 *   0-31: Virtual PCI devices. Mapped to virtual intx lines.
 *   32-N: SPIs.
 */
static inline bool irq_is_pci(uint32_t irq)
{
    return irq < PCI_NUM_SLOTS;
}

static inline int pci_swizzle(int slot, int pin)
{
    return (slot + pin) % PCI_NUM_PINS;
}

static inline int pci_map_irq(pcidev_t *pcidev)
{
    return pci_swizzle(pcidev->dev_id, 0);
}

static inline
seL4_Word pci_cfg_ioreq_native(pcidev_t *pcidev, unsigned int dir,
                               uintptr_t offset, size_t size, seL4_Word value)
{
    int err = ioreq_native(pcidev->io_proxy, AS_PCIDEV(pcidev->backend_pcidev_id),
                           dir, offset, size, &value);
    if (err) {
        ZF_LOGE("ioreq_native() failed (%d)", err);
        return 0;
    }

    return value;
}

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
        return INTERRUPT_PCI_INTX_BASE + pci_map_irq(cookie);
    case PCI_INTERRUPT_PIN:
        /* Map device interrupt to INTx pin */
        return pci_map_irq(cookie) + 1;
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

static int pcidev_register(vmm_pci_space_t *pci, io_proxy_t *io_proxy,
                           unsigned int backend_pcidev_id)
{
    if (pci_dev_count >= PCI_NUM_SLOTS) {
        ZF_LOGE("PCI device register failed: bus full");
        return -1;
    }

    pcidev_t *pcidev = &pci_devs[++pci_dev_count];

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
    vmm_pci_address_t addr;
    int err = vmm_pci_add_entry(pci, entry, &addr);
    if (err) {
        ZF_LOGE("vmm_pci_add_entry() failed (%d)", err);
        return -1;
    }

    pcidev->dev_id = addr.dev;
    pcidev->backend_pcidev_id = backend_pcidev_id;
    pcidev->io_proxy = io_proxy;

    ZF_LOGI("Registering PCI device %u (remote %u:%u)", pcidev->idx,
            pcidev->backend_id, pcidev->backend_pcidev_index);

    err = fdt_generate_virtio_node(io_proxy->dtb_buf, pcidev->dev_id,
                                   io_proxy->data_base, io_proxy->data_size);
    if (err) {
        ZF_LOGE("fdt_generate_virtio_node() failed (%d)", err);
        return -1;
    }

    return 0;
}

static pcidev_t *pcidev_find(unsigned int backend_id, unsigned int backend_pcidev_id)
{
    for (int i = 1; i < pci_dev_count; i++) {
        if (pci_devs[i].io_proxy->backend_id == backend_id
            && pci_devs[i].backend_pcidev_id == backend_pcidev_id) {
            return &pci_devs[i];
        }
    }
    return NULL;
}

static int pcidev_intx_set(unsigned int backend_id,
                           unsigned int backend_pcidev_id,
                           bool level)
{
    /* pcidev_id must map to irq */
    if (!irq_is_pci(backend_pcidev_id)) {
        ZF_LOGE("Interrupt %u is not a valid PCI device interrupt",
                backend_pcidev_id);
        return -1;
    }

    pcidev_t *pcidev = pcidev_find(backend_id, backend_pcidev_id);
    if (!pcidev) {
        ZF_LOGE("Backend %u does not contain pcidev %u",
                backend_id, backend_pcidev_id);
        return -1;
    }

    return shared_irq_line_change(&pci_intx[pci_map_irq(pcidev)],
                                  pcidev->dev_id, level);
}

static int handle_pci(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err;

    switch (op) {
    case RPC_MR0_OP_SET_IRQ:
        err = pcidev_intx_set(io_proxy->backend_id, msg->mr1, true);
        break;
    case RPC_MR0_OP_CLR_IRQ:
        err = pcidev_intx_set(io_proxy->backend_id, msg->mr1, false);
        break;
    case RPC_MR0_OP_REGISTER_PCI_DEV:
        err = pcidev_register(pci, io_proxy, msg->mr1);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    if (err) {
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
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

    int err = ioreq_start(io_proxy, vcpu, AS_GLOBAL, dir, paddr, len, value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return FAULT_ERROR;
    }

    /* ioreq_start() flushes queue from VMM to device, we do not need to */

    return FAULT_HANDLED;
}

static int irq_init(vm_t *vm)
{
    static bool done = false;

    if (!done) {
        int err;
        /* Initialize INTA-D */
        for (int i = 0; i < PCI_NUM_PINS; i++) {
            err = shared_irq_line_init(&pci_intx[i], vm->vcpus[BOOT_VCPU],
                                       INTERRUPT_PCI_INTX_BASE + i);
            if (err) {
                break;
            }
        }

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

    int err = irq_init(vm);
    if (err) {
        ZF_LOGE("irq_init() failed");
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
