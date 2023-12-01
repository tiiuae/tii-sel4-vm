/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utils/util.h>

#include <tii/guest.h>
#include <tii/io_proxy.h>

#define MAX_GRM_NODES 16

static USED SECTION("_guest_reserved_memory") guest_reserved_memory_t *grm[MAX_GRM_NODES];
static int num_grm;

int guest_reserved_memory_add(guest_reserved_memory_t *rm)
{
    if (num_grm >= ARRAY_SIZE(grm)) {
        ZF_LOGE("too many dynamic guest reserved memory nodes");
        return -1;
    }

    grm[num_grm++] = rm;

    return 0;
}

guest_reserved_memory_t *guest_reserved_memory_find(uintptr_t base, size_t size)
{
    guest_reserved_memory_t **start = __start__guest_reserved_memory;
    guest_reserved_memory_t **stop = __stop__guest_reserved_memory;

    for (guest_reserved_memory_t **ptr = start; ptr < stop; ptr++) {
        guest_reserved_memory_t *rm = (*ptr);
        if (!rm) {
            continue;
        }
        if (rm->base == base && rm->size == size) {
            return rm;
        }
    }

    return NULL;
}
