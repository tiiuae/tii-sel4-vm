/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sync/sem.h>

#include <sel4vm/guest_vcpu_fault.h>

#include "ioreq.h"

#define ADDR_SPACE_GLOBAL               ~0
#define ADDR_SPACE_PCI_CFG(__pcidev)    (__pcidev)

#define mb() __sync_synchronize()
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE);

#define ioreq_set_state(_ioreq, _state) atomic_store_release(&(_ioreq)->state, (_state))
#define ioreq_state(_ioreq) atomic_load_acquire(&(_ioreq)->state)

#define ioreq_state_complete(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_COMPLETE)
#define ioreq_state_free(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_FREE)

typedef ioack_result_t (*ioack_callback_t)(ioreq_t *, void *);

typedef struct ioack {
    ioack_callback_t callback;
    void *cookie;
} ioack_t;

typedef struct {
    sync_sem_t handoff;
    uint64_t data;
} ioack_sync_t;

typedef struct io_proxy {
    void *ctrl;
    ioack_t ioacks[SEL4_MAX_IOREQS];
    struct sel4_iohandler_buffer *iobuf;
    rpcmsg_queue_t *rx_queue;
    vka_t *vka;
    rpc_callback_t rpc_callback;
    void *rpc_cookie;
} io_proxy_t;

static __thread ioack_sync_t *ioack_sync_data = NULL;

static ioack_sync_t *ioack_sync_prepare(vka_t *vka);
static ioack_result_t ioack_vcpu(ioreq_t *ioreq, void *cookie);
static ioack_result_t ioack_sync(ioreq_t *ioreq, void *cookie);

static inline ioreq_t *io_proxy_slot_to_ioreq(io_proxy_t *io_proxy, int slot)
{
    if (!ioreq_slot_valid(slot))
        return NULL;

    return io_proxy->iobuf->request_slots + slot;
}

static inline ioack_t *io_proxy_slot_to_ioack(io_proxy_t *io_proxy, int slot)
{
    if (!ioreq_slot_valid(slot)) {
        return NULL;
    }

    return io_proxy->ioacks + slot;
}

static int io_proxy_next_free_slot(io_proxy_t *io_proxy)
{
    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        if (ioreq_state_free(io_proxy_slot_to_ioreq(io_proxy, i)))
            return i;
    }

    return -1;
}

int ioreq_mmio_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                     unsigned int direction, uintptr_t offset, size_t size,
                     uint64_t val)
{
    int slot;

    slot = io_proxy_start(io_proxy, vcpu, ADDR_SPACE_GLOBAL, direction,
                          offset, size, val);

    return (slot < 0) ? -1 : 0;
}

static int io_proxy_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                           uint32_t addr_space, unsigned int direction,
                           uintptr_t offset, size_t size, uint64_t val)
{
    if (!io_proxy) {
        return -1;
    }

    int slot = io_proxy_next_free_slot(io_proxy);
    if (slot < 0) {
        return -1;
    }

    ioreq_t *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    ioack_t *ioack = io_proxy_slot_to_ioack(io_proxy, slot);

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
        ioack->callback = ioack_vcpu;
        ioack->cookie = vcpu;
    } else {
        ioack->callback = ioack_sync;
        ioack->cookie = ioack_sync_prepare(io_proxy->vka);
    }

    if (!ioack->callback || !ioack->cookie) {
        return -1;
    }

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

int ioreq_pci_start(io_proxy_t *io_proxy, vm_vcpu_t *vcpu,
                    unsigned int pcidev, unsigned int direction,
                    uintptr_t offset, size_t size, uint32_t value)
{
    int slot;

    slot = io_proxy_start(io_proxy, vcpu, ADDR_SPACE_PCI_CFG(pcidev),
                          direction, offset, size, value);

    return (slot < 0) ? -1 : 0;
}

