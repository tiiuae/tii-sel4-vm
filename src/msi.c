/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stubs for handling MSIs.
 */

#include <tii/msi.h>

int WEAK handle_msi(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    return RPCMSG_RC_NONE;
}

int WEAK msi_init(vm_t *vm)
{
    return 0;
}
