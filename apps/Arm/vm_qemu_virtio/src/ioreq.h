/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023, Technology Innovation Institute
 *
 */
#pragma once

#include <sel4vm/guest_vm.h>

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#include "io_proxy.h"
#include "rpc.h"
#include "sel4-qemu.h"
#include "sel4_virt_types.h"

#define ioreq_slot_valid(_slot) SEL4_IOREQ_SLOT_VALID((_slot))

typedef enum {
    IOACK_OK,
    IOACK_ERROR,
} ioack_result_t;

typedef ioack_result_t (*rpc_callback_t)(rpcmsg_t *, void *);

typedef struct vka vka_t;

/*********************** VMM-side declarations begin *************************/

int ioreq_mmio_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                     unsigned int direction, uintptr_t offset, size_t size,
                     uint64_t val);

int ioreq_pci_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                    unsigned int pcidev, unsigned int direction,
                    uintptr_t offset, size_t size, uint32_t value);

int ioreq_wait(io_proxy_t *io_proxy, int slot, uint64_t *value);

/************************ VMM-side declarations end **************************/
