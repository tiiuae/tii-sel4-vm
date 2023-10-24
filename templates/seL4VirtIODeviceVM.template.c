/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4vmmplatsupport/drivers/pci.h>
#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <camkes.h>
#include <vmlinux.h>
#include <sel4vm/guest_vm.h>

#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>

#define CONNECTION_BASE_ADDRESS PCI_MEM_REGION_ADDR

/*- set vars = { } -*/

/*- set vm_virtio_drivers = configuration[me.name].get('vm_virtio_drivers') -*/
/*- for drv in vm_virtio_drivers -*/
extern dataport_caps_handle_t vm/*? drv.id ?*/_iobuf_handle;
extern dataport_caps_handle_t vm/*? drv.id ?*/_memdev_handle;

extern seL4_Word vm/*? drv.id ?*/_ntfn_recv_notification_badge(void);
/*- endfor -*/

static struct camkes_crossvm_connection connections[] = {
/*- for drv in vm_virtio_drivers -*/
    { &vm/*? drv.id ?*/_iobuf_handle, vm/*? drv.id ?*/_ntfn_send_emit, 16, "guest-iobuf-/*? drv.id ?*/" },
/*- endfor -*/
/*- for drv in vm_virtio_drivers -*/
    { &vm/*? drv.id ?*/_memdev_handle, NULL, -1, "guest-ram-/*? drv.id ?*/" },
/*- endfor -*/
};

static int consume_callback(vm_t *vm, void *cookie)
{
    struct camkes_crossvm_connection *connection = cookie;
    consume_connection_event(vm, connection->consume_badge, true);
    return 0;
}

static void init_cross_vm_connections(vm_t *vm, void *cookie)
{
    int err;

/*- for drv in vm_virtio_drivers -*/
    connections[/*? loop.index0 ?*/].consume_badge = vm/*? drv.id ?*/_ntfn_recv_notification_badge();
    err = register_async_event_handler(connections[/*? loop.index0 ?*/].consume_badge, consume_callback, &connections[/*? loop.index0 ?*/]);
    ZF_LOGF_IF(err, "Failed to register_async_event_handler for init_cross_vm_connections.");
/*- endfor -*/

    cross_vm_connections_init(vm, CONNECTION_BASE_ADDRESS, connections, ARRAY_SIZE(connections));
}

/*- if vm_virtio_drivers|length > 0 -*/
DEFINE_MODULE(cross_vm_connections, NULL, init_cross_vm_connections)
/*- endif -*/

/*- macro vm_virtio_driver_ctrl_base(driver_vm) -*/
    /*- set guest_virtio_drivers = configuration['vm' ~ driver_vm].get('vm_virtio_drivers') -*/
    /*- set guest_virtio_devices = configuration['vm' ~ driver_vm].get('vm_virtio_devices') -*/
    CONNECTION_BASE_ADDRESS
    /*- if guest_virtio_drivers -*/
        /*- for guest_drv in guest_virtio_drivers -*/
        + 4 * (/*? guest_drv.data_size ?*/) /* data plane to driver 'vm/*? guest_drv.id ?*/' */
        /*- endfor -*/
    /*- endif -*/
    /*- set dummy = vars.update({'done': false}) -*/
    /*- for guest_dev in guest_virtio_devices -*/
        /*- if me.name == 'vm' ~ guest_dev.id -*/
            /*- set dummy = vars.update({'done': true}) -*/
        /*- endif -*/
        /*- if not vars.done -*/
        + 4 * (/*? guest_dev.ctrl_size ?*/) /* control plane from device 'vm/*? guest_dev.id ?*/' */
        /*- endif -*/
    /*- endfor -*/
/*- endmacro -*/

const char *append_vm_virtio_device_cmdline(char *buffer)
{
/*- if vm_virtio_drivers|length > 0 -*/
    unsigned int id;
    uintptr_t data_base, ctrl_base;
    size_t data_size, ctrl_size;
    char *p = buffer;
/*- endif -*/

/*- for drv in vm_virtio_drivers -*/
    id = /*? drv.id ?*/;
    data_base = /*? drv.data_base ?*/;
    data_size = /*? drv.data_size ?*/;
    ctrl_base = /*? vm_virtio_driver_ctrl_base(drv.id) ?*/;
    ctrl_size = /*? drv.ctrl_size ?*/;
    /* TODO: safety checks */
    p += strlen(p);
    sprintf(p, " uservm=%u,0x%"PRIxPTR",0x%zx,0x%"PRIxPTR",0x%zx", id,
            data_base, data_size, ctrl_base, ctrl_size);
/*- endfor -*/

    return buffer;
}
