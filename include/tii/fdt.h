/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <autoconf.h>

#include <libfdt.h>

#include <tii/guest.h>

typedef struct vm vm_t;

typedef struct fdt_reserved_memory {
    guest_reserved_memory_t rm;
    const char *name;
    const char *compatible;
} fdt_reserved_memory_t;

int fdt_plat_customize(vm_t *vm, void *dtb_buf);

int fdt_generate_reserved_node(void *fdt, const char *prefix,
                               const char *compatible, uintptr_t base,
                               size_t size);

int fdt_format_memory_name(char *buffer, size_t len, const char *prefix,
                           uintptr_t base);

int fdt_format_pci_devfn_name(char *buffer, size_t len, const char *prefix,
                              uint32_t devfn);

int fdt_generate_pci_node(void *fdt, const char *prefix, uint32_t devfn);

int fdt_generate_reserved_memory_nodes(void *fdt);

int fdt_assign_reserved_memory(void *fdt, int off, guest_reserved_memory_t *rm);