int ioreq_wait(io_proxy_t *io_proxy, int slot, uint64_t *value)
{
    if (!io_proxy) {
        return -1;
    }

    ioreq_t *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    ioack_t *ioack = io_proxy_slot_to_ioack(io_proxy, slot);

    if (!ioreq || !ioack || !ioack->callback) {
        return -1;
    }

    if (ioack->callback != ioack_sync) {
        return 0;
    }

    ioack_sync_t *sync = (ioack_sync_t *)ioack->cookie;
    sync_sem_wait(&sync->handoff);

    if (ioreq->direction == SEL4_IO_DIR_READ && value != NULL) {
        *value = sync->data;
    }

    return 0;
}

/* Since ioack_sync_data is per-thread structure, the thread using it
 * and the thread calling io_proxy_init() are not necessarily the
 * same.
 */
static ioack_sync_t *ioack_sync_prepare(vka_t *vka)
{
    if (ioack_sync_data) {
        return ioack_sync_data;
    }

    ioack_sync_data = (ioack_sync_t *)calloc(1, sizeof(*ioack_sync_data));
    if (!ioack_sync_data) {
        ZF_LOGE("Failed to allocate memory");
        return NULL;
    }

    int err = sync_sem_new(vka, &ioack_sync_data->handoff, 0);
    if (err) {
        ZF_LOGE("Unable to allocate handoff semaphore (%d)", err);
        return NULL;
    }

    return ioack_sync_data;
}

static ioack_result_t ioreq_finish(io_proxy_t *io_proxy, unsigned int slot)
{
    if (!io_proxy) {
        return IOACK_ERROR;
    }

    ioreq_t *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    ioack_t *ioack = io_proxy_slot_to_ioack(io_proxy, slot);

    if (!ioreq || !ioack || !ioack->callback) {
        return IOACK_ERROR;
    }

    if (!ioreq_state_complete(ioreq)) {
        return IOACK_ERROR;
    }

    ioack_result_t res = ioack->callback(ioack->cookie);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return res;
}

io_proxy_t *io_proxy_init(void *ctrl, void *iobuf, vka_t *vka,
                          rpc_callback_t rpc_callback,
                          void *rpc_cookie)
{
    io_proxy_t *io_proxy = calloc(1, sizeof(*io_proxy));
    if (!io_proxy) {
        ZF_LOGE("Failed to allocate memory");
        return NULL;
    }

    io_proxy->vka = vka;
    io_proxy->ctrl = ctrl;
    io_proxy->iobuf = (struct sel4_iohandler_buffer *)iobuf;

    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        ioreq_set_state(io_proxy_slot_to_ioreq(io_proxy, i),
                        SEL4_IOREQ_STATE_FREE);
    }

    io_proxy->rpc_callback = rpc_callback;
    io_proxy->rpc_cookie = rpc_cookie;

    io_proxy->rx_queue = (((rpcmsg_queue_t *) io_proxy->ctrl) + 1);

    mb();

    return io_proxy;
}

static ioack_result_t ioack_vcpu(ioreq_t *ioreq, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    if (ioreq->direction == SEL4_IO_DIR_READ) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word data = 0;

        assert(ioreq->len <= sizeof(data));
        memcpy(&data, &ioreq->data, ioreq->len);

        set_vcpu_fault_data(vcpu, data << s);
    }

    advance_vcpu_fault(vcpu);

    return IOACK_OK;
}

static ioack_result_t ioack_sync(ioreq_t *ioreq, void *cookie)
{
    ioack_sync_t *sync = cookie;

    if (ioreq->direction == SEL4_IO_DIR_READ) {
        sync->data = 0;
        memcpy(&sync->data, &ioreq->data, ioreq->len);
    }

    sync_sem_post(&sync->handoff);

    return IOACK_OK;
}

void io_proxy_process(io_proxy_t *io_proxy)
{
    rpcmsg_queue_t *q = io_proxy->rx_queue;

    for (;;) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            break;
        }

        ioack_result_t result;
        if (QEMU_OP(msg->mr.mr0) == QEMU_OP_IO_HANDLED) {
            result = ioreq_finish(io_proxy, msg->mr.mr1);
        } else {
            result = io_proxy->rpc_callback(msg, io_proxy->rpc_cookie);
        }

        if (result == IOACK_ERROR) {
            ZF_LOGF("IO acknowledge error, no point continuing");
            /* no return */
        }

        rpcmsg_queue_advance_head(q);
    }
}
