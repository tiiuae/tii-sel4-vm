/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_irq_controller.h>

#include "pci_intx.h"

typedef struct intx {
    unsigned int irq;
    unsigned int pins;
    vm_vcpu_t *vcpu;
} intx_t;

static int intx_resample(intx_t *intx)
{
    if (!intx) {
        ZF_LOGE("null intx");
        return -1;
    }

    if (!intx->pins) {
           return 0;
    }

    int err = vm_inject_irq(intx->vcpu, intx->irq);
    if (err) {
        ZF_LOGE("vm_inject_irq() failed for IRQ %d (%d)", intx->irq, err);
    }

    return err;
}

static void intx_ack(vm_vcpu_t *vcpu, int irq, void *token)
{
    intx_t *intx = token;

    if (!vcpu || !intx || vcpu != intx->vcpu || irq != intx->irq) {
        ZF_LOGE("invalid arguments");
        return;
    }

    int err = intx_resample(intx);
    if (err) {
        ZF_LOGE("intx_resample() failed (%d)", err);
    }
}

intx_t *intx_init(vm_vcpu_t *vcpu, unsigned int irq)
{
    intx_t *intx = calloc(1, sizeof(*intx));
    if (!intx) {
        ZF_LOGE("Failed to allocate memory");
        return NULL;
    }

    intx->irq = irq;
    intx->vcpu = vcpu;
    intx->pins = 0;

    int err = vm_register_irq(vcpu, irq, intx_ack, intx);
    if (err) {
        ZF_LOGE("Failed to register IRQ %d (%d)", irq, err);
        return NULL;
    }

    return intx;
}

int intx_change_level(intx_t *intx, unsigned int dev, bool active)
{
    /* assuming byte size is 8 bits */
    if (dev >= sizeof(intx->pins) * 8) {
        ZF_LOGE("Device index %u too large", dev);
        return -1;
    }

    unsigned int saved_pins = intx->pins;

    if (active) {
        intx->pins |= 1 << dev;
    } else {
        intx->pins &= ~(1 << dev);
    }

    if (!!saved_pins == !!intx->pins) {
        /* no changes on wired-OR signal */
        return 0;
    }

    return intx_resample(intx);
}
