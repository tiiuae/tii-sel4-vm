/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <autoconf.h>
#include <vm_qemu_virtio/gen_config.h>

#include <camkes.h>
#include <sel4vm/guest_vm.h>

#include <libfdt.h>
#include <utils/util.h>

#if defined(CONFIG_VM_SWIOTLB)
/* see linux/include/linux/pci.h */
#define PCI_DEVFN(slot, func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))
#endif

extern const int vmid;
extern const int WEAK tracebuffer_base;
extern const int WEAK tracebuffer_size;
extern const int WEAK ramoops_base;
extern const int WEAK ramoops_size;

#if defined(CONFIG_VM_SWIOTLB)
/* see qemu.c */
extern unsigned int pci_dev_count;
#endif

/* TODO: proper headers, copying vm/components/VM_Arm/src/fdt_manipulation.h */
int fdt_generate_memory_node(void *fdt, uintptr_t base, size_t size);
int fdt_appendprop_uint(void *fdt, int offset, const char *name, uint64_t val,
                        int num_cells);

static int fdt_assign_phandle(void *fdt, int offset, uint32_t *result_phandle)
{
    uint32_t max_phandle = fdt_get_max_phandle(fdt);
    if (max_phandle == (uint32_t)-1) {
        return -1;
    }

    uint32_t phandle = max_phandle + 1;
    if (phandle < max_phandle || phandle == (uint32_t)-1) {
        ZF_LOGE("Too many phandles");
        return -1;
    }

    int err = fdt_appendprop_u32(fdt, offset, "phandle", phandle);
    if (err) {
        return -1;
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

    int this = fdt_add_subnode(fdt, root_offset, name);
    if (this < 0) {
        return this;
    }

    int err = fdt_appendprop_string(fdt, this, "compatible", compatible);
    if (err) {
        return err;
    }

    err = fdt_appendprop_uint(fdt, this, "reg", base, address_cells);
    if (err) {
        return err;
    }

    err = fdt_appendprop_uint(fdt, this, "reg", size, size_cells);
    if (err) {
        return err;
    }

    err = fdt_assign_phandle(fdt, this, phandle);
    if (err) {
        return err;
    }

    return 0;
}

#if defined(CONFIG_VM_SWIOTLB)
static int fdt_generate_virtio_nodes(void *fdt, uint32_t resmem_phandle)
{
    int root_offset = fdt_path_offset(fdt, "/pci");
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    if (root_offset <= 0) {
        return root_offset;
    }

    for (int i = 1; i <= pci_dev_count; i++) {
        char name[64];

        sprintf(name, "virtio%d", i);
        int this = fdt_add_subnode(fdt, root_offset, name);
        if (this < 0) {
            ZF_LOGE("Can't add %s subnode: %d", name, this);
            return this;
        }

        uint32_t devfn = PCI_DEVFN(i, 0);
        int err = fdt_appendprop_u32(fdt, this, "reg", (devfn & 0xff) << 8);
        for (int j = 0; !err && j < 4; j++) {
            err = fdt_appendprop_u32(fdt, this, "reg", 0);
        }
        if (err) {
            ZF_LOGE("Can't append reg property: %d", err);
            return err;
        }

        err = fdt_appendprop_u32(fdt, this, "memory-region", resmem_phandle);
        if (err) {
            ZF_LOGE("Can't append memory-region property: %d", this);
            return err;
        }
    }

    return 0;
}

static int generate_fdt_swiotlb(void *gen_fdt)
{
    if (!swiotlb_base || !swiotlb_size) {
        ZF_LOGI("Skipping SWIOTLB device tree setup");
        return 0;
    }

    /* Generating a "memory" node in addition to "reserved-memory" node
     * is not strictly necessary but enables sanity checks within Linux
     * kernel. If we accidentally declare some already used memory area
     * as reserved memory, Linux warns us.
     */
    int err = fdt_generate_memory_node(gen_fdt, swiotlb_base, swiotlb_size);
    if (err) {
        return err;
    }

    uint32_t phandle;
    char name[64];
    sprintf(name, "swiotlb@%"PRIxPTR, swiotlb_base);
    err = fdt_generate_reserved_node(gen_fdt, name, "restricted-dma-pool",
                                     swiotlb_base, swiotlb_size, &phandle);
    if (err) {
        return err;
    }

    err = fdt_generate_virtio_nodes(gen_fdt, phandle);
    if (err) {
        return err;
    }

    return 0;
}
#endif

static int fdt_generate_usb_node(void *fdt)
{
    if (vmid != 0) {
        ZF_LOGI("Skip adding usb@1,0 node");
        return 0;
    }

    int root_offset = fdt_path_offset(fdt, "/scb/pcie@7d500000/pci@0,0");
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    if (root_offset <= 0) {
        ZF_LOGE("root_offset: %d", root_offset);
        return root_offset;
    }

    int this = fdt_add_subnode(fdt, root_offset, "usb@0,0");
    if (this < 0) {
        ZF_LOGE("Can't add usb@1,0 subnode: %d", this);
        return this;
    }

    int err = fdt_appendprop_u32(fdt, this, "resets", 0x2d);
    if (err) {
        ZF_LOGE("Can't append resets property: %d", this);
        return err;
    }
    err = fdt_appendprop_u32(fdt, this, "resets", 0x00);
    if (err) {
        ZF_LOGE("Can't append resets property: %d", this);
        return err;
    }

    err = fdt_appendprop_u32(fdt, this, "reg", 0x10000);
    if (err) {
        ZF_LOGE("Can't append reg property: %d", this);
        return err;
    }

    err = fdt_appendprop_u32(fdt, this, "reg", 0x00);
    if (err) {
        ZF_LOGE("Can't append reg property: %d", this);
        return err;
    }

    err = fdt_appendprop_u32(fdt, this, "reg", 0x00);
    if (err) {
        ZF_LOGE("Can't append reg property: %d", this);
        return err;
    }

    err = fdt_appendprop_u32(fdt, this, "reg", 0x00);
    if (err) {
        ZF_LOGE("Can't append reg property: %d", this);
        return err;
    }

    err = fdt_appendprop_u32(fdt, this, "reg", 0x00);
    if (err) {
        ZF_LOGE("Can't append reg property: %d", this);
        return err;
    }

    ZF_LOGI("Added usb@1,0 node");

    return 0;
}

int fdt_customize(vm_t *vm, void *gen_fdt)
{
    int err = fdt_generate_usb_node(gen_fdt);
    if (err) {
        return err;
    }

    if (&tracebuffer_base && &tracebuffer_size && tracebuffer_base && tracebuffer_size) {
        err = fdt_generate_reserved_node(gen_fdt, "sel4_tracebuffer",
                                         "sel4_tracebuffer",
                                         tracebuffer_base,
                                         tracebuffer_size, NULL);
        if (err) {
            return err;
        }
        ZF_LOGD("sel4 tracebuffer node: 0x%lx@0x%lx", tracebuffer_size,
                tracebuffer_base);
    }

    if (&ramoops_base && &ramoops_size && ramoops_base && ramoops_size) {
        err = fdt_generate_reserved_node(gen_fdt, "ramoops", "ramoops",
                                         ramoops_base, ramoops_size, NULL);
        if (err) {
            return err;
        }
        ZF_LOGD("ramoops node: 0x%lx@0x%lx", ramoops_size, ramoops_base);
    }

#if defined(CONFIG_VM_SWIOTLB)
    err = generate_fdt_swiotlb(gen_fdt);
    if (err) {
        ZF_LOGE("Failed setting up SWIOTLB (%d)", err);
        return err;
    }
#endif

    return 0;
}
