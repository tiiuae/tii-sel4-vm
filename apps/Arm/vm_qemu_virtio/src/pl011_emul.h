/*
 * Copyright 2022, Technology Innovation Institute
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
#include <sel4vm/guest_memory.h>
#include <utils/zf_log.h>

static inline bool is_pl011_register(uintptr_t paddr)
{
    return (paddr >= 0x09000000 && paddr < 0x09001000);
}

static inline void pl011_read_fault_emul(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    switch (paddr) {
    /* UARTFR: uart flag register */
    case 0x09000018:
        /* transmitter empty and transmitter-hold-register empty */
        set_vcpu_fault_data(vcpu, 0x90);
        break;
    default:
        ZF_LOGE("unhandled read: vcpu=%d addr=%"PRIx64" len=%zu",
                vcpu->vcpu_id, paddr, len);
        break;
    }
}

static inline void pl011_write_fault_emul(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    seL4_Word value = emulate_vcpu_fault(vcpu, 0);

    switch (paddr) {
    /* UARTDR: uart data register */
    case 0x09000000:
        putchar((int) value);
        break;
    default:
        ZF_LOGE("unhandled write: vcpu=%d addr=%"PRIx64" len=%zu value=%08",
                vcpu->vcpu_id, paddr, len, value);
        break;
    }
}

static inline void handle_pl011_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    if (is_vcpu_read_fault(vcpu)) {
        pl011_read_fault_emul(vcpu, paddr, len);
    } else {
        pl011_write_fault_emul(vcpu, paddr, len);
    }
    advance_vcpu_fault(vcpu);
}


static memory_fault_result_t pl011_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                 uintptr_t paddr, size_t len,
                                                 void *cookie)
{
    if (!is_pl011_register(paddr)) {
        return FAULT_UNHANDLED;
    }

    handle_pl011_fault(vcpu, paddr, len);

    return FAULT_HANDLED;
}

