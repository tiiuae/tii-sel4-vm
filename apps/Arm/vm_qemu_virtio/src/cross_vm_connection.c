/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <camkes.h>
#include <vmlinux.h>
#include <sel4vm/guest_vm.h>

#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#ifdef CONFIG_PLAT_QEMU_ARM_VIRT
#define CONNECTION_BASE_ADDRESS 0xDF000000
#elif CONFIG_PLAT_BCM2711
#define CONNECTION_BASE_ADDRESS 0x60000000
#else
#define CONNECTION_BASE_ADDRESS 0x3F000000
#endif

// these are defined in the dataport's glue code
extern dataport_caps_handle_t ctrl_handle;
extern dataport_caps_handle_t iobuf_handle;
extern dataport_caps_handle_t memdev_handle;

extern const int vmid;

static struct camkes_crossvm_connection connections[] = {
    { &ctrl_handle, intervm_source_emit, 16, "guest-control-0" },
    { &iobuf_handle, NULL, -1, "guest-iobuf-0" },
    { &memdev_handle, NULL, -1, "guest-ram-0" },
};

static int consume_callback(vm_t *vm, void *cookie)
{
    consume_connection_event(vm, connections[0].consume_badge, true);
    return 0;
}

/* VM1 does not define this */
seL4_Word WEAK intervm_sink_notification_badge(void);

void init_cross_vm_connections(vm_t *vm, void *cookie)
{
    if (vmid != 0) {
        ZF_LOGI("running inside user VM, not initializing cross connections");
        return;
    }

    connections[0].consume_badge = intervm_sink_notification_badge();
    int err = register_async_event_handler(connections[0].consume_badge, consume_callback, NULL);
    ZF_LOGF_IF(err, "Failed to register_async_event_handler for init_cross_vm_connections.");

    cross_vm_connections_init(vm, CONNECTION_BASE_ADDRESS, connections, ARRAY_SIZE(connections));
}

DEFINE_MODULE(cross_vm_connections, NULL, init_cross_vm_connections)
