/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI interrupt extension emulation for GICv2.
 */

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <sel4vm/boot.h>
#include <utils/util.h>

#include <tii/gicv2m.h>
#include <tii/irq_line.h>

#define V2M_MSI_TYPER           0x008
#define V2M_MSI_SETSPI_NS       0x040
#define V2M_MSI_IIDR            0xFCC
#define V2M_IIDR0               0xFD0
#define V2M_IIDR11              0xFFC

#define VM2_PRODUCT_ID          0x53 /* ASCII code S */

static bool v2m_read_fault(gicv2m_t *s,
                           uintptr_t paddr,
                           size_t len,
                           uint32_t *data)
{
    ZF_LOGF_IF(!data, "data pointer NULL");

    if (len != 4) {
        ZF_LOGW("invalid read size: %zu", len);
        return false;
    }

    switch (paddr - s->base) {
    case V2M_MSI_TYPER: {
        uint32_t val = (s->irq_base) << 16;
        val |= s->num_irq;
        /* transmitter empty and transmitter-hold-register empty */
        *data = val;
        break;
    }
    case V2M_MSI_IIDR:
        *data = 0x53 << 20;
        break;
    case V2M_IIDR0 ... V2M_IIDR11:
        *data = 0;
        break;
    default:
        ZF_LOGW("unhandled read: addr=0x%"PRIxPTR" len=%zu",
                paddr, len);
        return false;
    }

    return true;
}

static bool v2m_write_fault(gicv2m_t *s,
                            uintptr_t paddr,
                            size_t len,
                            uint32_t data)
{
    if (len != 2 && len != 4) {
        ZF_LOGW("invalid read size: len=%zu", len);
        return false;
    }

    switch (paddr - s->base) {
    case V2M_MSI_SETSPI_NS: {
        uint32_t spi;

        spi = (data & 0x3ff) - s->irq_base;
        if (spi >= 0 && spi < s->num_irq) {
            int err = irq_line_pulse(&s->irq[spi]);
            if (err) {
                ZF_LOGE("pulsing irq line %lu failed (%d)", spi, err);
                return false;
            }
        }
        break;
    }
    default:
        ZF_LOGW("unhandled write: addr=0x%"PRIxPTR" len=%zu data=%08",
                paddr, len, data);
        return false;
    }

    return true;
}

static memory_fault_result_t v2m_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                               uintptr_t paddr, size_t len,
                                               void *cookie)
{
    bool fault_handled;
    gicv2m_t *s = cookie;
    uint32_t val = 0;

    if (is_vcpu_read_fault(vcpu)) {
        fault_handled = v2m_read_fault(s, paddr, len, &val);
        set_vcpu_fault_data(vcpu, val);
    } else {
        val = emulate_vcpu_fault(vcpu, 0);
        fault_handled = v2m_write_fault(s, paddr, len, val);
    }

    if (!fault_handled) {
        return FAULT_UNHANDLED;
    }

    advance_vcpu_fault(vcpu);

    return FAULT_HANDLED;
}

bool v2m_irq_valid(gicv2m_t *s, uint32_t irq)
{
    return irq >= s->irq_base && irq < (s->irq_base + s->num_irq);
}

int v2m_inject_irq(gicv2m_t *s, uint32_t irq)
{
    assert(s);

    if (!v2m_irq_valid(s, irq)) {
        return -1;
    }
    return irq_line_pulse(&s->irq[irq - s->irq_base]);
}

int v2m_init(gicv2m_t *s, vm_t *vm)
{
    vm_memory_reservation_t *res;

    assert(s);
    assert(vm);

    if (s->num_irq > GICV2M_IRQ_MAX) {
        ZF_LOGE("num_irq (%lu) exceeds max (%lu)", s->num_irq, GICV2M_IRQ_MAX);
        return -1;
    }

    if (s->irq_base + s->num_irq > 1020) {
        ZF_LOGE("irq range (%lu:%lu) exceeds max (1020)", s->irq_base,
                s->irq_base + s->num_irq);
        return -1;
    }

    int err = -1;
    for (uint32_t i = 0; i < s->num_irq; i++) {
        uint32_t irq = s->irq_base + i;

        err =  irq_line_init(&s->irq[i], vm->vcpus[BOOT_VCPU], irq, s);
        if (err) {
            ZF_LOGE("interrupt %lu initialization failed (%d)", irq, err);
            return err;
        }
    }

    res = vm_reserve_memory_at(vm, s->base, s->size, v2m_fault_handler, s);
    if (!res) {
        ZF_LOGE("Cannot reserve range 0x%"PRIxPTR" - 0x%"PRIxPTR,
                s->base, s->base - 1 + s->size);
        return -1;
    }

    return 0;
}
