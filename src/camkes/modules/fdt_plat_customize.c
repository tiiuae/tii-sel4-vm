/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <camkes.h>
#include <vmlinux.h>

#include <tii/fdt.h>

int WEAK fdt_plat_customize(vm_t *vm, void *dtb_buf)
{
    return 0;
}

static void fdt_plat_customize_init(vm_t *vm, void *cookie)
{
    if (!vm_config.generate_dtb) {
        return;
    }

    int err = fdt_plat_customize(vm, cookie);
    if (err) {
        ZF_LOGF("fdt_plat_customize() failed (%d)", err);
        /* no return */
    }
}

DEFINE_MODULE(fdt_plat_customize, gen_dtb_buf, fdt_plat_customize_init)
