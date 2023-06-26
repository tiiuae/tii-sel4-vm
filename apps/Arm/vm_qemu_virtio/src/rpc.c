/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rpc.h"

typedef struct rpc_callback {
    struct rpc_callback_t next;
    rpc_callback_fn_t callback;
    void *cookie;
} rpc_callback_t;

typedef struct rpc {
    rpcmsg_queue_t *rx_queue;
    rpc_callback_t callbacks;
    rpc_notify_fn_t notify;
} rpc_t;

void rpc_init(rpc_t *rpc, void *rx, rpc_notify_fn_t notify)
{
    rpc->rx_queue = rx;
    rpc->notify = notify;
}

rpc_t *rpc_new(void *rx, rpc_notify_fn_t notify)
{
    rpc_t *rpc = calloc(1, sizeof(*rpc));

    if (rpc) {
        int err = rpc_init(rpc, rx, notify);
        if (err) {
            return NULL;
        }
    }

    return rpc;
}

int rpc_register_callback(rpc_t *rpc, rpc_callback_fn_t callback, void *cookie)
{
    rpc_callback_t *cb = calloc(1, sizeof(*cb));
    if (!cb) {
        return -1;
    }

    cb->callback = callback;
    cb->cookie = cookie;

    rpc_callback_t p = rpc->callbacks;
    while (p && p->next) {
        p = p->next;
    }
    if (p) {
        p->next = cb;
    } else {
        rpc->callbacks = cb;
    }
    
    return 0;
}

int rpc_run(rpc_t *rpc)
{
    rpcmsg_queue_t *q = rpc->rx_queue;

    for (;;) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            break;
        }

        int result = 0;
        for (rpc_callback_t cb = rpc->callbacks; cb && !result; cb = cb->next) {
            result = cb->callback(msg, cb->cookie);
        }

        if (result < 0) {
            ZF_LOGE("RPC callback returns error %d", result);
            return result;
        } else if (result == 0) {
            ZF_LOGE("Unhandled RPC message");
            return -1;
        }

        rpcmsg_queue_advance_head(q);
    }

    return 0;
}

void rpc_notify(rpc_t *rpc)
{
    rpc->notify(rpc);
}
