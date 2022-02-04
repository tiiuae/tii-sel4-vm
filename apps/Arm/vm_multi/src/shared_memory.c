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

#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include <signal.h>

#ifdef CONFIG_PLAT_QEMU_ARM_VIRT
#define CONNECTION_BASE_ADDRESS 0xDF000000
#else
#define CONNECTION_BASE_ADDRESS 0x3F000000
#endif

struct dataport_iterator_cookie {
    seL4_CPtr *dataport_frames;
    uintptr_t dataport_start;
    size_t dataport_size;
    vm_t *vm;
};

// these are defined in the dataport's glue code
extern dataport_caps_handle_t buff_handle;

static struct camkes_crossvm_connection connections[] = {
	{&buff_handle, NULL, -1, NULL}
};

static int consume_callback(vm_t *vm, void *cookie)
{
    consume_connection_event(vm, connections[0].consume_badge, true);
    return 0;
}

static int write_buffer(vm_t *vm, uintptr_t load_addr)
{
	char *value = "test";

	vm_ram_mark_allocated(vm, load_addr, 4);
	clean_vm_ram_touch(vm, load_addr, 4, vm_guest_ram_write_callback, value); 

    return 0;
}

static int read_buffer(vm_t *vm, uintptr_t load_addr)
{
    char *value_test;

    vm_ram_mark_allocated(vm, load_addr, 4);
    clean_vm_ram_touch(vm, load_addr, 4, vm_guest_ram_read_callback, &value_test);

    return 0;
}

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

void init_shared_memory(vm_t *vm, void *cookie)
{
    int err;

    vm_memory_reservation_t *dataport_reservation = vm_reserve_memory_at(vm, CONNECTION_BASE_ADDRESS, 0x1000,
                                                                         default_error_fault_callback,
                                                                         NULL);
    struct dataport_iterator_cookie *dataport_cookie = malloc(sizeof(struct dataport_iterator_cookie));
    if (!dataport_cookie) {
        ZF_LOGE("Failed to allocate dataport iterator cookie");
        return -1;
    }
    dataport_cookie->vm = vm;
    dataport_cookie->dataport_frames = buff_handle.get_frame_caps();
    dataport_cookie->dataport_start = CONNECTION_BASE_ADDRESS;
    dataport_cookie->dataport_size = buff_handle.get_size();
    err = vm_map_reservation(vm, dataport_reservation, dataport_memory_iterator, (void *)dataport_cookie);
    if (err) {
        ZF_LOGE("Failed to map dataport memory");
        return -1;
    }

    if (!strcmp(linux_image_config.vm_name, "vm0")) { 
        
        write_buffer(vm, CONNECTION_BASE_ADDRESS);

        char *ptr = (char *)buff;
        printf("VM: %s - Writting buff: %p - 0x%x\n", linux_image_config.vm_name, ptr, *ptr);

    } else if (!strcmp(linux_image_config.vm_name, "vm1")) { 
        int i=0;
        
        while (i<999999999) {
            i++;
        }
            
        read_buffer(vm, CONNECTION_BASE_ADDRESS);

        char *ptr = (char *)buff;
        printf("VM: %s - Reading buff: %p - 0x%x\n", linux_image_config.vm_name, ptr, *ptr);

    }
}

DEFINE_MODULE(shared_memory, NULL, init_shared_memory)