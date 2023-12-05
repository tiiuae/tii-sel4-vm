/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_vm.h>

#include <sel4/benchmark_track_types.h>

#include <vmlinux.h>

#include <tii/trace.h>

const int __attribute__((weak)) tracebuffer_base;
const int __attribute__((weak)) tracebuffer_size;
const int __attribute__((weak)) ramoops_base;
const int __attribute__((weak)) ramoops_size;

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES

extern vka_t _vka;
extern vspace_t _vspace;

static vka_object_t kernel_trace_frame, __kernel_trace_frame;
void *kernel_trace, *__kernel_trace;
void *kernel_trace_vm, *__kernel_trace_vm;

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

static inline int vka_cnode_copy(seL4_CPtr slot_src, seL4_CPtr slot_dst, seL4_CapRights_t rights)
{
    cspacepath_t src_path, dest_path;

    vka_cspace_make_path(&_vka, slot_src, &src_path);
    vka_cspace_make_path(&_vka, slot_dst, &dest_path);

    return seL4_CNode_Copy(
               /* _service */      dest_path.root,
               /* dest_index */    dest_path.capPtr,
               /* destDepth */     dest_path.capDepth,
               /* src_root */      src_path.root,
               /* src_index */     src_path.capPtr,
               /* src_depth */     src_path.capDepth,
               /* rights */        rights
           );
}

void trace_init_shared_mem(vm_t *vm, const char *name,
		const uint64_t mem_addr, const uint64_t mem_size,
		vka_object_t *local_mem, void **vm_memory, void **vmm_memory)
{
    /* virtual address to use on VM side,
     * otherwise it will allocate @0x10000000
     * and conflicts with VM's memory
     */
    void *vaddr = (void *)mem_addr;

    /* Allocate memory  */
    int error = vka_alloc_frame(&_vka, mem_size, local_mem);
    if (error) {
	    ZF_LOGF("%s: failed to allocate pages for kernel log, size: 0x%lx", name, mem_size);
    }
    ZF_LOGE("%s: allocated page @0x%lx", name, local_mem->cptr);

    /* Reserve VM virtual memory mem_size@vaddr */
    reservation_t reservation =  vspace_reserve_range_at(&vm->mem.vm_vspace, vaddr, BIT(mem_size), seL4_AllRights, true);
    assert(reservation.res);
    ZF_LOGE("%s: reserved vaddr: 0x%lx", name, (unsigned long)vaddr);

    /* Map page to the VM's vspace */
    error = vspace_map_pages_at_vaddr(&vm->mem.vm_vspace, &local_mem->cptr, NULL, vaddr, 1, mem_size, reservation);
    assert(error == seL4_NoError);
    ZF_LOGE("%s: frame @0x%lx mapped to VM-> @0x%lx", name, local_mem->cptr, (unsigned long)vaddr);
    *vm_memory = vaddr;

    /* Duplicate the cap */
    seL4_CPtr slot;
    error = vka_cspace_alloc(&_vka, &slot);
    assert(!error);

    error = vka_cnode_copy(local_mem->cptr, slot, seL4_AllRights);
    assert(error == seL4_NoError);

    /* Maps page to VMM's vspace */
    *vmm_memory = vspace_map_pages(&_vspace, &slot, NULL, seL4_AllRights, 1, mem_size, true);
    assert(*vmm_memory);
    ZF_LOGE("%s: frame @0x%lx mapped to VMM-> @0x%lx\n", name, local_mem->cptr, (unsigned long)*vmm_memory);
}

static int fdt_generate_trace_nodes(void *gen_fdt)
{
    int err;

    if (tracebuffer_base && tracebuffer_size) {
        err = fdt_generate_reserved_node(gen_fdt, "sel4_tracebuffer",
                                         "sel4_tracebuffer", tracebuffer_base,
                                         tracebuffer_size, NULL);
        if (err < 0) {
            return err;
        }
    }

    if (ramoops_base && ramoops_size) {
        err = fdt_generate_reserved_node(gen_fdt, "ramoops", "ramoops",
                                         ramoops_base, ramoops_size, NULL);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static void trace_init(vm_t *vm, void *cookie)
{
    if (&ramoops_base && &ramoops_size && ramoops_base && ramoops_size) {
        const uint64_t pstore_mem_size_bits = seL4_LargePageBits;

        if (ramoops_size != BIT(seL4_LargePageBits))
             ZF_LOGF("For now only 2M (seL4_LargePageBits) size supported. ramoops_size: 0x%x", ramoops_size);

        trace_init_shared_mem(vm, "pstore_mem", ramoops_base, pstore_mem_size_bits,
                &__kernel_trace_frame, &__kernel_trace_vm, &__kernel_trace);
    }

    if (&tracebuffer_base && &tracebuffer_size && tracebuffer_base && tracebuffer_size) {
        const uint64_t sel4buf_mem_size_bits = seL4_LargePageBits;

        if (tracebuffer_size != BIT(seL4_LargePageBits))
             ZF_LOGF("For now only 2M (seL4_LargePageBits) size supported. tracebuffer_size: 0x%x", tracebuffer_size);

        trace_init_shared_mem(vm, "sel4buf_mem", tracebuffer_base, sel4buf_mem_size_bits,
                &kernel_trace_frame, &kernel_trace_vm, &kernel_trace);

        int error = seL4_BenchmarkSetLogBuffer(kernel_trace_frame.cptr);
        if (error) {
            ZF_LOGF("Cannot set kernel log buffer");
        }
    }

    int err = fdt_generate_trace_nodes(gen_dtb_buf);
    if (err) {
        ZF_LOGF("Cannot generate DT nodes for trace: %d", err);
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

DEFINE_MODULE(trace, NULL, trace_init)
#endif /* CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES */
