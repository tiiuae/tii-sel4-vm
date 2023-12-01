/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <libfdt.h>

#include <tii/fdt.h>
#include <tii/pci.h>
#include <tii/guest.h>

#define fdt_format(_buf, _len, _fmt, ...) ({ \
    int _n = snprintf(_buf, _len, _fmt, ##__VA_ARGS__); \
    ((_n < 0) || (_n >= _len)) ? -FDT_ERR_INTERNAL : 0; \
})

/* TODO: refactor fdt_generate_memory_node out of CAmkES VM */
int fdt_generate_memory_node(void *fdt, uintptr_t base, size_t size);

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

int fdt_generate_reserved_node(void *fdt, const char *prefix,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle)
{
    int err;

    char name[64];
    err = fdt_format_memory_name(name, sizeof(name), prefix, base);
    if (err) {
        ZF_LOGE("fdt_format_memory_name() failed (%d)", err);
        return -FDT_ERR_INTERNAL;
    }

    int root_offset = fdt_path_offset(fdt, "/reserved-memory");
    if (root_offset < 0) {
        ZF_LOGE("/reserved-memory node not found");
        err = root_offset;
        goto error;
    }

    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    int this = fdt_add_subnode(fdt, root_offset, name);
    if (this < 0) {
        err = this;
        goto error;
    }

    err = fdt_appendprop_string(fdt, this, "compatible", compatible);
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

    ZF_LOGI("Generated /reserved-memory/%s, size %zu", name, size);
    return this;

error:
    ZF_LOGE("Cannot generate /reserved-memory/%s: %s (%d)", name,
            fdt_strerror(err), err);
    return err;
}

static int fdt_get_swiotlb_node(void *fdt, uintptr_t data_base,
                                size_t data_size)
{
    int err;

    char name[64];
    err = fdt_format_memory_name(name, sizeof(name), "swiotlb", data_base);
    if (err) {
        ZF_LOGE("fdt_format_memory_name() failed (%d)", err);
        return -FDT_ERR_INTERNAL;
    }

    char path[256];
    sprintf(path, "/reserved-memory/%s", name);

    int this = fdt_path_offset(fdt, path);
    if (this >= 0) {
        return this;
    }

    /* Generating a "memory" node in addition to "reserved-memory" node
     * is not strictly necessary but enables sanity checks within Linux
     * kernel. If we accidentally declare some already used memory area
     * as reserved memory, Linux warns us.
     */
    int err = fdt_generate_memory_node(fdt, data_base, data_size);
    if (err) {
        return err;
    }

    return fdt_generate_reserved_node(fdt, "swiotlb", "restricted-dma-pool",
                                      data_base, data_size, NULL);
}

static uint32_t fdt_get_swiotlb_phandle(void *fdt, uintptr_t data_base,
                                        size_t data_size)
{
    int offset = fdt_get_swiotlb_node(fdt, data_base, data_size);
    if (offset < 0) {
        ZF_LOGE("fdt_get_swiotlb_node() failed (%d)", offset);
        return 0;
    }

    uint32_t phandle = fdt_get_phandle(fdt, offset);
    if (!phandle || phandle == (uint32_t)-1) {
        ZF_LOGE("Error getting phandle for swiotlb reserved region");
        return 0;
    }

    return phandle;
}

int fdt_format_memory_name(char *name, size_t len, const char *prefix,
                           uintptr_t base)
{
    return fdt_format(name, len, "%s@%"PRIxPTR, prefix, base);
}

int fdt_format_pci_devfn_name(char *name, size_t len, const char *prefix,
                              uint32_t devfn)
{
    return fdt_format(name, len, "%s@%u,%u", prefix, PCI_SLOT(devfn),
                      PCI_FUNC(devfn));
}

int fdt_generate_pci_node(void *fdt, const char *prefix, uint32_t devfn)
{
    int err;

    int root_offset = fdt_path_offset(fdt, "/pci");
    if (root_offset < 0) {
        ZF_LOGE("fdt_path_offset() failed (%d)", root_offset);
        return root_offset;
    }

    char name[64];
    err = fdt_format_pci_devfn_name(name, sizeof(name), prefix, devfn);
    if (err) {
        ZF_LOGE("fdt_format_pci_devfn_name() failed (%d)", err);
        return -FDT_ERR_INTERNAL;
    }

    int this = fdt_add_subnode(fdt, root_offset, name);
    if (this < 0) {
        ZF_LOGE("Cannot add subnode %s (%d)", name, this);
        return this;
    }

    /* 'reg' is a quintet (phys.hi phys.mid phys.lo size.hi size.lo), all cells
     * zero except phys.hi which is 0b00000000 bbbbbbbb dddddfff 00000000.
     *
     * For now, we also assume bus is always zero.
     */
    err = fdt_appendprop_u32(fdt, this, "reg", (devfn & 0xff) << 8);
    for (int j = 0; !err && j < 4; j++) {
        err = fdt_appendprop_u32(fdt, this, "reg", 0);
    }
    if (err) {
        ZF_LOGE("Can't append reg property: %d", err);
        return err;
    }

    return this;
}

int fdt_generate_virtio_node(void *fdt, uint32_t devfn, uintptr_t data_base,
                             size_t data_size)
{
    if (guest_ram_base == data_base && guest_ram_size == data_size) {
        /* SWIOTLB not used */
        return 0;
    }

    uint32_t swiotlb_phandle = fdt_get_swiotlb_phandle(fdt, data_base,
                                                       data_size);
    if (!swiotlb_phandle) {
        ZF_LOGE("fdt_get_swiotlb_phandle() failed");
        return -1;
    }

    int this = fdt_generate_pci_node(fdt, "virtio", devfn);
    if (this <= 0) {
        ZF_LOGE("fdt_generate_pci_node() failed (%d)", this);
        return -1;
    }

    int err = fdt_appendprop_u32(fdt, this, "memory-region", swiotlb_phandle);
    if (err) {
        ZF_LOGE("Can't append memory-region property: %d", this);
        return err;
    }

    return 0;
}
