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
    bool initialized;
    sync_sem_t handoff;
    uint64_t value;
} ioreq_native_t;

static __thread ioreq_native_t ioreq_native_data;

static ioreq_native_t *ioreq_native_prepare(vka_t *vka);
static int ioreq_native_wait(uint64_t *value);
static int ioreq_vcpu_finish(struct sel4_ioreq *ioreq, void *cookie);
static int ioreq_native_finish(struct sel4_ioreq *ioreq, void *cookie);

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

    if (vcpu) {
        ioack->callback = ioreq_vcpu_finish;
        ioack->cookie = vcpu;
    } else {
        ioack->callback = ioreq_native_finish;
        ioack->cookie = ioreq_native_prepare(io_proxy->vka);
    }

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

static ioreq_native_t *ioreq_native_prepare(vka_t *vka)
{
    if (!ioreq_native_data.initialized) {
        if (sync_sem_new(vka, &ioreq_native_data.handoff, 0)) {
            ZF_LOGF("Unable to allocate handoff semaphore");
        }
        ioreq_native_data.initialized = true;
    }

    return &ioreq_native_data;
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
    int err = ioreq_start(io_proxy, VCPU_NONE, addr_space, direction, addr,
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
