#pragma once

#include <configurations/vm.h>

#define VM_QEMU_INIT_DEF() \
    VM_INIT_DEF() \
    attribute int tracebuffer_base; \
    attribute int tracebuffer_size; \
    attribute int ramoops_base; \
    attribute int ramoops_size; \
    attribute int guest_large_pages = false; \
    attribute int cross_connector_large_pages = false; \
    attribute int vmid; \
    attribute { \
        int enabled = false; \
    } virtio_vm_backend_config; \
    attribute { \
        uintptr_t base; \
        size_t size; \
    } virtio_config = {}; \

#define VM_QEMU_CONFIGURATION_DEF(num) \
    vm##num.fs_shmem_size = 0x100000; \
    vm##num.global_endpoint_base = 1 << 27; \
    vm##num.asid_pool = true; \
    vm##num.simple = true; \
    vm##num.sem_value = 0; \
    vm##num.heap_size = 0x300000; \
    vm##num.vmid = num;

