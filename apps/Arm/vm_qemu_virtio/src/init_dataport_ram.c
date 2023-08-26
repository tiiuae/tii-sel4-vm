/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <camkes.h>

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_ram.h>
#include <vmlinux.h>

#include <tii/ram_dataport.h>

/* TODO: add proper definition to libsel4vm's include/sel4vm/guest_ram.h */
extern bool is_ram_region(vm_t *vm, uintptr_t addr, size_t size);

void init_ram_module(vm_t *vm, void *cookie)
{
    int err;

    err = ram_dataport_map_all(vm);
    if (err) {
        ZF_LOGF("ram_dataport_map_all() failed: %d", err);
        /* no return */
    }

    if (is_ram_region(vm, vm_config.ram.base, vm_config.ram.size)) {
        ZF_LOGI("Guest RAM mapped from dataport");
        return;
    }

    err = vm_ram_register_at(vm, vm_config.ram.base, vm_config.ram.size,
                             vm_config.map_one_to_one);
    if (err) {
        ZF_LOGF("vm_ram_register_at() failed: %d", err);
        /* no return */
    }

    if (vm_config.map_one_to_one) {
        ZF_LOGI("Guest RAM mapped from untyped memory (unity stage-2 mapping)");
    } else {
        ZF_LOGI("Guest RAM mapped from allocator pool (NO unity stage-2 mapping)");
    }
}
