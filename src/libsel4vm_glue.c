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

#include <tii/fdt.h>
#include <tii/shared_irq_line.h>

#include <tii/trace.h>
#include <tii/io_proxy.h>
#include <tii/pci.h>
#include <tii/emulated_device.h>

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

#define INTERRUPT_PCI_INTX_BASE (VIRTIO_CON_PLAT_INTERRUPT_LINE)

typedef int (*rpc_callback_fn_t)(io_proxy_t *io_proxy, unsigned int op,
                                 rpcmsg_t *msg);

extern vka_t _vka;

/************************** PCI externs begin here **************************/

extern vmm_pci_space_t *pci;

/*************************** PCI externs end here ***************************/

/*********************** main declarations begin here ***********************/

static shared_irq_line_t pci_intx[PCI_NUM_PINS];

static int ioack_vcpu_read(seL4_Word data, void *cookie);
static int ioack_vcpu_write(seL4_Word data, void *cookie);

/************************ main declarations end here ************************/

/*********************** PCI declarations begin here ************************/

pcidev_t *pci_devs[PCI_NUM_AVAIL_DEVICES];
unsigned int pci_dev_count;

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
    return pci_swizzle(PCI_SLOT(pcidev->devfn), 0);
}

static inline uint64_t pci_cfg_start(pcidev_t *pcidev, unsigned int dir,
                                     uintptr_t offset, size_t size,
                                     uint64_t value)
{
    unsigned int backend_slot = PCI_SLOT(pcidev->backend_devfn);

    int err = ioreq_native(pcidev->io_proxy, AS_PCIDEV(backend_slot),
                           dir, offset, size, &value);
    if (err) {
        ZF_LOGE("ioreq_native() failed (%d)", err);
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
                           uint32_t backend_devfn)
{
    if (pci_dev_count >= PCI_NUM_AVAIL_DEVICES) {
        ZF_LOGE("PCI device register failed: bus full");
        return -1;
    }

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
    vmm_pci_address_t addr;
    int err = vmm_pci_add_entry(pci, entry, &addr);
    if (err) {
        ZF_LOGE("vmm_pci_add_entry() failed (%d)", err);
        return -1;
    }

    pcidev->devfn = PCI_DEVFN(addr.dev, addr.fun);
    pcidev->backend_devfn = backend_devfn;
    pcidev->io_proxy = io_proxy;

    ZF_LOGI("Registering PCI devfn 0x%"PRIx32" (backend %p devfn 0x%"PRIx32")",
        pcidev->devfn, io_proxy, pcidev->backend_devfn);

    pci_devs[pci_dev_count++] = pcidev;

    return 0;
}

static pcidev_t *pcidev_find(io_proxy_t *io_proxy, uint32_t backend_devfn)
{
    for (int i = 0; i < pci_dev_count; i++) {
        if (pci_devs[i]->io_proxy == io_proxy
            && pci_devs[i]->backend_devfn == backend_devfn) {
            return pci_devs[i];
        }
    }
    return NULL;
}

static int pcidev_intx_set(io_proxy_t *io_proxy, uint32_t backend_devfn,
                           bool level)
{
    /* slot must map to irq */
    unsigned int irq = PCI_SLOT(backend_devfn);
    if (!irq_is_pci(irq)) {
        ZF_LOGE("Interrupt %u is not a valid PCI device interrupt", irq);
        return -1;
    }

    pcidev_t *pcidev = pcidev_find(io_proxy, backend_devfn);
    if (!pcidev) {
        ZF_LOGE("Backend %p does not contain PCI devfn 0x%"PRIx32,
                io_proxy, backend_devfn);
        return -1;
    }

    return shared_irq_line_change(&pci_intx[pci_map_irq(pcidev)],
                                  PCI_SLOT(pcidev->devfn), level);
}

static int handle_pci(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err;

    switch (op) {
    case QEMU_OP_SET_IRQ:
        if (!irq_is_pci(msg->mr1))
            return RPCMSG_RC_NONE;

        err = pcidev_intx_set(io_proxy, PCI_DEVFN(msg->mr1, 0), true);
        break;
    case QEMU_OP_CLR_IRQ:
        if (!irq_is_pci(msg->mr1))
            return RPCMSG_RC_NONE;

        err = pcidev_intx_set(io_proxy, PCI_DEVFN(msg->mr1, 0), false);
        break;
    case QEMU_OP_REGISTER_PCI_DEV:
        err = pcidev_register(pci, io_proxy, PCI_DEVFN(msg->mr1, 0));
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
    case QEMU_OP_START_VM:
        io_proxy->ok_to_run = 1;
        sync_sem_post(&io_proxy->backend_started);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    return RPCMSG_RC_HANDLED;
}

static rpc_callback_fn_t rpc_callbacks[] = {
    handle_mmio,
    handle_pci,
    handle_emudev,
    handle_control,
    NULL,
};

int rpc_run(io_proxy_t *io_proxy)
{
    rpcmsg_t *msg;

    rpcmsg_queue_iterate(io_proxy->rpc.rx_queue, msg) {
        unsigned int op = QEMU_OP(msg->mr0);

        int rc = RPCMSG_RC_NONE;
        for (rpc_callback_fn_t *cb = rpc_callbacks;
             rc == RPCMSG_RC_NONE && *cb; cb++) {
            rc = (*cb)(io_proxy, op, msg);
        }

        if (rc == RPCMSG_RC_ERROR) {
            return -1;
        }

        if (rc == RPCMSG_RC_NONE) {
            ZF_LOGE("Unknown RPC message %u", op);
            return -1;
        }
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
    uint64_t value = 0;

    if (!is_vcpu_read_fault(vcpu)) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        uint64_t mask = get_vcpu_fault_data_mask(vcpu) >> s;
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

    /* Let's not advance the fault here -- the reply from QEMU does that */
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

    err = emudev_init(vm, mmio_fault_handler);
    if (err) {
        ZF_LOGE("emudev_init() failed");
        return -1;
    }

    err = io_proxy_run(io_proxy);
    if (err) {
        ZF_LOGE("io_proxy_run() failed");
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
