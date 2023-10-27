/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>
#include <stdbool.h>

#include <sel4/sel4.h>

/***
 * @module shared_irq_line.h
 * This module emulates the level-triggered interrupt line. Such a line can be
 * shared among multiple sources. This software emulation supports up to 64
 * different sources wire-ORed together to a single interrupt line.
 */

typedef struct shared_irq_line {
    void *vcpu_cookie;
    unsigned int irq;
    uint64_t sources;
} shared_irq_line_t;

/***
 * @function shared_irq_line_init(line, vcpu, irq)
 * Initialize shared IRQ line emulation object.
 * @param {share_irq_line_t *} line     Pointer to shared IRQ line object
 * @param {void *} vcpu_cookie          vCPU to which IRQ will be injected
 * @param {unsigned int} irq            IRQ number that will be injected
 * @return                              Zero on success, non-zero on failure
 */
int shared_irq_line_init(shared_irq_line_t *line, void *vcpu_cookie,
                         unsigned int irq);

/***
 * @function shared_irq_line_change(line, source, active)
 * @param {share_irq_line_t *} line     Pointer to shared IRQ line object
 * @param {unsigned int} source         Index of source changing the level
 * @param {bool} active                 New logic level
 * @return                              Zero on success, non-zero on failure
 */
int shared_irq_line_change(shared_irq_line_t *line, unsigned int source,
                           bool active);
