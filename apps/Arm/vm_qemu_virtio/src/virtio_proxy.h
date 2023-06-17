/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

typedef struct virtio_proxy_config {
    unsigned int vcpu_id;
    unsigned int irq;
    uintptr_t pci_mmio_base;
    size_t pci_mmio_size;
} virtio_proxy_config_t;

typedef struct virtio_proxy virtio_proxy_t;

virtio_proxy_t *virtio_proxy_init(vm_t *vm, vmm_pci_space_t *pci,
                                  vka_t *vka,
                                  const virtio_proxy_config_t *config);

void rpc_handler(virtio_proxy_t *proxy);
