/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <libfdt.h>

extern uintptr_t guest_ram_base;
extern size_t guest_ram_size;

int fdt_plat_customize(void *cookie, void *dtb_buf);

int fdt_generate_reserved_node(void *fdt, const char *name,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle);

int fdt_generate_virtio_node(void *fdt, unsigned int slot, uintptr_t data_base,
                             size_t data_size);
