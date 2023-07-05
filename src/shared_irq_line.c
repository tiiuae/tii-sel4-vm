/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_irq_controller.h>

#include <tii/shared_irq_line.h>

static bool shared_irq_line_resample(void *cookie)
{
    shared_irq_line_t *line = cookie;

    return line->sources;
}

int shared_irq_line_init(shared_irq_line_t *line, vm_vcpu_t *vcpu,
                         unsigned int irq)
{
    line->sources = 0;

    return level_irq_init(&line->irq, vcpu, irq, shared_irq_line_resample,
                          line);
}

int shared_irq_line_change(shared_irq_line_t *line, unsigned int source,
                           bool active)
{
    if (source >= 64) {
        ZF_LOGE("Source index %u >= 64", source);
        return -1;
    }

    unsigned int saved_sources = line->sources;

    if (active) {
        line->sources |= 1 << source;
    } else {
        line->sources &= ~(1 << source);
    }

    if (!!saved_sources == !!line->sources) {
        /* no changes on wired-OR signal */
        return 0;
    }

    return level_irq_resample(&line->irq);
}
