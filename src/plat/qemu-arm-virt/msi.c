/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSI interrupt extension emulation for GICv2.
 */

#include <sel4vm/guest_vm.h>
#include <tii/gicv2m.h>
#include <tii/msi.h>

static gicv2m_t v2m = {
    .base = 0x08020000,
    .size = BIT(PAGE_BITS_4K),
    .irq_base = 96,
    .num_irq = 32,
};

int msi_irq_set(uint32_t irq, uint32_t op)
{
    if (!v2m_irq_valid(&v2m, irq)) {
        return RPCMSG_RC_NONE;
    }

    switch (op) {
    case RPC_IRQ_SET: /* fall through */
    case RPC_IRQ_PULSE:
        if(v2m_inject_irq(&v2m, irq)) {
            return RPCMSG_RC_ERROR;
        }
        break;
    case RPC_IRQ_CLR: /* nop */
        break;
    default:
        ZF_LOGE("invalid irq operation %u", op);
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
}

int handle_msi(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err = RPCMSG_RC_NONE;

    switch (op) {
    case QEMU_OP_SET_IRQ:
        err = msi_irq_set(msg->mr1, msg->mr2);
        break;
    default:
        break;
    }

    return err;
}

int msi_init(vm_t *vm)
{
    return v2m_init(&v2m, vm);
}
