/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <fdt_custom.h>

int fdt_customize(vm_t *vm, void *gen_fdt)
{
    int err;

    err = fdt_plat_customize(vm, gen_fdt);
    if (err) {
        return err;
    }

    return 0;
}
