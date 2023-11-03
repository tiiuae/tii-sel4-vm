/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <sel4vm/guest_memory.h>

#include <ioreq.h>
#include <tii/libsel4vm.h>

static int ioack_vcpu_read(seL4_Word data, void *cookie);
static int ioack_vcpu_write(seL4_Word data, void *cookie);

static int ioack_vcpu_read(seL4_Word data, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    set_vcpu_fault_data(vcpu, data << s);
    advance_vcpu_fault(vcpu);

    return 0;
}

static int ioack_vcpu_write(seL4_Word data, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    advance_vcpu_fault(vcpu);

    return 0;
}

static memory_fault_result_t mmio_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie)
{
    io_proxy_t *io_proxy = cookie;

    unsigned int dir = SEL4_IO_DIR_READ;
    seL4_Word value = 0;

    if (!is_vcpu_read_fault(vcpu)) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word mask = get_vcpu_fault_data_mask(vcpu) >> s;
        value = get_vcpu_fault_data(vcpu) & mask;
        dir = SEL4_IO_DIR_WRITE;
    }

    int err = ioreq_start(io_proxy, vcpu->vcpu_id, ioack_vcpu_read,
                          ioack_vcpu_write, vcpu, AS_GLOBAL, dir, paddr, len,
                          value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return FAULT_ERROR;
    }

    /* ioreq_start() flushes queue from VMM to device, we do not need to */

    return FAULT_HANDLED;
}

static int libsel4vm_fault_handler_install(io_proxy_t *io_proxy,
                                           uintptr_t addr, size_t size)
{
    vm_t *vm = io_proxy->fault_handler_cookie;

    vm_memory_reservation_t *reservation;

    reservation = vm_reserve_memory_at(vm, addr, size, mmio_fault_handler,
                                       io_proxy);
    if (!reservation) {
        ZF_LOGE("vm_reserve_memory_at() failed");
        return -1;
    }

    return 0;
}

int libsel4vm_fault_handler_setup(io_proxy_t *io_proxy, vm_t *vm)
{
    io_proxy->fault_handler_install = libsel4vm_fault_handler_install;
    io_proxy->fault_handler_cookie = vm;

    return 0;
}
