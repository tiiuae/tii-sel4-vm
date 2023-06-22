/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sync/sem.h>

#include <sel4vm/guest_vcpu_fault.h>

#include "ioreq.h"

#define ioreq_to_mmio(_ioreq) (&(_ioreq)->req.mmio)
#define ioreq_to_pci(_ioreq) (&(_ioreq)->req.pci)

#define mb() __sync_synchronize()
#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE);

#define ioreq_set_state(_ioreq, _state) atomic_store_release(&(_ioreq)->state, (_state))
#define ioreq_state(_ioreq) atomic_load_acquire(&(_ioreq)->state)

#define ioreq_state_complete(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_COMPLETE)
#define ioreq_state_free(_ioreq) (ioreq_state((_ioreq)) == SEL4_IOREQ_STATE_FREE)

#define ioreq_is_type(_ioreq, _type) ((_ioreq)->type == (_type))
#define ioreq_is_mmio(_ioreq) ioreq_is_type((_ioreq), SEL4_IOREQ_TYPE_MMIO)
#define ioreq_is_pci(_ioreq) ioreq_is_type((_ioreq), SEL4_IOREQ_TYPE_PCI)

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
    vka_t *vka;
} io_proxy_t;

static __thread ioack_sync_t *ioack_sync_data = NULL;

static ioack_sync_t *ioack_sync_prepare(vka_t *vka);
static ioack_result_t ioack_vcpu(ioreq_t *ioreq, void *cookie);
static ioack_result_t ioack_sync(ioreq_t *ioreq, void *cookie);

static int ioreq_pci_wait(io_proxy_t *io_proxy, int slot, uint64_t *value);

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
    struct sel4_ioreq_mmio *mmio;

    assert(io_proxy && vcpu && size >= 0 && size <= sizeof(val));

    int slot = io_proxy_next_free_slot(io_proxy);

    ioreq_t *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    ioack_t *ioack = io_proxy_slot_to_ioack(io_proxy, slot);
    if (!ioreq)
        return -1;

    mmio = ioreq_to_mmio(ioreq);
    mmio->direction = direction;
    mmio->vcpu = vcpu->vcpu_id;
    mmio->addr = offset;
    mmio->len = size;
    if (direction == SEL4_IO_DIR_WRITE) {
        memcpy(&mmio->data, &val, size);
    } else {
        mmio->data = 0;
    }

    ioack->callback = ioack_vcpu;
    ioack->cookie = vcpu;

    ioreq->type = SEL4_IOREQ_TYPE_MMIO;

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
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

int ioreq_pci_start(io_proxy_t *io_proxy, unsigned int pcidev,
                    unsigned int direction, uintptr_t offset, size_t size,
                    uint32_t value)
{
    assert(io_proxy && size >= 0 && size <= sizeof(value));

    int slot = io_proxy_next_free_slot(io_proxy);

    ioreq_t *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    ioack_t *ioack = io_proxy_slot_to_ioack(io_proxy, slot);
    if (!ioreq)
        return -1;

    struct sel4_ioreq_pci *pci = ioreq_to_pci(ioreq);
    pci->direction = direction;
    pci->pcidev = pcidev;
    pci->addr = offset;
    pci->len = size;
    if (direction == SEL4_IO_DIR_WRITE) {
        memcpy(&pci->data, &value, size);
    } else {
        pci->data = 0;
    }

    ioack->callback = ioack_sync;
    ioack->cookie = ioack_sync_prepare(io_proxy->vka);

    ioreq->type = SEL4_IOREQ_TYPE_PCI;

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

uint32_t ioreq_pci_finish(io_proxy_t *io_proxy, unsigned int slot)
{
    uint64_t data = 0;

    int err = ioreq_wait(io_proxy, slot, &data);
    if (err) {
        ZF_LOGE("ioreq_wait() failed");
        return 0;
    }

    return data;
}

io_proxy_t *io_proxy_init(void *ctrl, void *iobuf, vka_t *vka)
{
    io_proxy_t *io_proxy;
    ps_io_ops_t ops;

    io_proxy->vka = vka;

    int err = ps_new_stdlib_malloc_ops(&ops.malloc_ops);
    if (err) {
        ZF_LOGE("Failed to get malloc ops (%d)", err);
        return NULL;
    }

    err = ps_calloc(&ops.malloc_ops, 1, sizeof(*io_proxy), (void **)&io_proxy);
    if (err) {
        ZF_LOGE("Failed to allocate memory (%d)", err);
        return NULL;
    }

    io_proxy->ctrl = ctrl;
    io_proxy->iobuf = (struct sel4_iohandler_buffer *)iobuf;

    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        ioreq_set_state(io_proxy_slot_to_ioreq(io_proxy, i),
                        SEL4_IOREQ_STATE_FREE);
    }
    mb();

    return io_proxy;
}

static ioack_result_t ioack_vcpu(ioreq_t *ioreq, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;
    struct sel4_ioreq_mmio *mmio = ioreq_to_mmio(ioreq);

    if (mmio->direction == SEL4_IO_DIR_READ) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word data = 0;

        assert(mmio->len <= sizeof(data));
        memcpy(&data, &mmio->data, mmio->len);

        set_vcpu_fault_data(vcpu, data << s);
    }

    advance_vcpu_fault(vcpu);

    return IOACK_OK;
}

static ioack_result_t ioack_sync(ioreq_t *ioreq, void *cookie)
{
    ioack_sync_t *sync = cookie;
    struct sel4_ioreq_pci *pci = ioreq_to_pci(ioreq);

    if (pci->direction == SEL4_IO_DIR_READ) {
        sync->data = 0;
        memcpy(&sync->data, &pci->data, pci->len);
    }

    sync_sem_post(&sync->handoff);

    return IOACK_OK;
}

ioack_result_t ioreq_finish(io_proxy_t *io_proxy, unsigned int slot)
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

    ioack_result_t res = ioack->callback(ioreq, ioack->cookie);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return res;

}

static int ioreq_pci_wait(io_proxy_t *io_proxy, int slot, uint64_t *value)
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

    struct sel4_ioreq_pci *pci = ioreq_to_pci(ioreq);

    if (pci->direction == SEL4_IO_DIR_READ && value != NULL) {
        *value = sync->data;
    }

    return 0;
}
