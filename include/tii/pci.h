/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>

#define PCI_DEVFN(slot, func)  ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)        (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)        ((devfn) & 0x07)

#define PCI_NUM_SLOTS   (32)
#define PCI_NUM_PINS    (4)
/* Bridge consumes one slot */
#define PCI_NUM_AVAIL_DEVICES   (PCI_NUM_SLOTS - 1)

typedef struct io_proxy io_proxy_t;

typedef struct pcidev {
    uint32_t devfn;
    uint32_t backend_devfn;
    io_proxy_t *io_proxy;
} pcidev_t;

extern pcidev_t *pci_devs[PCI_NUM_AVAIL_DEVICES];
extern unsigned int pci_dev_count;
