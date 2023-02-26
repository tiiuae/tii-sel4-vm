/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <autoconf.h>
#include <vm_qemu_virtio/gen_config.h>

#include <camkes.h>
#include <sel4vm/guest_vm.h>

#include <libfdt.h>
#include <utils/util.h>

int fdt_customize(vm_t *vm, void *gen_fdt)
{
    return 0;
}
