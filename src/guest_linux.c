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

static int fdt_generate_pci_config(void *fdt)
{
    for (int i = 0; i < pci_dev_count; i++) {
        int this = fdt_generate_pci_node(fdt, "virtio", pci_devs[i]->devfn);
        if (this <= 0) {
            ZF_LOGE("fdt_generate_pci_node() failed (%d)", this);
            return -1;
        }

        int err = fdt_assign_reserved_memory(fdt, this, "swiotlb",
                                             pci_devs[i]->io_proxy->data_base);
        if (err) {
            ZF_LOGE("fdt_assign_reserved_memory() failed (%d)", err);
            return -1;
        }
    }

    return 0;
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

    err = fdt_node_generate_compatibles(config->dtb, "restricted-dma-pool");
    if (err) {
        ZF_LOGE("fdt_node_generate_compatibles() failed (%d)", err);
        return -1;
    }

    err = fdt_generate_pci_config(config->dtb);
    if (err) {
        ZF_LOGE("fdt_generate_pci_config() failed (%d)", err);
        return -1;
    }

    err = fdt_node_generate_all(config->dtb);
    if (err) {
        ZF_LOGE("fdt_node_generate_all() failed (%d)", err);
        return -1;
    }

    return 0;
}
