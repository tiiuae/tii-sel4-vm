/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_irq_controller.h>

#include <tii/shared_irq_line.h>

static void shared_irq_ack(vm_vcpu_t *vcpu, int irq, void *cookie)
{
}

int shared_irq_line_init(shared_irq_line_t *line, void *vcpu_cookie,
                         unsigned int irq)
{
    vm_vcpu_t *vcpu = vcpu_cookie;

    line->vcpu_cookie = vcpu;
    line->irq = irq;
    line->sources = 0;

    int err = vm_register_irq(vcpu, irq, shared_irq_ack, line);
    if (err) {
        ZF_LOGE("Failed to register IRQ %d (%d)", irq, err);
        return -1;
    }

    return 0;
}

int shared_irq_line_change(shared_irq_line_t *line, unsigned int source,
                           bool active)
{
    if (source >= 64) {
        ZF_LOGE("Source index %u >= 64", source);
        return -1;
    }

    uint64_t saved_sources = line->sources;

    if (active) {
        line->sources |= 1 << source;
    } else {
        line->sources &= ~(1 << source);
    }

    if (!!saved_sources == !!line->sources) {
        /* no changes on wired-OR signal */
        return 0;
    }

    return vm_set_irq_level((vm_vcpu_t *)line->vcpu_cookie, line->irq,
                            !!line->sources);
}
