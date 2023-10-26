/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <inttypes.h>

#include <libfdt.h>
#include <utils/util.h>

#include <fdt_custom.h>

/* see linux/include/linux/pci.h */
#define PCI_DEVFN(slot, func)  ((((slot) & 0x1f) << 3) | ((func) & 0x07))

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

int fdt_generate_reserved_node(void *fdt, const char *name,
                               const char *compatible, uintptr_t base,
                               size_t size, uint32_t *phandle)
{
    int err;

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
    char name[64];
    sprintf(name, "swiotlb@%"PRIxPTR, data_base);

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

    return fdt_generate_reserved_node(fdt, name, "restricted-dma-pool",
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

static int fdt_generate_pci_node(void *fdt, const char *name, uint32_t slot)
{
    int root_offset = fdt_path_offset(fdt, "/pci");
    if (root_offset < 0) {
        return root_offset;
    }

    int this = fdt_add_subnode(fdt, root_offset, name);
    if (this < 0) {
        ZF_LOGE("Cannot add subnode %s (%d)", name, this);
        return this;
    }

    uint32_t devfn = PCI_DEVFN(slot, 0);
    int err = fdt_appendprop_u32(fdt, this, "reg", (devfn & 0xff) << 8);
    for (int j = 0; !err && j < 4; j++) {
        err = fdt_appendprop_u32(fdt, this, "reg", 0);
    }
    if (err) {
        ZF_LOGE("Can't append reg property: %d", err);
        return err;
    }

    return this;
}

int fdt_generate_virtio_node(void *fdt, unsigned int slot, uintptr_t data_base,
                             size_t data_size)
{
    char name[64];
    sprintf(name, "virtio%d", slot);

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

    int this = fdt_generate_pci_node(fdt, name, slot);
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

int WEAK fdt_plat_customize(void *cookie, void *dtb_buf)
{
    return 0;
}
