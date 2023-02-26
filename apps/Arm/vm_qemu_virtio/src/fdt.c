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

extern const int vmid;

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

    return 0;
}
