/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/***
 * @module irq_line.h
 * Emulates emulates interrupt line that can be edge- or level-triggered. The
 * trigger semantics are configured by the Guest OS.
 */

typedef struct irq_line {
    vm_vcpu_t *vcpu;
    unsigned int irq;
    void *cookie;
} irq_line_t;

/***
 * @function irq_line_init(line, vcpu, irq)
 * Initialize IRQ line emulation object.
 * @param {irq_line_t *} line           Pointer to IRQ line object
 * @param {vm_vcpu_t *} vcpu            vCPU to which IRQ will be injected
 * @param {unsigned int} irq            IRQ number that will be injected
 * @param {void *} cookie               User data
 * @return                              Zero on success, non-zero on failure
 */
int irq_line_init(irq_line_t *line, vm_vcpu_t *vcpu, unsigned int irq,
                  void *cookie);

/***
 * @function irq_line_change(line, active)
 * @param {irq_line_t *} line           Pointer to IRQ line object
 * @param {bool} active                 New logic level
 * @return                              Zero on success, non-zero on failure
 */
int irq_line_change(irq_line_t *line, bool active);

/***
 * @function irq_line_pulse(line)
 * @param {irq_line_t *} line           Pointer to IRQ line object
 * @return                              Zero on success, non-zero on failure
 */
int irq_line_pulse(irq_line_t *line);
