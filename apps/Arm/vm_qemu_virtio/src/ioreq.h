/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023, Technology Innovation Institute
 *
 */
#pragma once

#include <sel4vm/guest_vm.h>
#include <sel4vmmplatsupport/ioports.h>

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#include "sel4_virt_types.h"

#define ioreq_slot_valid(_slot) SEL4_IOREQ_SLOT_VALID((_slot))

typedef struct io_proxy io_proxy_t;

int ioreq_mmio_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                     unsigned int direction, uintptr_t offset, size_t size,
                     uint64_t val);

int ioreq_mmio_finish(vm_t *vm, io_proxy_t *io_proxy, unsigned int slot);

int ioreq_pci_start(io_proxy_t *io_proxy, unsigned int pcidev,
                    unsigned int direction, uintptr_t offset, size_t size,
                    uint32_t value);

uint32_t ioreq_pci_finish(io_proxy_t *io_proxy, unsigned int slot);

io_proxy_t *io_proxy_init(void *ctrl, void *iobuf);
