/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Simple pl011 emulation for early guest debugging. To enable, add
 * "earlycon=pl011,mmio32,0x09000000" to kernel_bootcmdline.
 */

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <utils/util.h>

#include <vmlinux.h>

typedef struct pl011 {
    uintptr_t base;
    size_t size;
} pl011_t;

#define PL011_UARTDR    0x00    /* UARTDR: uart data register */
#define PL011_UARTFR    0x18    /* UARTFR: uart flag register */

static inline bool pl011_read_fault(pl011_t *p, vm_vcpu_t *vcpu,
                                    uintptr_t paddr, size_t len)
{
    switch (paddr - p->base) {
    case PL011_UARTFR:
        /* transmitter empty and transmitter-hold-register empty */
        set_vcpu_fault_data(vcpu, 0x90);
        break;
    default:
        ZF_LOGW("unhandled read: vcpu=%d addr=0x%"PRIxPTR" len=%zu",
                vcpu->vcpu_id, paddr, len);
        return false;
    }

    return true;
}

static inline bool pl011_write_fault(pl011_t *p, vm_vcpu_t *vcpu,
                                     uintptr_t paddr, size_t len)
{
    seL4_Word value = emulate_vcpu_fault(vcpu, 0);

    switch (paddr - p->base) {
    case PL011_UARTDR:
        putchar((int) value);
        break;
    default:
        ZF_LOGW("unhandled write: vcpu=%d addr=0x%"PRIxPTR" len=%zu value=%08",
                vcpu->vcpu_id, paddr, len, value);
        return false;
    }

    return true;
}

static memory_fault_result_t pl011_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                 uintptr_t paddr, size_t len,
                                                 void *cookie)
{
    bool fault_handled;
    pl011_t *p = cookie;

    if (is_vcpu_read_fault(vcpu)) {
        fault_handled = pl011_read_fault(p, vcpu, paddr, len);
    } else {
        fault_handled = pl011_write_fault(p, vcpu, paddr, len);
    }

    if (!fault_handled) {
        return FAULT_UNHANDLED;
    }

    advance_vcpu_fault(vcpu);

    return FAULT_HANDLED;
}

static void pl011_init(vm_t *vm, void *cookie)
{
    vm_memory_reservation_t *res;
    pl011_t *p = cookie;

    res = vm_reserve_memory_at(vm, p->base, p->size, pl011_fault_handler,
                               cookie);
    ZF_LOGF_IF(!res, "Cannot reserve range 0x%"PRIxPTR" - 0x%"PRIxPTR,
               p->base, p->base - 1 + p->size);
}

static pl011_t pl011 = {
    .base = 0x09000000,
    .size = BIT(PAGE_BITS_4K),
};

DEFINE_MODULE(pl011, &pl011, pl011_init)
