/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sync/sem.h>

#include <sel4vm/guest_vcpu_fault.h>

#include "ioreq.h"

#define mb() __sync_synchronize()
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE);

#define ioreq_set_state(_ioreq, _state) atomic_store_release(&(_ioreq)->state, (_state))
#define ioreq_state(_ioreq) atomic_load_acquire(&(_ioreq)->state)

#define ioreq_state_complete(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_COMPLETE)
#define ioreq_state_free(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_FREE)

typedef struct ioack {
    int (*callback)(struct sel4_ioreq *ioreq, void *cookie);
    void *cookie;
} ioack_t;

typedef struct ioreq_sync {
    bool initialized;
    sync_sem_t handoff;
    uint64_t value;
} ioreq_sync_t;

static __thread ioreq_sync_t ioreq_sync;
static ioack_t ioacks[SEL4_MAX_IOREQS];

extern vka_t _vka;

static ioreq_sync_t *ioreq_sync_prepare(void);
static int ioreq_vcpu_finish(struct sel4_ioreq *ioreq, void *cookie);
static int ioreq_sync_finish(struct sel4_ioreq *ioreq, void *cookie);

static inline struct sel4_ioreq *ioreq_slot_to_ptr(struct sel4_iohandler_buffer *iobuf,
                                                   int slot)
{
    if (!ioreq_slot_valid(slot))
        return NULL;

    return iobuf->request_slots + slot;
}

static int ioreq_next_free_slot(struct sel4_iohandler_buffer *iobuf)
{
    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        if (ioreq_state_free(ioreq_slot_to_ptr(iobuf, i)))
            return i;
    }

    return -1;
}

int ioreq_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu, uint32_t addr_space,
                unsigned int direction, uintptr_t offset, size_t size,
                uint64_t val)
{
    struct sel4_ioreq *ioreq;

    assert(io_proxy && io_proxy->iobuf && size >= 0 && size <= sizeof(val));

    int slot = ioreq_next_free_slot(io_proxy->iobuf);

    ioreq = ioreq_slot_to_ptr(io_proxy->iobuf, slot);
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

    if (vcpu) {
        ioacks[slot].callback = ioreq_vcpu_finish;
        ioacks[slot].cookie = vcpu;
    } else {
        ioacks[slot].callback = ioreq_sync_finish;
        ioacks[slot].cookie = ioreq_sync_prepare();
    }

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

int ioreq_finish(io_proxy_t *io_proxy, unsigned int slot)
{
    struct sel4_ioreq *ioreq;

    assert(io_proxy && io_proxy->iobuf);

    ioreq = ioreq_slot_to_ptr(io_proxy->iobuf, slot);
    assert(ioreq);

    if (!ioreq_state_complete(ioreq)) {
        return -1;
    }

    int err = ioacks[slot].callback(ioreq, ioacks[slot].cookie);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return err;
}

static int ioreq_vcpu_finish(struct sel4_ioreq *ioreq, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    if (ioreq->direction == SEL4_IO_DIR_READ) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word data = 0;

        assert(ioreq->len <= sizeof(data));
        memcpy(&data, &ioreq->data, ioreq->len);

        set_vcpu_fault_data(vcpu, data << s);
        advance_vcpu_fault(vcpu);
    } else {
        advance_vcpu_fault(vcpu);
    }

    return 0;
}

static ioreq_sync_t *ioreq_sync_prepare(void)
{
    if (!ioreq_sync.initialized) {
        if (sync_sem_new(&_vka, &ioreq_sync.handoff, 0)) {
            ZF_LOGF("Unable to allocate handoff semaphore");
        }
        ioreq_sync.initialized = true;
    }

    return &ioreq_sync;
}

static int ioreq_sync_finish(struct sel4_ioreq *ioreq, void *cookie)
{
    ioreq_sync_t *sync = cookie;

    uint32_t data = 0;

    if (ioreq->direction == SEL4_IO_DIR_READ) {
        memcpy(&data, &ioreq->data, ioreq->len);
        sync->value = data;
    }

    sync_sem_post(&sync->handoff);

    return 0;
}

int ioreq_wait(uint64_t *value)
{
    sync_sem_wait(&ioreq_sync.handoff);

    if (value) {
        *value = ioreq_sync.value;
    }

    return 0;
}

void io_proxy_init(io_proxy_t *io_proxy)
{
    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        ioreq_set_state(ioreq_slot_to_ptr(io_proxy->iobuf, i), SEL4_IOREQ_STATE_FREE);
    }
    mb();
}
