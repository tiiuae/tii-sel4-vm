/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>
#include <stddef.h>

#include <sel4/sel4.h>

#define PCI_NUM_SLOTS   (32)
#define PCI_NUM_PINS    (4)

#define PCI_INTA    0
#define PCI_INTB    1
#define PCI_INTC    2
#define PCI_INTD    3

#define PCI_DEVFN(slot, func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))

#define PCI_SLOT(devfn)		(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)		((devfn) & 0x07)

#define PCI_BUS(devfn)		(((devfn) >> 16) & 0xff)
#define PCI_DOMAIN(devfn)       (((devfn) >> 28) & 0x07)
#define PCI_DEVFN_EXT(slot, func, bus, domain) (PCI_DEVFN(slot, func) | (((bus) & 0xff) << 16) | (((domain) & 0x07) << 28))

typedef struct io_proxy io_proxy_t;
typedef struct rpcmsg rpcmsg_t;

typedef struct pcidev {
    unsigned int slot;
    io_proxy_t *io_proxy;
} pcidev_t;

/* Interrupt mapping
 *
 * On ARM 0-31 are used for PPIs and SGIs, we don't allow injecting those.
 * Instead we use that range for virtual PCI devices:
 *   0-31: Virtual PCI devices. Mapped to virtual intx lines.
 *   32-N: SPIs.
 */
static inline int pci_swizzle(int slot, int pin)
{
    return (slot + pin) % PCI_NUM_PINS;
}

static inline int pci_map_irq(pcidev_t *pcidev, unsigned int intx)
{
    return pci_swizzle(pcidev->slot, intx);
}

seL4_Word pci_cfg_ioreq_native(pcidev_t *pcidev, unsigned int dir,
                               uintptr_t offset, size_t size, seL4_Word value);

int handle_pci(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg);

int pci_irq_init(unsigned int irq_base, void *irq_cookie);
