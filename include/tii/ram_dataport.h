/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_memory.h>

typedef struct {
    seL4_CPtr *frames;
    size_t num_frames;
    size_t frame_size_bits;
    uintptr_t addr;
} ram_dataport_t;

int ram_dataport_setup(void);
int ram_dataport_map_all(vm_t *vm);
