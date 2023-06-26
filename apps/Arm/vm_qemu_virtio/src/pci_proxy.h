/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sel4vmmplatsupport/drivers/pci.h>

#include "io_proxy.h"

typedef struct pci_proxy pci_proxy_t;

pci_proxy_t *pci_proxy_init(io_proxy_t *io_proxy, vmm_pci_space_t *pci, int idx);
