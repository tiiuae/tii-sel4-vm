/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stubs for handling MSIs.
 */

#pragma once

#include <tii/io_proxy.h>

int handle_msi(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg);
int msi_init(vm_t *vm);
