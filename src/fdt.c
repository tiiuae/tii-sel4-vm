/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <fdt_utils.h>

#include <fdt_custom.h>

extern const int __attribute__((weak)) tracebuffer_base;
extern const int __attribute__((weak)) tracebuffer_size;
extern const int __attribute__((weak)) ramoops_base;
extern const int __attribute__((weak)) ramoops_size;

static int fdt_assign_phandle(void *fdt, int offset, uint32_t *result_phandle)
{
    uint32_t max_phandle = fdt_get_max_phandle(fdt);
    if (max_phandle == (uint32_t)-1) {
        return -FDT_ERR_NOPHANDLES;
    }

    uint32_t phandle = max_phandle + 1;
    if (phandle < max_phandle || phandle == (uint32_t)-1) {
        ZF_LOGE("Too many phandles");
        return -FDT_ERR_NOPHANDLES;
    }

    int err = fdt_appendprop_u32(fdt, offset, "phandle", phandle);
    if (err) {
        return err;
    }

    if (result_phandle) {
        *result_phandle = phandle;
    }

    return 0;
}

int fdt_generate_reserved_node(void *fdt, const char *name,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle)
{
    int root_offset = fdt_path_offset(fdt, "/reserved-memory");
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    if (root_offset < 0) {
        ZF_LOGE("/reserved-memory node not found");
        err = root_offset;
        goto error;
    }

    int this = fdt_add_subnode(fdt, root_offset, name);
    if (this < 0) {
        err = this;
        goto error;
    }

    int err = fdt_appendprop_string(fdt, this, "compatible", compatible);
    if (err) {
        goto error;
    }

    err = fdt_appendprop_uint(fdt, this, "reg", base, address_cells);
    if (err) {
        goto error;
    }

    err = fdt_appendprop_uint(fdt, this, "reg", size, size_cells);
    if (err) {
        goto error;
    }

    err = fdt_assign_phandle(fdt, this, phandle);
    if (err) {
        goto error;
    }

    ZF_LOGI("Generated /reserved-memory/%s 0x%"PRIxPTR" - 0x%"PRIxPTR,
            base, base - 1 + size);

error:
    ZF_LOGE("Cannot generate /reserved-memory/%s: %s (%d)", name, err,
            fdt_stderror(err));
    return err;
}

static int fdt_generate_trace_nodes(void *gen_fdt)
{
    int err;

    if (&tracebuffer_base && &tracebuffer_size &&
        tracebuffer_base && tracebuffer_size) {
        err = fdt_generate_reserved_node(fdt, "sel4_tracebuffer",
                                         "sel4_tracebuffer", tracebuffer_base,
                                         tracebuffer_size, NULL);
        if (err) {
            return err;
        }
    }

    if (&ramoops_base && &ramoops_size &&
        ramoops_base && ramoops_size) {
        err = fdt_generate_reserved_node(fdt, "ramoops", "ramoops",
                                         ramoops_base, ramoops_size, NULL);
        if (err) {
            return err;
        }
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
