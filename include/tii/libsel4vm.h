/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

typedef struct vm vm_t;
typedef struct io_proxy io_proxy_t;

int libsel4vm_fault_handler_setup(io_proxy_t *io_proxy, vm_t *vm);
int libsel4vm_pci_setup(io_proxy_t *io_proxy, vm_t *vm);

int libsel4vm_io_proxy_init(io_proxy_t *io_proxy, vm_t *vm);
