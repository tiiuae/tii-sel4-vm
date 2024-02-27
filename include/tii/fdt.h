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

typedef struct fdt_node {
    const char *name;
    const char *compatible;
    bool generated;
    int (*generate)(struct fdt_node *, void *);
} fdt_node_t;

#define FDT_NODE(_prefix) FDT_NODE_ ## _prefix

#define DEFINE_FDT_NODE(_name, _ptr) \
    __attribute__((used)) __attribute__((section("_fdt_node"))) fdt_node_t *FDT_NODE(_name) = (_ptr);

extern fdt_node_t *__start__fdt_node[];
extern fdt_node_t *__stop__fdt_node[];

typedef struct fdt_dataport {
    fdt_node_t node;
    uintptr_t gpa;
    size_t size;
} fdt_dataport_t;

int fdt_plat_customize(vm_t *vm, void *dtb_buf);

int fdt_generate_reserved_node(void *fdt, const char *prefix,
                               const char *compatible, uintptr_t base,
                               size_t size);

int fdt_format_memory_name(char *buffer, size_t len, const char *prefix,
                           uintptr_t base);

int fdt_format_pci_devfn_name(char *buffer, size_t len, const char *prefix,
                              uint32_t devfn);

int fdt_generate_pci_node(void *fdt, const char *prefix, uint32_t devfn);

int fdt_assign_reserved_memory(void *fdt, int off, const char *prefix,
                               uintptr_t base);

int fdt_node_add(fdt_node_t *node);
int fdt_node_generate_dataport(fdt_node_t *node, void *fdt);
int fdt_node_generate_swiotlb(fdt_node_t *node, void *fdt);
int fdt_node_generate_compatibles(void *fdt, const char *compatible);
int fdt_node_generate_all(void *fdt);
