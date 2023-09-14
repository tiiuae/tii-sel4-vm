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

extern uintptr_t guest_ram_base;
extern size_t guest_ram_size;

int fdt_plat_customize(vm_t *vm, void *dtb_buf);

int fdt_generate_reserved_node(void *fdt, const char *name,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle);

int fdt_generate_virtio_node(void *fdt, unsigned int idx, uintptr_t base,
                             size_t size);
