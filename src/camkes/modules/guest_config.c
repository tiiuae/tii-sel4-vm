/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <camkes.h>
#include <vmlinux.h>

#include <tii/guest.h>
#include <tii/libsel4vm/guest.h>

static void camkes_guest_configure(vm_t *vm, void *cookie)
{
    guest_config_t config = {
        .vm = vm,
        .dtb = cookie,
    };

    int err = guest_configure(&config);
    if (err) {
        ZF_LOGE("guest_configure() failed (%d)", err);
        /* no return */
    }
}

DEFINE_MODULE(guest_config, gen_dtb_buf, camkes_guest_configure)
