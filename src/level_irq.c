/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_irq_controller.h>

#include <tii/level_irq.h>

int level_irq_resample(level_irq_t *level_irq)
{
    if (!level_irq || !level_irq->resample || !level_irq->vcpu) {
        return -1;
    }

    if (!level_irq->resample(level_irq->resample_cookie)) {
        return 0;
    }

    int err = vm_inject_irq(level_irq->vcpu, level_irq->irq);
    if (err) {
        ZF_LOGE("vm_inject_irq() failed for IRQ %d (%d)", level_irq->irq, err);
    }

    return err;
}

static void level_irq_ack(vm_vcpu_t *vcpu, int irq, void *token)
{
    level_irq_t *level_irq = token;

    if (!level_irq || vcpu != level_irq->vcpu || irq != level_irq->irq) {
        ZF_LOGF("invalid arguments");
    }

    int err = level_irq_resample(level_irq);
    if (err) {
        ZF_LOGF("level_irq_resample() failed (%d)", err);
    }
}

int level_irq_init(level_irq_t *level_irq, vm_vcpu_t *vcpu, unsigned int irq,
                   bool (*resample)(void *cookie), void *resample_cookie)
{
    level_irq->vcpu = vcpu;
    level_irq->irq = irq;
    level_irq->resample = resample;
    level_irq->resample_cookie = resample_cookie;

    int err = vm_register_irq(vcpu, irq, level_irq_ack, level_irq);
    if (err) {
        ZF_LOGE("Failed to register IRQ %d (%d)", irq, err);
        return -1;
    }

    return level_irq_resample(level_irq);
}
