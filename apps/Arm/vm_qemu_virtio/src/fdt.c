/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <autoconf.h>
#include <vm_qemu_virtio/gen_config.h>

#include <sel4vm/guest_vm.h>

#include <libfdt.h>
#include <utils/util.h>

extern const int vmid;
extern const int WEAK tracebuffer_base;
extern const int WEAK tracebuffer_size;
extern const int WEAK ramoops_base;
extern const int WEAK ramoops_size;

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

    return 0;
}
