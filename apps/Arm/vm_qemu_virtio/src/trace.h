/*
 * Copyright 2022, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define TRACE_POINT_MAX 256
#define ARG_UNUSED(x) (void)(x)

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES

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

#else

#define trace(__str__)
static inline unsigned int trace_index(const char *str) { ARG_UNUSED(str); }
static inline void trace_init(vm_t *vm) { ARG_UNUSED(vm); ZF_LOGI("seL4 tracing is not supported by configuration"); }
static inline void trace_start(void) {}
static inline void trace_stop(void) {}
static inline void trace_dump(void) {}

#endif /* CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES */
