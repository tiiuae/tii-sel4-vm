/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, 2023, Technology Innovation Institute
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

extern uintptr_t linux_ram_base;
extern size_t linux_ram_size;

extern const int vmid;

extern dataport_caps_handle_t memdev_handle;

struct dataport_cookie {
    const char *name;
    uintptr_t gpa;
    size_t size;
    dataport_caps_handle_t *handle;
};

static vm_frame_t dataport_memory_iterator(uintptr_t gpa, void *cookie)
{
    cspacepath_t return_frame;
    vm_frame_t frame_result = { seL4_CapNull, seL4_NoRights, 0, 0 };
    struct dataport_cookie *dp = cookie;
    /* 2 MB large pages only */
    int sz = seL4_LargePageBits;

    uintptr_t frame_start = ROUND_DOWN(gpa, BIT(sz));
    if (frame_start < dp->gpa || frame_start >= (dp->gpa + dp->size)) {
        ZF_LOGE("Error: Not dataport region");
        return frame_result;
    }

    int page_idx = (frame_start - dp->gpa) / BIT(sz);
    frame_result.cptr = dataport_get_nth_frame_cap(dp->handle, page_idx);
    frame_result.rights = seL4_AllRights;
    frame_result.vaddr = frame_start;
    frame_result.size_bits = sz;
    return frame_result;
}

static void map_dataport(vm_t *vm, struct dataport_cookie *dp)
{
    ZF_LOGF_IF(!dp->gpa, "dataport \"%s\" has zero GPA", dp->name);
    ZF_LOGF_IF(!dp->size, "dataport \"%s\" has zero size", dp->name);
    ZF_LOGI("mapping dataport \"%s\" to GPA range 0x%"PRIxPTR"-0x%"PRIxPTR,
            dp->name, dp->gpa, dp->gpa + dp->size - 1);

    int err = vm_ram_register_at_custom_iterator(vm, dp->gpa, dp->size,
                                                 dataport_memory_iterator,
                                                 dp);
    ZF_LOGF_IF(err, "mapping dataport \"%s\" failed (%d)", dp->name, err);
}

static void map_guest_ram(vm_t *vm)
{
    struct dataport_cookie dp = {
        .name = "guest RAM",
        .gpa = linux_ram_base,
        .size = linux_ram_size,
        .handle = &memdev_handle,
    };

    map_dataport(vm, &dp);
};

static void do_init_ram_module(vm_t *vm, void *cookie)
{
    int err;

    if (config_set(CONFIG_PLAT_EXYNOS5) || config_set(CONFIG_PLAT_QEMU_ARM_VIRT) || config_set(CONFIG_PLAT_TX2)) {
        err = vm_ram_register_at(vm, linux_ram_base, linux_ram_size, true);
    } else {
        err = vm_ram_register_at(vm, linux_ram_base, linux_ram_size, false);
    }
    assert(!err);
}

void init_dataport_ram_module(vm_t *vm, void *cookie)
{
    if (vmid == 0) {
        ZF_LOGI("mapping guest RAM from untyped memory");
        do_init_ram_module(vm, cookie);
    }

    if (vmid != 0) {
        ZF_LOGI("mapping guest RAM from dataport");
        map_guest_ram(vm);
    }
}

DEFINE_MODULE(init_dataport_ram, NULL, init_dataport_ram_module)
