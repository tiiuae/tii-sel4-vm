/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tii/io_proxy.h>

int handle_emudev(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg);
int emudev_init(vm_t *vm, memory_fault_callback_fn fault_callback_fn);
