/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/util.h>

#include <tii/io_proxy.h>
#include <tii/fdt.h>
#include <tii/guest.h>
#include <tii/pci.h>
#include <tii/vmm.h>
#include <tii/libsel4vm/guest.h>

static int pci_generate_config(void *fdt)
{
    for (int i = 0; i < pci_dev_count; i++) {
        int this = fdt_generate_pci_node(fdt, "virtio", pci_devs[i]->devfn);
        if (this <= 0) {
            ZF_LOGE("fdt_generate_pci_node() failed (%d)", this);
            return -1;
        }

        guest_reserved_memory_t *rm = pci_devs[i]->io_proxy->data_plane;

        int err = fdt_assign_reserved_memory(fdt, this, rm);
        if (err) {
            ZF_LOGE("fdt_assign_reserved_memory() failed (%d)", err);
            return -1;
        }
    }

    return 0;
}

int guest_register_io_proxy(io_proxy_t *io_proxy)
{
    if (io_proxy->data_plane->base == guest_ram_base &&
        io_proxy->data_plane->size == guest_ram_size) {
        return 0;
    }

    return guest_reserved_memory_add(io_proxy->data_plane);
}

int guest_configure(void *cookie)
{
    guest_config_t *config = cookie;

    int err;

    err = vmm_module_init_by_name("vpci_install", config->vm);
    if (err) {
        ZF_LOGE("vmm_module_init_by_name() failed");
        return -1;
    }

    if (!config->generate_dtb) {
        return 0;
    }

    err = fdt_generate_reserved_memory_nodes(config->dtb);
    if (err) {
        ZF_LOGE("fdt_generate_reserved_memory_nodes() failed (%d)", err);
        return -1;
    }

    err = pci_generate_config(config->dtb);
    if (err) {
        ZF_LOGE("pci_generate_config() failed (%d)", err);
        return -1;
    }

    return 0;
}
