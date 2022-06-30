/*
 * Copyright 2022, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_vm.h>

#include <sel4/benchmark_track_types.h>

#include "trace.h"

extern vka_t _vka;
extern vspace_t _vspace;

static vka_object_t kernel_trace_frame;
void *kernel_trace;
uintptr_t kernel_trace_vm;

static const char *trace_names[TRACE_POINT_MAX];
static int trace_names_count;

bool trace_started;
unsigned int benchmark_entries;

unsigned int trace_index(const char *str)
{
    for (int i = 1; i < trace_names_count; i++) {
        if (!strcmp(str, trace_names[i])) {
            return i;
        }
    }
    if (trace_names_count == TRACE_POINT_MAX) {
        ZF_LOGF("Too many trace points");
    }
    trace_names[trace_names_count++] = str;
    return trace_names_count - 1;
}

void trace_init(vm_t *vm)
{
    int error = vka_alloc_frame(&_vka, seL4_LargePageBits, &kernel_trace_frame);
    if (error) {
	    ZF_LOGF("Failed to allocate large page for kernel log");
    }
    error = seL4_BenchmarkSetLogBuffer(kernel_trace_frame.cptr);
    if (error) {
	    ZF_LOGF("Cannot set kernel log buffer");
    }
    kernel_trace = vspace_map_pages(&_vspace, &kernel_trace_frame.cptr, NULL, seL4_AllRights, 1, seL4_LargePageBits, true);
    if (!kernel_trace) {
        ZF_LOGF("Cannot map kernel log buffer to VMM vspace");
    }

    kernel_trace_vm = (uintptr_t)vspace_map_pages(&vm->mem.vm_vspace, &kernel_trace_frame.cptr, NULL, seL4_AllRights, 1, seL4_LargePageBits, true);
    if (!kernel_trace_vm) {
        ZF_LOGF("Cannot map kernel log buffer to VM vspace");
    }
}

void trace_start(void)
{
    uint64_t pmuserenr_el0;

    if (trace_started) {
        return;
    }

    asm volatile("mrs %0, pmuserenr_el0" : "=r"(pmuserenr_el0));
    if (pmuserenr_el0 & 1 == 0) {
        ZF_LOGF("PMU registers not available at EL0");
    }

    seL4_BenchmarkResetLog();

    trace_started = true;
}

void trace_stop(void)
{
     benchmark_entries = seL4_BenchmarkFinalizeLog();
     trace_started = false;
}

void trace_dump(void)
{
    seL4_DebugDumpScheduler();

    benchmark_track_kernel_entry_t *entries = kernel_trace;
    for (int i = 0; i < benchmark_entries; i++) {
        if (entries[i].entry.path == Entry_Switch) {
            printf("%"PRId64" %"PRIxPTR"\n",
                   entries[i].start_time,
                   (uintptr_t)entries[i].entry.next);
        }
    }
}
