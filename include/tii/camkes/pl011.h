/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

typedef struct pl011 {
    uintptr_t base;
    size_t size;
} pl011_t;

void pl011_init(vm_t *vm, void *cookie);
