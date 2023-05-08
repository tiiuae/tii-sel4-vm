/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <camkes.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/guest_memory_helpers.h>
#include <sel4vm/guest_ram.h>
#include <sel4vmmplatsupport/guest_memory_util.h>
#include <vmlinux.h>

extern const int vmid;

extern dataport_caps_handle_t memdev_handle;

/* Hack to pass the information to vm_ram_touch() in libsel4vm/src/guest_ram.c
 * without making libsel4vm depend on CAmkES being linked in.
 */
uintptr_t __attribute__((weak)) ram_base(void)
{
    return vm_config.ram.base;
}

size_t __attribute__((weak)) ram_size(void)
{
    return vm_config.ram.size;
}

static vm_frame_t dataport_memory_iterator(uintptr_t addr, void *cookie)
{
    cspacepath_t return_frame;
    vm_frame_t frame_result = { seL4_CapNull, seL4_NoRights, 0, 0 };

    int sz = seL4_PageBits;
    if (addr >= vm_config.ram.base && addr < (vm_config.ram.base + vm_config.ram.size)) {
        sz = seL4_LargePageBits;
    }

    uintptr_t frame_start = ROUND_DOWN(addr, BIT(sz));
    if (frame_start < vm_config.ram.base || frame_start > vm_config.ram.base + vm_config.ram.size) {
        ZF_LOGE("Error: Not dataport ram region");
        return frame_result;
    }

    int page_idx = (frame_start - vm_config.ram.base) / BIT(sz);
    frame_result.cptr = dataport_get_nth_frame_cap(&memdev_handle, page_idx);
    frame_result.rights = seL4_AllRights;
    frame_result.vaddr = frame_start;
    frame_result.size_bits = sz;
    return frame_result;
}

static void original_init_ram_module(vm_t *vm, void *cookie)
{
    int err = vm_ram_register_at(vm, vm_config.ram.base, vm_config.ram.size, vm_config.map_one_to_one);
    assert(!err);
}

void init_ram_module(vm_t *vm, void *cookie)
{
    int err;

    if (vmid == 0) {
        ZF_LOGI("initializing RAM module the old way");
        original_init_ram_module(vm, cookie);
        return;
    }

    ZF_LOGI("initializing RAM module from dataport");

    err = vm_ram_register_at_custom_iterator(vm, vm_config.ram.base, vm_config.ram.size, dataport_memory_iterator, NULL);
    assert(!err);
}
