/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include "io_proxy.h"
#include "pci_proxy.h"

typedef struct pci_proxy {
    unsigned int idx;
    struct io_proxy *io_proxy;
} pci_proxy_t;

#define VCPU_NONE NULL

static inline uint32_t pci_proxy_start(pci_proxy_t *dev, vm_vcpu_t *vcpu,
                                       unsigned int dir, uintptr_t offset,
                                       size_t size, uint32_t value)
{
    io_proxy_t *io_proxy = dev->io_proxy;

    int slot = ioreq_pci_start(io_proxy, vcpu, dev->idx, dir, offset, size, value);
    assert(ioreq_slot_valid(slot));
    
    rpc_notify(io_proxy->rpc);

    uint64_t result;
    int err = ioreq_wait(io_proxy, slot, &result);
    if (err) {
        ZF_LOGE("ioreq_wait() failed");
        return 0;
    }

    /* vCPU != NULL case falls thru, fix it */

    return result;
}

static inline uint32_t pci_proxy_read(void *cookie, vm_vcpu_t *vcpu, unsigned int offset,
                                      size_t size)
{
    pci_proxy_t *dev = cookie;

    return pci_proxy_start(dev, vcpu, SEL4_IO_DIR_READ, offset, size, 0);
}

static inline void pci_proxy_write(void *cookie, vm_vcpu_t *vcpu, unsigned int offset,
                                   size_t size, uint32_t value)
{
    pci_proxy_t *dev = cookie;

    pci_proxy_start(dev, vcpu, SEL4_IO_DIR_WRITE, offset, size, value);
}

static uint8_t pci_proxy_read8(void *cookie, vmm_pci_address_t addr,
                               unsigned int offset)
{
    return pci_proxy_read(cookie, VCPU_NONE, offset, 1);
}

static uint16_t pci_proxy_read16(void *cookie, vmm_pci_address_t addr,
                                 unsigned int offset)
{
    return pci_proxy_read(cookie, VCPU_NONE, offset, 2);
}

static uint32_t pci_proxy_read32(void *cookie, vmm_pci_address_t addr,
                                 unsigned int offset)
{
    return pci_proxy_read(cookie, VCPU_NONE, offset, 4);
}

static void pci_proxy_write8(void *cookie, vmm_pci_address_t addr,
                             unsigned int offset, uint8_t val)
{
    pci_proxy_write(cookie, VCPU_NONE, offset, 1, val);
}

static void pci_proxy_write16(void *cookie, vmm_pci_address_t addr,
                              unsigned int offset, uint16_t val)
{
    pci_proxy_write(cookie, VCPU_NONE, offset, 2, val);
}

static void pci_proxy_write32(void *cookie, vmm_pci_address_t addr,
                              unsigned int offset, uint32_t val)
{
    pci_proxy_write(cookie, VCPU_NONE, offset, 4, val);
}

static vmm_pci_config_t pci_proxy_make_config(pci_proxy_t *dev)
{
    return (vmm_pci_config_t) {
        .cookie = dev,
        .ioread8 = pci_proxy_read8,
        .ioread16 = pci_proxy_read16,
        .ioread32 = pci_proxy_read32,
        .iowrite8 = pci_proxy_write8,
        .iowrite16 = pci_proxy_write16,
        .iowrite32 = pci_proxy_write32,
    };
}

static pci_proxy_t *pci_proxy_init(io_proxy_t *io_proxy, vmm_pci_space_t *pci,
                                   int idx)
{
    pci_proxy_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        ZF_LOGE("Failed to allocate memory");
        return NULL;
    }

    dev->io_proxy = io_proxy;
    dev->idx = idx;

    vmm_pci_address_t bogus_addr = {
        .bus = 0,
        .dev = 0,
        .fun = 0,
    };
    vmm_pci_entry_t entry = vmm_pci_create_passthrough(bogus_addr,
                                                       pci_proxy_make_config(dev));

   /* TODO: add IRQ faker */

    vmm_pci_add_entry(pci, entry, NULL);

    return dev;
}
