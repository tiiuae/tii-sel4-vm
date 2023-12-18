/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI interrupt emulation for GICv2.
 */

#pragma once

#include <tii/irq_line.h>

#define GICV2M_IRQ_MAX          128

typedef struct gicv2m {
    uintptr_t base;
    size_t size;
    irq_line_t irq[GICV2M_IRQ_MAX];
    uint32_t irq_base;
    uint32_t num_irq;
} gicv2m_t;

bool v2m_irq_valid(gicv2m_t *s, uint32_t irq);
int v2m_inject_irq(gicv2m_t *s, uint32_t irq);
int v2m_init(gicv2m_t *s, vm_t *vm);
