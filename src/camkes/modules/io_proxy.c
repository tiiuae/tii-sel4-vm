/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <camkes.h>
#include <vmlinux.h>

#include <tii/camkes/io_proxy.h>
#include <sel4-qemu.h>
#include <ioreq.h>

void camkes_io_proxy_module_init(vm_t *vm, void *cookie)
{
    io_proxy_t *io_proxy = cookie;

    io_proxy->rx_queue = (rpcmsg_queue_t *)io_proxy_iobuf_page(io_proxy, IOBUF_PAGE_RECV);
    io_proxy->iobuf = (struct sel4_iohandler_buffer *)io_proxy_iobuf_page(io_proxy, IOBUF_PAGE_MMIO);

    int err = libsel4vm_io_proxy_init(vm, io_proxy);
    if (err) {
        ZF_LOGF("libsel4vm_io_proxy_init() failed (%d)", err);
        /* no return */
    }
}
