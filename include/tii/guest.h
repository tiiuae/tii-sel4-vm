/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <inttypes.h>

extern uintptr_t guest_ram_base;
extern size_t guest_ram_size;

typedef struct io_proxy io_proxy_t;

typedef struct guest_reserved_memory {
    uintptr_t base;
    size_t size;
} guest_reserved_memory_t;

#define GUEST_RESERVED_MEMORY(_prefix) GUEST_RESERVED_MEMORY_ ## _prefix

#define DEFINE_GUEST_RESERVED_MEMORY(_name, _ptr) \
    __attribute__((used)) __attribute__((section("_guest_reserved_memory"))) guest_reserved_memory_t *GUEST_RESERVED_MEMORY(_name) = (_ptr);

extern guest_reserved_memory_t *__start__guest_reserved_memory[];
extern guest_reserved_memory_t *__stop__guest_reserved_memory[];

int guest_reserved_memory_add(guest_reserved_memory_t *rm);
guest_reserved_memory_t *guest_reserved_memory_find(uintptr_t base, size_t size);

/* guest-specific implementations of these functions must be given */
int guest_register_io_proxy(io_proxy_t *io_proxy);
int guest_configure(void *cookie);
