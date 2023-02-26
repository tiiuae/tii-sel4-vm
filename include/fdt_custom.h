/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <autoconf.h>
#include <tii_camkes_vm/gen_config.h>

#include <camkes.h>
#include <sel4vm/guest_vm.h>

#include <libfdt.h>
#include <utils/util.h>

extern const int vmid;

int fdt_plat_customize(vm_t *vm, void *gen_fdt);
