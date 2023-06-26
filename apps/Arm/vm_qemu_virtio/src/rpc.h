/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sel4-qemu.h"

typedef struct rpc rpc_t;

typedef int (*rpc_callback_fn_t)(rpcmsg_t *msg, void *cookie);
typedef void (*rpc_notify_fn_t)(rpc_t *rpc);

void rpc_init(rpc_t *rpc, void *rx, rpc_notify_fn_t notify);
rpc_t *rpc_new(void *rx, rpc_notify_fn_t notify);
int rpc_register_callback(rpc_t *rpc, rpc_callback_fn_t callback,
                          void *cookie);
int rpc_run(rpc_t *rpc);
void rpc_notify(rpc_t *rpc);
