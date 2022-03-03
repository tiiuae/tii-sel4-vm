/*
 * Copyright 2022, SSRC, TII
 *
 */

#include <autoconf.h>
#include <camkes.h>
#include <vmlinux.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_ram.h>

#include <sel4vm/guest_memory_helpers.h>

#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include <signal.h>

#ifdef CONFIG_PLAT_QEMU_ARM_VIRT
#define CONNECTION_BASE_ADDRESS_RING1 0xDF000000
#define CONNECTION_BASE_ADDRESS_RING2 0xDF001000
#else
#define CONNECTION_BASE_ADDRESS_RING1 0x3F000000
#define CONNECTION_BASE_ADDRESS_RING2 0x3F001000
#endif

struct dataport_iterator_cookie {
    seL4_CPtr *dataport_frames;
    uintptr_t dataport_start;
    size_t dataport_size;
    vm_t *vm;
};

struct camkes_shared_memory_connection {
    dataport_caps_handle_t *handle;
    emit_fn emit_fn;
    seL4_Word consume_badge;
    const char *connection_name;
};

// these are defined in the dataport's glue code
extern dataport_caps_handle_t ring1_handle;
extern dataport_caps_handle_t ring2_handle;

static vm_frame_t dataport_memory_iterator(uintptr_t addr, void *cookie)
{
    int error;
    cspacepath_t return_frame;
    vm_frame_t frame_result = { seL4_CapNull, seL4_NoRights, 0, 0 };
    struct dataport_iterator_cookie *dataport_cookie = (struct dataport_iterator_cookie *)cookie;
    seL4_CPtr *dataport_frames = dataport_cookie->dataport_frames;
    vm_t *vm = dataport_cookie->vm;
    uintptr_t dataport_start = dataport_cookie->dataport_start;
    size_t dataport_size = dataport_cookie->dataport_size;
    int page_size = seL4_PageBits;

    uintptr_t frame_start = ROUND_DOWN(addr, BIT(page_size));
    if (frame_start <  dataport_start ||
        frame_start > dataport_start + dataport_size) {
        ZF_LOGE("Error: Not Dataport region");
        return frame_result;
    }
    int page_idx = (frame_start - dataport_start) / BIT(page_size);
    frame_result.cptr = dataport_frames[page_idx];
    frame_result.rights = seL4_AllRights;
    frame_result.vaddr = frame_start;
    frame_result.size_bits = page_size;
    return frame_result;
}

static int init_dataport(vm_t *vm, dataport_caps_handle_t ring_handle, const CONNECTION_BASE_ADDRESS_RING)
{
    int err;
    vm_memory_reservation_t *dataport_reservation_ring = vm_reserve_memory_at(vm, CONNECTION_BASE_ADDRESS_RING, 0x1000,
                                                                         default_error_fault_callback,
                                                                         NULL);
    struct dataport_iterator_cookie *dataport_cookie_ring = malloc(sizeof(struct dataport_iterator_cookie));
    if (!dataport_cookie_ring) {
        ZF_LOGE("Failed to allocate dataport iterator cookie");
        return -1;
    }
    dataport_cookie_ring->vm = vm;
    dataport_cookie_ring->dataport_frames = ring_handle.get_frame_caps();
    dataport_cookie_ring->dataport_start = CONNECTION_BASE_ADDRESS_RING;
    dataport_cookie_ring->dataport_size = ring_handle.get_size();
    err = vm_map_reservation(vm, dataport_reservation_ring, dataport_memory_iterator, (void *)dataport_cookie_ring);
    if (err) {
        ZF_LOGE("Failed to map dataport memory");
        return -1;
    }
    return 0;
}

void init_shared_memory(vm_t *vm, void *cookie)
{
    int err;

    // RING 1
    err = init_dataport(vm, ring1_handle, CONNECTION_BASE_ADDRESS_RING1);
    if (err){
        ZF_LOGE("Failed to init dataport");
        return -1;
    }

    // RING 2
    err = init_dataport(vm, ring2_handle, CONNECTION_BASE_ADDRESS_RING2);
    if (err){
        ZF_LOGE("Failed to init dataport");
        return -1;
    }
}

DEFINE_MODULE(shared_memory, NULL, init_shared_memory)