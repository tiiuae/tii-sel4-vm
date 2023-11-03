/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ioreq.h>
#include <tii/libsel4vm.h>

typedef struct vm vm_t;

int libsel4vm_io_proxy_init(io_proxy_t *io_proxy, vm_t *vm)
{
    int err;

    err = libsel4vm_fault_handler_setup(io_proxy, vm);
    if (err) {
        ZF_LOGE("libsel4vm_fault_handler_setup() failed (%d)", err);
        return -1;
    }

    err = libsel4vm_pci_setup(io_proxy, vm);
    if (err) {
        ZF_LOGE("libsel4vm_pci_setup() failed (%d)", err);
        return -1;
    }

    err = io_proxy_init(io_proxy);
    if (err) {
        ZF_LOGE("io_proxy_init() failed (%d)", err);
        return -1;
    }

    return 0;
}
