/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <utils/util.h>

#include <fdt_custom.h>

#define USB_PCI_NODE_PATH   "/scb/pcie@7d500000/pci@0,0"
#define USB_NODE_NAME       "usb@0,0"

static int fdt_generate_usb_node(void *fdt)
{
    int root_offset = fdt_path_offset(fdt, USB_PCI_NODE_PATH);
    int address_cells = fdt_address_cells(fdt, root_offset);
    int size_cells = fdt_size_cells(fdt, root_offset);

    if (root_offset <= 0) {
        ZF_LOGI("Not adding %s, parent %s missing", USB_NODE_NAME,
                USB_PCI_NODE_PATH);
        return 0;
    }

    int this = fdt_add_subnode(fdt, root_offset, USB_NODE_NAME);
    if (this < 0) {
        ZF_LOGE("Can't add %s subnode: %d", USB_NODE_NAME, this);
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

    ZF_LOGI("%s/%s added to device tree", USB_PCI_NODE_PATH, USB_NODE_NAME);

    return 0;
}

int fdt_plat_customize(void *cookie, void *dtb_buf)
{
    int err;

    err = fdt_generate_usb_node(dtb_buf);
    if (err) {
        ZF_LOGE("Cannot generate USB DTB node (%d)", err);
        return -1;
    }

    return 0;
}
