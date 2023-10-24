/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2023, Technology Innovation Institute
 *
 */
#pragma once

#include <sync/sem.h>

#include <sel4vm/guest_vm.h>
#include <sel4vmmplatsupport/ioports.h>

#define RPCMSG_RC_ERROR -1
#define RPCMSG_RC_NONE 0
#define RPCMSG_RC_HANDLED 1

#define VCPU_NONE NULL

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

#include "sel4_virt_types.h"

typedef int (*ioack_fn_t)(seL4_Word data, void *cookie);

typedef struct ioack {
    ioack_fn_t callback;
    void *cookie;
} ioack_t;

typedef struct io_proxy {
    sync_sem_t status_changed;
    unsigned int status;
    sel4_rpc_t rpc;
    int (*run)(struct io_proxy *io_proxy);
    uintptr_t data_base;
    size_t data_size;
    uintptr_t ctrl_base;
    size_t ctrl_size;
    uintptr_t (*iobuf_page_get)(struct io_proxy *io_proxy, unsigned int page);
    void *dtb_buf;
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

int ioreq_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu, uint32_t addr_space,
                unsigned int direction, uintptr_t addr, size_t size,
                seL4_Word data);

int ioreq_native(io_proxy_t *io_proxy, unsigned int addr_space,
                 unsigned int direction, uintptr_t offset, size_t len,
                 uint64_t *value);

void io_proxy_wait_until_device_ready(io_proxy_t *io_proxy);

int io_proxy_init(io_proxy_t *io_proxy);

int libsel4vm_io_proxy_init(vm_t *vm, io_proxy_t *io_proxy);

int rpc_run(io_proxy_t *io_proxy);

int handle_mmio(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg);
