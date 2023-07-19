/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <autoconf.h>
#include <tii_sel4vm/gen_config.h>

#include <sel4vm/guest_vm.h>

#include <libfdt.h>
#include <utils/util.h>

int fdt_generate_reserved_node(void *fdt, const char *name,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle);
