/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sync/sem.h>

#include <sel4vm/guest_vcpu_fault.h>

#include <tii/io_proxy.h>
#include <tii/guest.h>

#define mb() __sync_synchronize()
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE);

#define ioreq_set_state(_ioreq, _state) atomic_store_release(&(_ioreq)->state, (_state))
#define ioreq_state(_ioreq) atomic_load_acquire(&(_ioreq)->state)

#define ioreq_state_complete(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_COMPLETE)
#define ioreq_state_free(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_FREE)

typedef struct ioreq_native {
    int slot;
    sync_sem_t handoff;
    uint64_t value;
} ioreq_native_t;

static __thread ioreq_native_t ioreq_native_data;

static int free_native_slot = SEL4_MMIO_NATIVE_BASE;

static int ioreq_native_slot(io_proxy_t *io_proxy);
static int ioreq_native_wait(uint64_t *value);
static int ioreq_native_finish(struct sel4_ioreq *ioreq, void *cookie);

static inline struct sel4_ioreq *ioreq_slot_to_ptr(struct sel4_iohandler_buffer *iobuf,
                                                   int slot)
{
    if (!ioreq_slot_valid(slot))
        return NULL;

    return iobuf->request_slots + slot;
}

int ioreq_start(io_proxy_t *io_proxy, unsigned int slot, ioack_fn_t callback,
                void *cookie, uint32_t addr_space, unsigned int direction,
                uintptr_t offset, size_t size, uint64_t val)
{
    struct sel4_ioreq *ioreq;

    assert(io_proxy && io_proxy->iobuf && size >= 0 && size <= sizeof(val));

    if (slot >= ARRAY_SIZE(io_proxy->ioacks)) {
        return -1;
    }

    ioreq = ioreq_slot_to_ptr(io_proxy->iobuf, slot);
    ioack_t *ioack = &io_proxy->ioacks[slot];
    if (!ioreq)
        return -1;

    ioreq->direction = direction;
    ioreq->addr_space = addr_space;
    ioreq->addr = offset;
    ioreq->len = size;
    if (direction == SEL4_IO_DIR_WRITE) {
        memcpy(&ioreq->data, &val, size);
    } else {
        ioreq->data = 0;
    }

    ioack->callback = callback;
    ioack->cookie = cookie;

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return 0;
}

int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot)
{
    struct sel4_ioreq *ioreq;

    assert(io_proxy && io_proxy->iobuf);

    ioreq = ioreq_slot_to_ptr(io_proxy->iobuf, slot);
    ioack_t *ioack = &io_proxy->ioacks[slot];
    assert(ioreq);

    if (!ioreq_state_complete(ioreq)) {
        return -1;
    }

    int err = ioack->callback(ioreq, ioack->cookie);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return err;
}

static int ioreq_native_slot(io_proxy_t *io_proxy)
{
    /* ioreq_native_data is in thread local storage, hence a unique ID
     * is returned for each native thread calling this.
     */
    if (ioreq_native_data.slot) {
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

static int ioreq_native_finish(struct sel4_ioreq *ioreq, void *cookie)
{
    ioreq_native_t *native_data = cookie;

    uint64_t data = 0;

    if (ioreq->direction == SEL4_IO_DIR_READ) {
        memcpy(&data, &ioreq->data, ioreq->len);
        native_data->value = data;
    }

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

    int err = ioreq_start(io_proxy, slot, ioreq_native_finish,
                          &ioreq_native_data, addr_space, direction, addr,
                          size, *value);
    if (err) {
        ZF_LOGE("ioreq_start() failed (%d)", err);
        return -1;
    }

    io_proxy_backend_notify(io_proxy);

    err = ioreq_native_wait(value);
    if (err) {
        ZF_LOGE("ioreq_native_wait() failed");
        return -1;
    }

    return 0;
}

void io_proxy_wait_for_backend(io_proxy_t *io_proxy)
{
    volatile int *ok_to_run = &io_proxy->ok_to_run;
    while (!*ok_to_run) {
        sync_sem_wait(&io_proxy->backend_started);
    };
}

void io_proxy_init(io_proxy_t *io_proxy)
{
    int err;

    err = guest_register_io_proxy(io_proxy);
    if (err) {
        ZF_LOGF("guest_register_io_proxy() failed (%d)", err);
        /* no return */
    }

    if (sync_sem_new(io_proxy->vka, &io_proxy->backend_started, 0)) {
        ZF_LOGF("Unable to allocate semaphore");
    }

    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        ioreq_set_state(ioreq_slot_to_ptr(io_proxy->iobuf, i), SEL4_IOREQ_STATE_FREE);
    }
    mb();
}
