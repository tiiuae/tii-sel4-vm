/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simple pl011 mmio emulation for early guest debugging. Enable by
 * adding "earlycon=pl011,mmio32,0x09000000" to driver-vm kernel arguments.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <sel4/sel4.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <utils/zf_log.h>

typedef struct pl011 {
    uintptr_t base;
    size_t size;
} pl011_t;

#define PL011_UARTDR    0x00
#define PL011_UARTFR    0x18

static inline void pl011_read_fault(pl011_t *p, vm_vcpu_t *vcpu,
                                    uintptr_t paddr, size_t len)
{
    switch (paddr - p->base) {
    /* UARTFR: uart flag register */
    case PL011_UARTFR:
        /* transmitter empty and transmitter-hold-register empty */
        set_vcpu_fault_data(vcpu, 0x90);
        break;
    default:
        ZF_LOGE("unhandled read: vcpu=%d addr=%"PRIx64" len=%zu",
                vcpu->vcpu_id, paddr, len);
        break;
    }
}

static inline void pl011_write_fault(pl011_t *p, vm_vcpu_t *vcpu,
                                     uintptr_t paddr, size_t len)
{
    seL4_Word value = emulate_vcpu_fault(vcpu, 0);

    switch (paddr - p->base) {
    /* UARTDR: uart data register */
    case PL011_UARTDR:
        putchar((int) value);
        break;
    default:
        ZF_LOGE("unhandled write: vcpu=%d addr=%"PRIx64" len=%zu value=%08",
                vcpu->vcpu_id, paddr, len, value);
        break;
    }
}

static inline void pl011_handle_fault(pl011_t *p, vm_vcpu_t *vcpu,
                                      uintptr_t paddr, size_t len)
{
    if (is_vcpu_read_fault(vcpu)) {
        pl011_read_fault(p, vcpu, paddr, len);
    } else {
        pl011_write_fault(p, vcpu, paddr, len);
    }
    advance_vcpu_fault(vcpu);
}


static memory_fault_result_t pl011_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                 uintptr_t paddr, size_t len,
                                                 void *cookie)
{
    pl011_t *p = cookie;

    if (paddr < base ||
        paddr - base >= p->size) {
        return FAULT_UNHANDLED;
    }

    pl011_handle_fault(p, vcpu, paddr, len);

    return FAULT_HANDLED;
}

static void pl011_init(vm_t *vm, void *cookie)
{
    vm_memory_reservation_t *res;
    pl011_t *p = cookie;

    res = vm_reserve_memory_at(vm, p->base, p->size, pl011_fault_handler,
                               cookie);
    ZF_LOGF_IF(!res, "Cannot reserve address range for pl011 emulation");
}

static pl011_t pl011_0x09000000 = {
    .base = 0x09000000,
    .size = BITS(PAGE_BITS_4K),
};

DEFINE_MODULE(pl011, &pl011_0x09000000, pl011_init)
