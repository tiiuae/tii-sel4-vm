/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

typedef struct vmm_pci_space vmm_pci_space_t;

typedef struct virtio_proxy_config {
    unsigned int vcpu_id;
    unsigned int irq;
    uintptr_t pci_mmio_base;
    size_t pci_mmio_size;
} virtio_proxy_config_t;

typedef struct virtio_proxy virtio_proxy_t;

int virtio_proxy_init(virtio_proxy_t *virtio_proxy, vm_t *vm,
                      vmm_pci_space_t *pci, vka_t *vka, rpc_t *rpc,
                      io_proxy_t *io_proxy,
                      const virtio_proxy_config_t *config);

virtio_proxy_t *virtio_proxy_new(vm_t *vm, vmm_pci_space_t *pci, vka_t *vka,
                                 rpc_t *rpc, io_proxy_t *io_proxy,
                                 const virtio_proxy_config_t *config);
