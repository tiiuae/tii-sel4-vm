/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vmmplatsupport/drivers/pci.h>
#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <camkes.h>
#include <vmlinux.h>

#include <tii/ram_dataport.h>
#include <tii/camkes/io_proxy.h>

#include <ioreq.h>

#define CONNECTION_BASE_ADDRESS PCI_MEM_REGION_ADDR

/*- set vm_virtio_devices = configuration[me.name].get('vm_virtio_devices') -*/
/*- set vm_virtio_drivers = configuration[me.name].get('vm_virtio_drivers') -*/

/*- macro vm_virtio_driver_ctrl_base() -*/
    CONNECTION_BASE_ADDRESS
    /*- if vm_virtio_drivers -*/
        /*- for drv in vm_virtio_drivers -*/
        + 4 * (/*? drv.data_size ?*/) /* data plane to driver 'vm/*? drv.id ?*/' */
        /*- endfor -*/
    /*- endif -*/
/*- endmacro -*/

/*- set vars = { 'ctrl_base': vm_virtio_driver_ctrl_base() } -*/

/*- for dev in vm_virtio_devices -*/
    /*- set dummy = vars.update({'vm' ~ dev.id: vars.ctrl_base}) -*/
    /*- set dummy = vars.update({'ctrl_base': vars.ctrl_base + '\n        + 4 * (' + dev.ctrl_size + ') /* control plane from device \'vm' ~ dev.id ~ '\' */'}) -*/
/*- endfor -*/

/*- for dev in vm_virtio_devices -*/
extern void *vm/*? dev.id ?*/_iobuf;

ram_dataport_t __attribute__((section("_ram_dataport_definition"))) vm/*? dev.id ?*/_ram_dataport;

static uintptr_t vm/*? dev.id ?*/_iobuf_page_get(io_proxy_t *io_proxy, unsigned int page)
{
    uintptr_t addr = (uintptr_t)vm/*? dev.id ?*/_iobuf;

    addr += page * 4096;

    return addr;
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

io_proxy_t vm/*? dev.id ?*/_io_proxy = {
    .data_base = /*? dev.data_base ?*/,
    .data_size = /*? dev.data_size ?*/,
    .ctrl_base = /*? vars['vm' ~ dev.id] ?*/,
    .ctrl_size = /*? dev.ctrl_size ?*/,
    .run = vm/*? dev.id ?*/_io_proxy_run,
    .iobuf_page_get = vm/*? dev.id ?*/_iobuf_page_get,
    .rpc = {
        /* queue addresses need to be filled in run time */
        .doorbell = vm/*? dev.id ?*/_notify,
        .doorbell_cookie = &vm/*? dev.id ?*/_io_proxy,
    },
};

DEFINE_MODULE(vm/*? dev.id ?*/_io_proxy, &vm/*? dev.id ?*/_io_proxy, camkes_io_proxy_module_init)
/*- endfor -*/

int ram_dataport_setup(void)
{
    ram_dataport_t *ram_dp;
    dataport_caps_handle_t *dp;
/*- for dev in vm_virtio_devices -*/
    extern dataport_caps_handle_t vm/*? dev.id ?*/_memdev_handle;
    dp = &vm/*? dev.id ?*/_memdev_handle;
    ram_dp = &vm/*? dev.id ?*/_ram_dataport;
    ram_dp->addr = /*? dev.data_base ?*/,
    /* TODO: dev.data_size is ignored, should we do safety check? */
    ram_dp->frames = dp->get_frame_caps();
    ram_dp->num_frames = dp->get_num_frame_caps();
    ram_dp->frame_size_bits = dp->get_frame_size_bits();
/*- endfor -*/

    return 0;
}
