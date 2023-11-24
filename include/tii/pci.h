/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define PCI_DEVFN(slot, func)  ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)        (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)        ((devfn) & 0x07)
