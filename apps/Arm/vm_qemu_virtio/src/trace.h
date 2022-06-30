/*
 * Copyright 2022, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define TRACE_POINT_MAX 256

#define trace(__str__) do {             \
    static unsigned int idx;            \
    if (!idx) {                         \
        idx = trace_index(__str__);     \
    }                                   \
} while (0)

/* guaranteed to always succeed, index always >= 1 */
unsigned int trace_index(const char *);

void trace_init(vm_t *vm);
void trace_start(void);
void trace_stop(void);
void trace_dump(void);
