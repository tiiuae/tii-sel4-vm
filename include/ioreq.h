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
#include "sel4-qemu.h"

#define ioreq_slot_valid(_slot) SEL4_IOREQ_SLOT_VALID((_slot))

typedef struct ioack {
    int (*callback)(struct sel4_ioreq *ioreq, void *cookie);
    void *cookie;
} ioack_t;

typedef struct io_proxy {
    sync_sem_t backend_started;
    int ok_to_run;
    struct sel4_iohandler_buffer *iobuf;
    rpcmsg_queue_t *rx_queue;
    void (*backend_notify)(struct io_proxy *io_proxy);
    int (*run)(struct io_proxy *io_proxy);
    uint32_t backend_id;
    uintptr_t data_base;
    size_t data_size;
    uintptr_t ctrl_base;
    size_t ctrl_size;
    uintptr_t (*iobuf_page_get)(struct io_proxy *io_proxy, unsigned int page);
    void *dtb_buf;
    vka_t *vka;
    ioack_t ioacks[SEL4_MAX_IOREQS];
} io_proxy_t;

static inline void io_proxy_backend_notify(io_proxy_t *io_proxy)
{
    io_proxy->backend_notify(io_proxy);
}

static inline int io_proxy_run(io_proxy_t *io_proxy)
{
    return io_proxy->run(io_proxy);
}

static uintptr_t io_proxy_iobuf_page(io_proxy_t *io_proxy, unsigned int page)
{
    return io_proxy->iobuf_page_get(io_proxy, page);
}

int ioreq_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu, uint32_t addr_space,
                unsigned int direction, uintptr_t offset, size_t size,
                uint64_t val);

int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot);

int ioreq_wait(uint64_t *value);

void io_proxy_wait_for_backend(io_proxy_t *io_proxy);

void io_proxy_init(io_proxy_t *io_proxy);

int libsel4vm_io_proxy_init(vm_t *vm, io_proxy_t *io_proxy);

int rpc_run(io_proxy_t *io_proxy);
