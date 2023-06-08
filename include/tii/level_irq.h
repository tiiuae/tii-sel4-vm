/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/***
 * @module level_irq.h
 * This module emulates the level-triggered IRQ.
 */

typedef struct level_irq {
    vm_vcpu_t *vcpu;
    unsigned int irq;
    bool (*resample)(void *cookie);
    void *resample_cookie;
} level_irq_t;

/***
 * @function level_irq_init(vcpu, irq, resample, resample_cookie)
 * Initialize the level-triggered IRQ emulation object.
 * @param {level_irq_t *} level_irq     Pointer to level IRQ emulation object
 * @param {vm_vcpu_t *} vcpu            vCPU to which IRQ will be injected
 * @param {unsigned int} irq            IRQ number that will be injected
 * @param {bool (*)(void *) resample    Resample callback
 * @param {void} resample_cookie        Cookie given to resample callback
 * @return                              Zero on success, non-zero on failure
 */
int level_irq_init(level_irq_t *level_irq, vm_vcpu_t *vcpu, unsigned int irq,
                   bool (*resample)(void *cookie), void *resample_cookie);

/***
 * @function level_irq_resample(level_irq)
 * Resamples the IRQ conditions and reinjects IRQ to VM if needed.
 * @param {level_irq_t *} level_irq     Pointer to IRQ pins object
 * @return                              Zero on success, non-zero on failure
 */
int level_irq_resample(level_irq_t *level_irq);
