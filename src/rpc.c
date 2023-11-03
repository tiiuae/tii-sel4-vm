/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ioreq.h>
#include <tii/pci.h>

typedef int (*rpc_callback_fn_t)(io_proxy_t *io_proxy, unsigned int op,
                                 rpcmsg_t *msg);

static int handle_control(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    switch (op) {
    case RPC_MR0_OP_NOTIFY_STATUS:
        io_proxy->status = msg->mr1;
        sync_sem_post(&io_proxy->status_changed);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    return RPCMSG_RC_HANDLED;
}

static rpc_callback_fn_t rpc_callbacks[] = {
    handle_mmio,
    handle_pci,
    handle_control,
    NULL,
};

int rpc_run(io_proxy_t *io_proxy)
{
    rpcmsg_t *msg;

    while ((msg = rpcmsg_queue_head(io_proxy->rpc.rx_queue)) != NULL) {
        unsigned int state = BIT_FIELD_GET(msg->mr0, RPC_MR0_STATE);

        if (state == RPC_MR0_STATE_COMPLETE) {
            rpc_assert(!"logic error");
        } else if (state == RPC_MR0_STATE_RESERVED) {
            /* emu side is crafting message, let's continue later */
            break;
        }

        int rc = RPCMSG_RC_NONE;
        for (rpc_callback_fn_t *cb = rpc_callbacks;
             rc == RPCMSG_RC_NONE && *cb; cb++) {
            rc = (*cb)(io_proxy, BIT_FIELD_GET(msg->mr0, RPC_MR0_OP), msg);
        }

        if (rc == RPCMSG_RC_ERROR) {
            return -1;
        }

        if (rc == RPCMSG_RC_NONE) {
            if (BIT_FIELD_GET(msg->mr0, RPC_MR0_STATE) != RPC_MR0_STATE_PROCESSING) {
                ZF_LOGE("Unknown RPC message %u", BIT_FIELD_GET(msg->mr0, RPC_MR0_OP));
                return -1;
            }
            /* try again later */
            return 0;
        }

        msg->mr0 = BIT_FIELD_SET(msg->mr0, RPC_MR0_STATE, RPC_MR0_STATE_COMPLETE);
        rpcmsg_queue_advance_head(io_proxy->rpc.rx_queue);
    }

    return 0;
}
