/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>
#include <stddef.h>

extern uintptr_t guest_ram_base;
extern size_t guest_ram_size;

/* guest-specific implementations of these functions must be given */
int guest_configure(void *cookie);
