/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <stdbool.h>

#include <libfdt.h>

#include <tii/fdt.h>
#include <tii/utils.h>
#include <tii/pci.h>
#include <tii/guest.h>
#include <tii/io_proxy.h>

#define fdt_format(_buf, _len, _fmt, ...) ({ \
    int _n = snprintf(_buf, _len, _fmt, ##__VA_ARGS__); \
    ((_n < 0) || (_n >= _len)) ? -FDT_ERR_INTERNAL : 0; \
})

/* TODO: refactor fdt_generate_memory_node out of CAmkES VM */
int fdt_generate_memory_node(void *fdt, uintptr_t base, size_t size);

#define MAX_FDT_NODES 32

static USED SECTION("_fdt_node") fdt_node_t *fdt_nodes[MAX_FDT_NODES];
static int num_fdt_nodes;

static int fdt_assign_phandle(void *fdt, int offset)
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

    return 0;
}

int fdt_generate_reserved_node(void *fdt, const char *prefix,
                               const char *compatible, uintptr_t base,
                               size_t size)
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

    err = fdt_assign_phandle(fdt, this);
    if (err) {
        goto error;
    }

    /* Generating a "memory" node in addition to "reserved-memory" node
     * is not strictly necessary but enables sanity checks within Linux
     * kernel. If we accidentally declare some already used memory area
     * as reserved memory, Linux warns us.
     */
    err = fdt_generate_memory_node(fdt, base, size);
    if (err) {
        ZF_LOGE("fdt_generate_memory_node() failed (%d)", err);
        return -1;
    }

    ZF_LOGI("Generated /reserved-memory/%s, size %zu", name, size);
    return this;

error:
    ZF_LOGE("Cannot generate /reserved-memory/%s: %s (%d)", name,
            fdt_strerror(err), err);
    return err;
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

static int fdt_reserved_memory_phandle(void *fdt, const char *name,
                                       uint32_t *phandle)
{
    if (!fdt || !name || !phandle) {
        return -FDT_ERR_INTERNAL;
    }

    char path[256];
    int n = snprintf(path, sizeof path, "/reserved-memory/%s", name);
    if (n < 0 || n >= sizeof path) {
        return -FDT_ERR_INTERNAL;
    }

    int off = fdt_path_offset(fdt, path);
    if (off < 0) {
        if (off != -FDT_ERR_NOTFOUND) {
            ZF_LOGE("fdt_path_offset() failed (%d)", off);
        }
        return off;
    }

    uint32_t p = fdt_get_phandle(fdt, off);
    if (!p) {
        return -FDT_ERR_NOTFOUND;
    }

    *phandle = p;

    return 0;
}

int fdt_assign_reserved_memory(void *fdt, int off, const char *prefix,
                               uintptr_t base)
{
    int err;

    if (!fdt || !prefix) {
        ZF_LOGE("invalid arguments");
        return -1;
    }

    char name[64];
    err = fdt_format_memory_name(name, sizeof(name), prefix, base);
    if (!name) {
        ZF_LOGE("fdt_format_memory_name() failed (%d)", err);
        return -1;
    }

    uint32_t phandle;
    err = fdt_reserved_memory_phandle(fdt, name, &phandle);
    if (err == -FDT_ERR_NOTFOUND) {
        return 0;
    }
    if (err < 0) {
        ZF_LOGE("fdt_reserved_memory_phandle() failed (%d)", err);
        return -1;
    }

    err = fdt_appendprop_u32(fdt, off, "memory-region", phandle);
    if (err) {
        ZF_LOGE("fdt_appendprop_u32() failed (%d)", err);
        return err;
    }

    return 0;
}

int fdt_node_add(fdt_node_t *node)
{
    if (num_fdt_nodes >= ARRAY_SIZE(fdt_nodes)) {
        ZF_LOGE("too many dynamic device tree nodes");
        return -1;
    }

    fdt_nodes[num_fdt_nodes++] = node;

    return 0;
}

static int fdt_node_generate(fdt_node_t *node, void *fdt)
{
    if (!node || !node->generate || !fdt) {
        return -1;
    }

    if (node->generated) {
        return 0;
    }

    int rc = node->generate(node, fdt);
    if (rc > 0) {
        node->generated = true;
    }

    return rc;
}

int fdt_node_generate_dataport(fdt_node_t *node, void *fdt)
{
    fdt_dataport_t *dataport = container_of(node, fdt_dataport_t, node);

    int offset = fdt_generate_reserved_node(fdt, node->name, node->compatible,
                                            dataport->gpa, dataport->size);
    if (offset <= 0) {
        ZF_LOGE("fdt_generate_reserved_node() failed (%d)", offset);
        return -1;
    }

    return 1;
}

int fdt_node_generate_swiotlb(fdt_node_t *node, void *fdt)
{
    fdt_dataport_t *dataport = container_of(node, fdt_dataport_t, node);

    if (dataport->gpa == guest_ram_base && dataport->size == guest_ram_size) {
        return 0;
    }

    return fdt_node_generate_dataport(node, fdt);
}

static int fdt_node_filter_compatible(fdt_node_t *node, void *cookie)
{
    const char *compatible = cookie;

    if (!node || !compatible) {
        ZF_LOGE("invalid arguments");
        return -1;
    }

    if (!node->compatible || strcmp(node->compatible, compatible)) {
        return 0;
    }

    return 1;
}

static int fdt_node_generate_filter(int (*filter_fn)(fdt_node_t *node, void *cookie),
                                    void *cookie, void *fdt)
{
    fdt_node_t **start = __start__fdt_node;
    fdt_node_t **stop = __stop__fdt_node;
    int rc = 0;

    for (fdt_node_t **ptr = start; !rc && ptr < stop; ptr++) {
        fdt_node_t *node = (*ptr);

        if (!node) {
            continue;
        }

        if (filter_fn) {
            rc = filter_fn(node, cookie);
            if (rc == -1) {
                return -1;
            }
        }

        if (rc == 0) {
            continue;
        }

        rc = fdt_node_generate(node, fdt);
        if (rc < 0) {
            ZF_LOGE("fdt_node_generate() failed (%d)", rc);
            return -1;
        }
    }

    return 0;
}

int fdt_node_generate_compatibles(void *fdt, const char *compatible)
{
    /* non-const cast safe because it is just passed through */
    return fdt_node_generate_filter(fdt_node_filter_compatible,
                                    (void *)compatible, fdt);
}

int fdt_node_generate_all(void *fdt)
{
    return fdt_node_generate_filter(NULL, NULL, fdt);
}
