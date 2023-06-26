/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "rpc.h"

typedef struct io_proxy io_proxy_t;

int io_proxy_init(io_proxy_t *io_proxy, void *iobuf, vka_t *vka, rpc_t *rpc);
io_proxy_t *io_proxy_new(void *iobuf, vka_t *vka, rpc_t *rpc);

int mmio_proxy_create(io_proxy_t *io_proxy, vm_t *vm, uintptr_t base,
                      size_t size);
