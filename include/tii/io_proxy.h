/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023, Technology Innovation Institute
 *
 */
#pragma once

#include <sync/sem.h>

#include <sel4vm/guest_vm.h>
#include <sel4vmmplatsupport/ioports.h>

#define VCPU_NONE NULL

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#include "sel4_virt_types.h"

#include <tii/guest.h>

#define ioreq_slot_valid(_slot) SEL4_IOREQ_SLOT_VALID((_slot))

typedef int (*ioack_fn_t)(seL4_Word data, void *cookie);

typedef struct ioack {
    ioack_fn_t callback;
    void *cookie;
} ioack_t;

typedef struct io_proxy {
    sync_sem_t backend_started;
    int ok_to_run;
    struct sel4_iohandler_buffer *iobuf;
    sel4_rpc_t rpc;
    int (*run)(struct io_proxy *io_proxy);
    guest_reserved_memory_t *data_plane;
    uintptr_t ctrl_base;
    size_t ctrl_size;
    uintptr_t (*iobuf_page_get)(struct io_proxy *io_proxy, unsigned int page);
    vka_t *vka;
    ioack_t ioacks[SEL4_MMIO_MAX_VCPU + SEL4_MMIO_MAX_NATIVE];
} io_proxy_t;

static inline int io_proxy_run(io_proxy_t *io_proxy)
{
    return io_proxy->run(io_proxy);
}

static uintptr_t io_proxy_iobuf_page(io_proxy_t *io_proxy, unsigned int page)
{
    return io_proxy->iobuf_page_get(io_proxy, page);
}

int ioreq_start(io_proxy_t *io_proxy, unsigned int slot, ioack_fn_t ioack_read,
                ioack_fn_t ioack_write, void *cookie, uint32_t addr_space,
                unsigned int direction, uintptr_t addr, size_t size,
                seL4_Word val);

int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot);

int ioreq_native(io_proxy_t *io_proxy, unsigned int addr_space,
                 unsigned int direction, uintptr_t offset, size_t len,
                 uint64_t *value);

void io_proxy_wait_for_backend(io_proxy_t *io_proxy);

void io_proxy_init(io_proxy_t *io_proxy);

int libsel4vm_io_proxy_init(vm_t *vm, io_proxy_t *io_proxy);

int rpc_run(io_proxy_t *io_proxy);
