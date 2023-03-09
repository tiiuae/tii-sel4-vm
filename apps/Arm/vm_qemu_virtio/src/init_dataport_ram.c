/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <vm_qemu_virtio/gen_config.h>

#include <camkes.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/guest_memory_helpers.h>
#include <sel4vm/guest_ram.h>
#include <sel4vmmplatsupport/guest_memory_util.h>
#include <vmlinux.h>

extern const int vmid;

extern dataport_caps_handle_t memdev_handle;

struct dataport_cookie {
    const char *name;
    uintptr_t base;
    size_t size;
    dataport_caps_handle_t *handle;
};

static vm_frame_t dataport_memory_iterator(uintptr_t base, void *cookie)
{
    cspacepath_t return_frame;
    vm_frame_t frame_result = { seL4_CapNull, seL4_NoRights, 0, 0 };
    struct dataport_cookie *dp = cookie;
    /* 2 MB large pages only */
    int sz = seL4_LargePageBits;

    uintptr_t frame_start = ROUND_DOWN(base, BIT(sz));
    if (frame_start < dp->base || frame_start >= (dp->base + dp->size)) {
        ZF_LOGE("Error: Not dataport region");
        return frame_result;
    }

    int page_idx = (frame_start - dp->base) / BIT(sz);
    frame_result.cptr = dataport_get_nth_frame_cap(dp->handle, page_idx);
    frame_result.rights = seL4_AllRights;
    frame_result.vaddr = frame_start;
    frame_result.size_bits = sz;
    return frame_result;
}

static int map_dataport(vm_t *vm, struct dataport_cookie *dp)
{
    if (!dp->base || !dp->size) {
        ZF_LOGE("Dataport %s has invalid configuration (base 0x%"PRIxPTR
                " size %zu)", dp->base, dp->size);
        return -1;
    }

    int err = vm_ram_register_at_custom_iterator(vm, dp->base, dp->size,
                                                 dataport_memory_iterator,
                                                 dp);
    if (err) {
        ZF_LOGF_IF(err, "Mapping dataport %s failed (%d)", dp->name, err);
        return err;
    }

    ZF_LOGI("Mapped dataport %s, %zu bytes @ 0x%"PRIxPTR, dp->name, dp->size,
            dp->base);

    return 0;
}

#if defined(CONFIG_VM_SWIOTLB)
static int map_swiotlb(vm_t *vm)
{
    struct dataport_cookie dp = {
        .name = "swiotlb",
        .base = swiotlb_base,
        .size = swiotlb_size,
        .handle = &memdev_handle,
    };

    if (!swiotlb_base || !swiotlb_size) {
        ZF_LOGI("Skipping mapping swiotlb dataport");
        return 0;
    }

    return map_dataport(vm, &dp);
};
#else
static int map_swiotlb(vm_t *vm)
{
    return 0;
}

static int map_guest_ram(vm_t *vm)
{
    struct dataport_cookie dp = {
        .name = "guest RAM",
        .base = ram_base,
        .size = ram_size,
        .handle = &memdev_handle,
    };

    return map_dataport(vm, &dp);
};
#endif

void init_dataport_ram_module(vm_t *vm, void *cookie)
{
    int err;

    if (vmid == 0 || config_set(CONFIG_VM_SWIOTLB)) {
        err = vm_ram_register_at(vm, ram_base, ram_size, map_one_to_one);
        if (err) {
            ZF_LOGF("Guest RAM mapping failed (%d)", err);
            return;
        }
        ZF_LOGI("Guest RAM mapped from untyped memory");
    }

    map_swiotlb(vm);

#if !defined(CONFIG_VM_SWIOTLB)
    if (vmid != 0) {
        err = map_guest_ram(vm);
        if (err) {
            ZF_LOGF("Guest RAM mapping failed (%d)", err);
            return;
        }
        ZF_LOGI("Guest RAM mapped from dataport");
    }
#endif
}

DEFINE_MODULE(init_dataport_ram, NULL, init_dataport_ram_module)
