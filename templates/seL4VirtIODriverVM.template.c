/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <camkes.h>
#include <vmlinux.h>

#include <tii/guest.h>
#include <tii/ram_dataport.h>
#include <tii/io_proxy.h>
#include <tii/camkes/io_proxy.h>
#include <tii/fdt.h>

/*- set vm_virtio_devices = configuration[me.name].get('vm_virtio_devices') -*/

/*- for dev in vm_virtio_devices -*/
extern void *vm/*? dev.id ?*/_iobuf;

ram_dataport_t __attribute__((section("_ram_dataport_definition"))) vm/*? dev.id ?*/_ram_dataport;

static uintptr_t vm/*? dev.id ?*/_iobuf_get(io_proxy_t *io_proxy)
{
    return (uintptr_t)vm/*? dev.id ?*/_iobuf;
}

static void vm/*? dev.id ?*/_notify(void *cookie)
{
    vm/*? dev.id ?*/_ntfn_send_emit();
}

static void vm/*? dev.id ?*/_ntfn_callback(void *opaque)
{
    io_proxy_t *io_proxy = opaque;

    int err = vm/*? dev.id ?*/_ntfn_recv_reg_callback(vm/*? dev.id ?*/_ntfn_callback, opaque);
    assert(!err);

    err = rpc_run(io_proxy);
    if (err) {
        ZF_LOGF("rpc_run() failed, guest corrupt");
        /* no return */
    }
}

int vm/*? dev.id ?*/_io_proxy_run(io_proxy_t *io_proxy)
{
    vm/*? dev.id ?*/_ntfn_callback(io_proxy);
    return 0;
}

static fdt_reserved_memory_t fdt_swiotlb_vm/*? dev.id ?*/ = {
    .rm = {
        .base = /*? dev.data_base ?*/,
        .size = /*? dev.data_size ?*/,
    },
    .name = "swiotlb_vm/*? dev.id ?*/",
    .compatible = "restricted-dma-pool",
};

io_proxy_t vm/*? dev.id ?*/_io_proxy = {
    .data_plane = &fdt_swiotlb_vm/*? dev.id ?*/.rm,
    .ctrl_base = /*? dev.ctrl_base ?*/,
    .ctrl_size = /*? dev.ctrl_size ?*/,
    .run = vm/*? dev.id ?*/_io_proxy_run,
    .iobuf_get = vm/*? dev.id ?*/_iobuf_get,
    .rpc = {
        /* queue addresses need to be filled in run time */
        .doorbell = vm/*? dev.id ?*/_notify,
        .doorbell_cookie = &vm/*? dev.id ?*/_io_proxy,
    },
};

DEFINE_MODULE(vm/*? dev.id ?*/_io_proxy, &vm/*? dev.id ?*/_io_proxy, camkes_io_proxy_module_init)
/* vpci modules are in vm/components/VM_Arm/src/modules/pci.c */
DEFINE_MODULE_DEP(vm/*? dev.id ?*/_io_proxy, vpci_init)
DEFINE_MODULE_DEP(vpci_register_devices, vm/*? dev.id ?*/_io_proxy)
/*- endfor -*/

int ram_dataport_setup(void)
{
    ram_dataport_t *ram_dp;
    dataport_caps_handle_t *dp;
/*- for dev in vm_virtio_devices -*/
    extern dataport_caps_handle_t vm/*? dev.id ?*/_memdev_handle;
    dp = &vm/*? dev.id ?*/_memdev_handle;
    ram_dp = &vm/*? dev.id ?*/_ram_dataport;
    ram_dp->addr = /*? dev.data_base ?*/;
    /* TODO: dev.data_size is ignored, should we do safety check? */
    ram_dp->frames = dp->get_frame_caps();
    ram_dp->num_frames = dp->get_num_frame_caps();
    ram_dp->frame_size_bits = dp->get_frame_size_bits();
/*- endfor -*/

    return 0;
}
