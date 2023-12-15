/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tii/reservations.h>
#include <tii/list.h>

static list_t irqs;

static int irq_line_cmp(void *l, void *r)
{
    irq_line_t *a = l;
    irq_line_t *b = r;

    return !(a->irq == b->irq && a->cookie == b->cookie);
}

irq_line_t *irq_res_find(io_proxy_t *io_proxy, uint32_t irq)
{
    return list_item(&irqs,
                     &(irq_line_t) { .irq = irq, .cookie = io_proxy},
                     &irq_line_cmp);
}

int irq_res_assign(io_proxy_t *io_proxy, vm_vcpu_t *vcpu, uint32_t irq)
{
    irq_line_t *irq_line = calloc(1, sizeof(irq_line_t));
    if (!irq_line) {
        ZF_LOGE("Failed to allocate object for irq %u", irq);
        return -1;
    }

    int err = irq_line_init(irq_line, vcpu, irq, io_proxy);
    if (err) {
        ZF_LOGE("Failed to register IRQ %d (%d)", irq, err);
        free(irq_line);
        return err;
    }

    err = list_append(&irqs, irq_line);
    if (err) {
        ZF_LOGE("Failed to add IRQ %d to list", irq);
        free(irq_line);
    }

    return err;
}

int irq_res_free(io_proxy_t *io_proxy, uint32_t irq)
{
    irq_line_t *res = irq_res_find(io_proxy, irq);
    if (!res) {
        ZF_LOGE("Failed to find IRQ %u for backend %p", irq, io_proxy);
        return -1;
    }

    int err = list_remove(&irqs, &res, &irq_line_cmp);
    ZF_LOGE_IF(err, "list_remove() failed");

    /* there's no API for deregistering IRQ, so just free... */
    free(res);

    return 0;
}

int irq_res_init(void)
{
    return list_init(&irqs);
}
