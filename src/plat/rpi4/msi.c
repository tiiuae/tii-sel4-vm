/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI interrupt extension emulation for GICv2.
 */

#include <sel4vm/guest_vm.h>
#include <tii/gicv2m.h>
#include <tii/msi.h>

static gicv2m_t v2m = {
    .base = 0x08021000,
    .size = BIT(PAGE_BITS_4K),
    .irq_base = 144,
    .num_irq = 32,
};

int msi_irq_set(uint32_t irq, bool active)
{
    if (!v2m_irq_valid(&v2m, irq)) {
        return RPCMSG_RC_NONE;
    }

    if (active && v2m_inject_irq(&v2m, irq)) {
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
}

int handle_msi(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err = RPCMSG_RC_NONE;

    switch (op) {
    case QEMU_OP_SET_IRQ:
        err = msi_irq_set(msg->mr1, true);
        break;
    case QEMU_OP_CLR_IRQ:
        err = msi_irq_set(msg->mr1, false);
    default:
        break;
    }

    return err;
}

int msi_init(vm_t *vm)
{
    return v2m_init(&v2m, vm);
}
