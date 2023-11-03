/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_vm.h>
#include <sel4vm/boot.h>

#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

#include <ioreq.h>
#include <tii/pci.h>
#include <tii/libsel4vm.h>

#define PCI_INTX_IRQ_BASE ((VIRTIO_CON_PLAT_INTERRUPT_LINE) + 1)

/* TODO: this is actually from CAmkES-VM */
extern vmm_pci_space_t *pci;

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

static int pcidev_register(pcidev_t *pcidev, void *cookie)
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

    vmm_pci_space_t *pci = cookie;

    vmm_pci_entry_t entry = vmm_pci_create_passthrough(bogus_addr, config);
    vmm_pci_address_t addr;
    int err = vmm_pci_add_entry(pci, entry, &addr);
    if (err) {
        ZF_LOGE("vmm_pci_add_entry() failed (%d)", err);
        return -1;
    }

    return addr.dev;
}

int libsel4vm_pci_setup(io_proxy_t *io_proxy, vm_t *vm)
{
    io_proxy->pcidev_register = pcidev_register;
    io_proxy->pci_cookie = pci;

    io_proxy->pci_irq_cookie = vm->vcpus[BOOT_VCPU];
    io_proxy->pci_irq_base = PCI_INTX_IRQ_BASE;

    return 0;
}
