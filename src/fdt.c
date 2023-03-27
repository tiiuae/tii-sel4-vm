/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <fdt_custom.h>

extern const int __attribute__((weak)) tracebuffer_base;
extern const int __attribute__((weak)) tracebuffer_size;
extern const int __attribute__((weak)) ramoops_base;
extern const int __attribute__((weak)) ramoops_size;

int fdt_generate_sel4_tracebuffer_node(void *fdt, unsigned long base, size_t size)
{
    int root_offset = fdt_path_offset(fdt, "/reserved-memory");
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    ZF_LOGE("/reserved-memory node: 0x%x", root_offset);

    int this = fdt_add_subnode(fdt, root_offset, "sel4_tracebuffer");
    if (this < 0) {
        ZF_LOGE("Can not create sel4_tracebuffer node");
        return this;
    }

    int err = fdt_appendprop_string(fdt, this, "compatible", "sel4_tracebuffer");
    if (err) {
        ZF_LOGE("Can not set compatible prop");
        return err;
    }

    err = append_prop_with_cells(fdt, this, base, address_cells, "reg");
    if (err) {
        ZF_LOGE("Can not add reg prop");
        return err;
    }

    err = append_prop_with_cells(fdt, this, size, size_cells, "reg");
    if (err) {
        ZF_LOGE("Can not add reg2 prop");
        return err;
    }

    return 0;
}

int fdt_generate_ramoops_node(void *fdt, unsigned long base, size_t size)
{
    int root_offset = fdt_path_offset(fdt, "/reserved-memory");
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    ZF_LOGE("/reserved-memory node: 0x%x", root_offset);

    int this = fdt_add_subnode(fdt, root_offset, "ramoops");
    if (this < 0) {
        ZF_LOGE("Can not create ramoops node");
        return this;
    }

    int err = fdt_appendprop_string(fdt, this, "compatible", "ramoops");
    if (err) {
        ZF_LOGE("Can not set compatible prop");
        return err;
    }

    err = append_prop_with_cells(fdt, this, base, address_cells, "reg");
    if (err) {
        ZF_LOGE("Can not add reg prop");
        return err;
    }

    err = append_prop_with_cells(fdt, this, size, size_cells, "reg");
    if (err) {
        ZF_LOGE("Can not add reg2 prop");
        return err;
    }

    err = append_prop_with_cells(fdt, this, size, 1, "ftrace-size");
    if (err) {
        ZF_LOGE("Can not add ftrace-size");
        return err;
    }

    return 0;
}

static int fdt_generate_trace_nodes(void *gen_fdt)
{
    int err;

    if (&tracebuffer_base && &tracebuffer_size && tracebuffer_base && tracebuffer_size) {
        err = fdt_generate_sel4_tracebuffer_node(gen_fdt, tracebuffer_base, tracebuffer_size);
        if (err) {
            ZF_LOGE("Adding sel4_tracebuffer node failed (%d)", err);
            return -1;
        }
        ZF_LOGI("Added sel4_tracebuffer node: 0x%lx@0x%lx", tracebuffer_size, tracebuffer_base);
    }

    if (&ramoops_base && &ramoops_size && ramoops_base && ramoops_size) {
        err = fdt_generate_ramoops_node(gen_fdt, ramoops_base, ramoops_size);
        if (err) {
            ZF_LOGE("Adding ramoops node failed (%d)", err);
            return -1;
        }
        ZF_LOGI("Added ramoops node: 0x%lx@0x%lx", ramoops_size, ramoops_base);
    }

    return 0;
}

int fdt_customize(vm_t *vm, void *gen_fdt)
{
    int err;

    err = fdt_plat_customize(vm, gen_fdt);
    if (err) {
        return err;
    }

    err = fdt_generate_trace_nodes(gen_fdt);
    if (err) {
        return err;
    }

    return 0;
}
