/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023, Technology Innovation Institute
 *
 */
#pragma once

#include <sel4vm/guest_vm.h>
#include <sel4vmmplatsupport/ioports.h>

#define VCPU_NONE NULL

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#include "sel4_virt_types.h"

#define ioreq_slot_valid(_slot) SEL4_IOREQ_SLOT_VALID((_slot))

int ioreq_start(struct sel4_iohandler_buffer *iobuf, vm_vcpu_t *vcpu,
                uint32_t addr_space, unsigned int direction,
                uintptr_t offset, size_t size,
                uint64_t val);

int ioreq_finish(struct sel4_iohandler_buffer *iobuf, unsigned int slot);

int ioreq_wait(uint64_t *value);

void ioreq_init(struct sel4_iohandler_buffer *iobuf);

