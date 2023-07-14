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

static int ioreq_mmio_finish(struct sel4_ioreq *ioreq, void *cookie);
static int ioreq_pci_finish(struct sel4_ioreq *ioreq, void *cookie);

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

int ioreq_mmio_start(struct sel4_iohandler_buffer *iobuf,
                     vm_vcpu_t *vcpu, unsigned int direction,
                     uintptr_t offset, size_t size,
                     uint64_t val)
{
    struct sel4_ioreq *ioreq;
    struct sel4_ioreq_mmio *mmio;

    assert(iobuf && vcpu && size >= 0 && size <= sizeof(val));

    int slot = ioreq_next_free_slot(iobuf);

    ioreq = ioreq_slot_to_ptr(iobuf, slot);
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

    ioreq->type = SEL4_IOREQ_TYPE_MMIO;

    ioacks[slot].callback = ioreq_mmio_finish;
    ioacks[slot].cookie = vcpu;

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

int ioreq_finish(struct sel4_iohandler_buffer *iobuf, unsigned int slot)
{
    struct sel4_ioreq *ioreq;
    struct sel4_ioreq_mmio *mmio;

    assert(iobuf);

    ioreq = ioreq_slot_to_ptr(iobuf, slot);
    assert(ioreq);

    if (!ioreq_state_complete(ioreq)) {
        return -1;
    }

    int err = ioacks[slot].callback(ioreq, ioacks[slot].cookie);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return err;
}

static int ioreq_mmio_finish(struct sel4_ioreq *ioreq, void *cookie)
{
    vm_vcpu_t *vcpu = cookie;

    struct sel4_ioreq_mmio *mmio = ioreq_to_mmio(ioreq);

    if (mmio->direction == SEL4_IO_DIR_READ) {
        seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
        seL4_Word data = 0;

        assert(mmio->len <= sizeof(data));
        memcpy(&data, &mmio->data, mmio->len);

        set_vcpu_fault_data(vcpu, data << s);
        advance_vcpu_fault(vcpu);
    } else {
        advance_vcpu_fault(vcpu);
    }

    return 0;
}

int ioreq_pci_start(struct sel4_iohandler_buffer *iobuf,
                    unsigned int pcidev, unsigned int direction,
                    uintptr_t offset, size_t size,
                    uint32_t value)
{
    assert(iobuf && size >= 0 && size <= sizeof(value));

    int slot = ioreq_next_free_slot(iobuf);

    struct sel4_ioreq *ioreq = ioreq_slot_to_ptr(iobuf, slot);
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

    ioreq->type = SEL4_IOREQ_TYPE_PCI;

    if (!ioreq_sync.initialized) {
        if (sync_sem_new(&_vka, &ioreq_sync.handoff, 0)) {
            ZF_LOGF("Unable to allocate handoff semaphore");
        }
        ioreq_sync.initialized = true;
    }

    ioacks[slot].callback = ioreq_pci_finish;
    ioacks[slot].cookie = &ioreq_sync;

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

static int ioreq_pci_finish(struct sel4_ioreq *ioreq, void *cookie)
{
    ioreq_sync_t *sync = cookie;

    uint32_t data = 0;
    struct sel4_ioreq_pci *pci;

    pci = ioreq_to_pci(ioreq);

    if (pci->direction == SEL4_IO_DIR_READ) {
        memcpy(&data, &pci->data, pci->len);
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

void ioreq_init(struct sel4_iohandler_buffer *iobuf)
{
    for (unsigned i = 0; i < SEL4_MAX_IOREQS; i++) {
        ioreq_set_state(ioreq_slot_to_ptr(iobuf, i), SEL4_IOREQ_STATE_FREE);
    }
    mb();
}
