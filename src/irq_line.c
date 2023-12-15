/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_irq_controller.h>

#include <tii/irq_line.h>

static void irq_line_ack(vm_vcpu_t *vcpu, int irq, void *cookie)
{
}

int irq_line_init(irq_line_t *line, vm_vcpu_t *vcpu, unsigned int irq,
                  void *cookie)
{
    line->vcpu = vcpu;
    line->irq = irq;
    line->cookie = cookie;

    int err = vm_register_irq(vcpu, irq, irq_line_ack, line);
    if (err) {
        ZF_LOGE("Failed to register IRQ %d (%d)", irq, err);
        return -1;
    }

    return 0;
}

int irq_line_change(irq_line_t *line, bool active)
{
    return vm_set_irq_level(line->vcpu, line->irq, active);
}
