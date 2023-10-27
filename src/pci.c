/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tii/pci.h>
#include <tii/shared_irq_line.h>
#include <ioreq.h>

#include <fdt_custom.h>

static bool pci_irq_init_done = false;

static unsigned int pci_dev_count;

static shared_irq_line_t pci_intx[PCI_NUM_PINS];

seL4_Word pci_cfg_ioreq_native(pcidev_t *pcidev, unsigned int dir,
                               uintptr_t offset, size_t size, seL4_Word value)
{
    unsigned int backend_slot = pcidev - pcidev->io_proxy->pci_slots;

    int err = ioreq_native(pcidev->io_proxy, AS_PCIDEV(backend_slot),
                           dir, offset, size, &value);
    if (err) {
        ZF_LOGE("ioreq_native() failed (%d)", err);
        return 0;
    }

    return value;
}

static int pcidev_register(io_proxy_t *io_proxy, unsigned int backend_slot)
{
    if ((pci_dev_count + 1) >= PCI_NUM_SLOTS) {
        ZF_LOGE("No free slots");
        return -1;
    }

    pcidev_t *pcidev = &io_proxy->pci_slots[backend_slot];

    int slot = io_proxy->pcidev_register(pcidev, io_proxy->pci_bus_cookie);
    if (slot == -1) {
        ZF_LOGE("pcidev_register() callback failed (%d)", slot);
        return -1;
    }

    pcidev->slot = slot;
    pcidev->io_proxy = io_proxy;

    ZF_LOGI("Registering PCI device %u (remote %u:%u)", pcidev->slot,
            pcidev->io_proxy->backend_id, backend_slot);

    int err = fdt_generate_virtio_node(io_proxy->dtb_buf, pcidev->slot,
                                       io_proxy->data_base,
                                       io_proxy->data_size);
    if (err) {
        ZF_LOGE("fdt_generate_virtio_node() failed (%d)", err);
        return -1;
    }

    pci_dev_count++;

    return 0;
}

static int pcidev_intx_set(io_proxy_t *io_proxy, unsigned int backend_slot,
                           unsigned int intx, bool level)
{
    if (backend_slot >= ARRAY_SIZE(io_proxy->pci_slots)) {
        return -1;
    }

    pcidev_t *pcidev = &io_proxy->pci_slots[backend_slot];
    if (!pcidev) {
        return -1;
    }

    return shared_irq_line_change(&pci_intx[pci_map_irq(pcidev, intx)],
                                  pcidev->slot, level);
}

int handle_pci(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err;

    switch (op) {
    case RPC_MR0_OP_SET_IRQ:
        err = pcidev_intx_set(io_proxy, msg->mr1 >> 2, msg->mr1 & 3, msg->mr2);
        break;
    case RPC_MR0_OP_REGISTER_PCI_DEV:
        err = pcidev_register(io_proxy, msg->mr1);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    if (err) {
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
}

int pci_irq_init(void *vcpu_cookie, unsigned int irq_base)
{
    if (pci_irq_init_done) {
        return 0;
    }

    for (int i = 0; i < PCI_NUM_PINS; i++) {
        int err = shared_irq_line_init(&pci_intx[i], vcpu_cookie, irq_base + i);
        if (err) {
            ZF_LOGE("shared_irq_line_init() failed (%d)", err);
            return -1;
        }
    }

    pci_irq_init_done = true;

    return 0;
}
