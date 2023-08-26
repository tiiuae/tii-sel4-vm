/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <configurations/vm.h>

#define VM_TII_INIT_DEF() \
    VM_INIT_DEF() \
    attribute int tracebuffer_base; \
    attribute int tracebuffer_size; \
    attribute int ramoops_base; \
    attribute int ramoops_size; \
    attribute { \
        int id; \
        string base; \
        string size; \
        string mmio_base; \
        string mmio_size; \
    } vm_virtio_devices[] = []; \
    attribute { \
        int id; \
        string base; \
        string size; \
        string mmio_base; \
        string mmio_size; \
    } vm_virtio_drivers[] = []; \

#define VM_TII_CONFIGURATION_DEF(num) \
    vm##num.fs_shmem_size = 0x100000; \
    vm##num.global_endpoint_base = 1 << 27; \
    vm##num.asid_pool = true; \
    vm##num.simple = true; \
    vm##num.sem_value = 0; \
    vm##num.heap_size = 0x300000; \

#undef VM_COMPONENT_DEF
#define VM_COMPONENT_DEF(num) \
    component VM##num vm##num;

#define VIRTIO_COMPONENT_DEF(_num) \
    emits    VirtIONotify vm##_num##_ntfn_send; \
    consumes VirtIONotify vm##_num##_ntfn_recv; \
    dataport Buf(4096) vm##_num##_iobuf; \
    dataport Buf(4096) vm##_num##_memdev;

#define VIRTIO_DEVICE_COMPONENT_DEF(_num) \
    VIRTIO_COMPONENT_DEF(_num)

#define VIRTIO_DRIVER_COMPONENT_DEF(_num) \
    VIRTIO_COMPONENT_DEF(_num)

#define VIRTIO_COMPOSITION_DEF(_dev, _drv) \
    connection seL4SharedDataWithCaps vm##_dev##_vm##_drv##_iobuf(from vm##_dev.vm##_drv##_iobuf, to vm##_drv.vm##_dev##_iobuf); \
    connection seL4SharedDataWithCaps vm##_dev##_vm##_drv##_memdev(from vm##_dev.vm##_drv##_memdev, to vm##_drv.vm##_dev##_memdev); \
    connection seL4GlobalAsynch vm##_dev##_vm##_drv##_upcall(from vm##_drv.vm##_dev##_ntfn_send, to vm##_dev.vm##_drv##_ntfn_recv); \
    connection seL4Notification vm##_dev##_vm##_drv##_downcall(from vm##_dev.vm##_drv##_ntfn_send, to vm##_drv.vm##_dev##_ntfn_recv);
