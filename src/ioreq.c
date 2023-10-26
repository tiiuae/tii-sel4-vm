/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sync/sem.h>

#include "ioreq.h"

#define mb() __sync_synchronize()
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE);

typedef struct ioreq_native {
    int slot;
    sync_sem_t handoff;
    uint64_t value;
} ioreq_native_t;

static __thread ioreq_native_t ioreq_native_data;

static int free_native_slot = SEL4_MMIO_NATIVE_BASE;

static int ioreq_native_slot(io_proxy_t *io_proxy);
static int ioack_native_read(seL4_Word data, void *cookie);
static int ioack_native_write(seL4_Word data, void *cookie);
static int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot, seL4_Word data);

int ioreq_start(io_proxy_t *io_proxy, unsigned int slot, ioack_fn_t ioack_read,
                ioack_fn_t ioack_write, void *cookie, uint32_t addr_space,
                unsigned int direction, uintptr_t addr, size_t size,
                seL4_Word data)
{
    assert(io_proxy && size >= 0 && size <= sizeof(data));

    ioack_t *ioack = &io_proxy->ioacks[slot];

    rpc_assert(ioack->callback == NULL);

    ioack->callback = (direction == RPC_MR0_MMIO_DIRECTION_READ) ? ioack_read : ioack_write;
    ioack->cookie = cookie;

    return sel4_rpc_doorbell(device_req_mmio_start(&io_proxy->rpc, direction,
                                                   addr_space, slot, addr,
                                                   size, data));
}

static int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot, seL4_Word data)
{
    ioack_t *ioack = &io_proxy->ioacks[slot];

    rpc_assert(ioack->callback != NULL);

    int rc = ioack->callback(data, ioack->cookie);

    ioack->callback = NULL;

    return rc;
}

static int ioreq_native_slot(io_proxy_t *io_proxy)
{
    /* ioreq_native_data is in thread local storage, hence a unique ID
     * is returned for each native thread calling this.
     */
    if (!ioreq_native_data.slot) {
        return ioreq_native_data.slot;
    }

    int err = sync_sem_new(io_proxy->vka, &ioreq_native_data.handoff, 0);
    if (err) {
        ZF_LOGE("sync_sem_new() failed (%d)", err);
        return -1;
    }

    if (free_native_slot >= ARRAY_SIZE(io_proxy->ioacks)) {
        ZF_LOGE("too many native threads");
        return -1;
    }

    /* TODO: atomic read + increment? */
    ioreq_native_data.slot = free_native_slot++;

    return ioreq_native_data.slot;
}

static int ioack_native_read(seL4_Word data, void *cookie)
{
    ioreq_native_t *native_data = cookie;

    native_data->value = data;

    sync_sem_post(&native_data->handoff);

    return 0;
}

static int ioack_native_write(seL4_Word data, void *cookie)
{
    ioreq_native_t *native_data = cookie;

    sync_sem_post(&native_data->handoff);

    return 0;
}

static int ioreq_native_wait(uint64_t *value)
{
    sync_sem_wait(&ioreq_native_data.handoff);

    if (value) {
        *value = ioreq_native_data.value;
    }

    return 0;
}

int ioreq_native(io_proxy_t *io_proxy, unsigned int addr_space,
                 unsigned int direction, uintptr_t addr, size_t size,
                 uint64_t *value)
{
    int slot = ioreq_native_slot(io_proxy);
    if (slot < 0) {
        ZF_LOGE("ioreq_native_slot() failed");
        return -1;
    }

    int err = ioreq_start(io_proxy, slot, ioack_native_read,
                          ioack_native_write, &ioreq_native_data, addr_space,
                          direction, addr, size, *value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return -1;
    }

    err = ioreq_native_wait(value);
    if (err) {
        ZF_LOGE("ioreq_native_wait() failed");
        return -1;
    }

    return 0;
}

void io_proxy_wait_until_device_ready(io_proxy_t *io_proxy)
{
    volatile unsigned int *status = &io_proxy->status;
    while (*status != RPC_MR1_NOTIFY_STATUS_READY) {
        ZF_LOGI("Waiting for device ready");
        sync_sem_wait(&io_proxy->status_changed);
    };
}

int io_proxy_init(io_proxy_t *io_proxy)
{
    int err;

    err = sync_sem_new(io_proxy->vka, &io_proxy->status_changed, 0);
    if (err) {
        ZF_LOGE("sync_sem_new() failed (%d)", err);
        return -1;
    }

    return 0;
}

int handle_mmio(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    if (op != RPC_MR0_OP_MMIO) {
        return RPCMSG_RC_NONE;
    }

    unsigned int slot = BIT_FIELD_GET(msg->mr0, RPC_MR0_MMIO_SLOT);
    seL4_Word data = msg->mr2;

    int err = ioreq_finish(io_proxy, slot, data);
    if (err) {
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
}
