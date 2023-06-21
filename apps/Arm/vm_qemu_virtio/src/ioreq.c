/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

typedef struct io_proxy {
    void *ctrl;
    struct sel4_iohandler_buffer *iobuf;
} io_proxy_t;

static inline struct sel4_ioreq *io_proxy_slot_to_ioreq(io_proxy_t *io_proxy,
                                                        int slot)
{
    if (!ioreq_slot_valid(slot))
        return NULL;

    return io_proxy->iobuf->request_slots + slot;
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
    struct sel4_ioreq *ioreq;
    struct sel4_ioreq_mmio *mmio;

    assert(io_proxy && vcpu && size >= 0 && size <= sizeof(val));

    int slot = io_proxy_next_free_slot(io_proxy);

    ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
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

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

int ioreq_mmio_finish(vm_t *vm, io_proxy_t *io_proxy, unsigned int slot)
{
    struct sel4_ioreq *ioreq;
    struct sel4_ioreq_mmio *mmio;

    assert(io_proxy);

    ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    assert(ioreq);

    /* io request not for us, or not complete */
    if (!ioreq_is_mmio(ioreq) || !ioreq_state_complete(ioreq)) {
        return -1;
    }

    mmio = ioreq_to_mmio(ioreq);
    vm_vcpu_t *vcpu = vm->vcpus[mmio->vcpu];
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

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return 0;
}

int ioreq_pci_start(io_proxy_t *io_proxy, unsigned int pcidev,
                    unsigned int direction, uintptr_t offset, size_t size,
                    uint32_t value)
{
    assert(io_proxy && size >= 0 && size <= sizeof(value));

    int slot = io_proxy_next_free_slot(io_proxy);

    struct sel4_ioreq *ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
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

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_PENDING);

    return slot;
}

uint32_t ioreq_pci_finish(io_proxy_t *io_proxy, unsigned int slot)
{
    uint32_t data = 0;
    struct sel4_ioreq *ioreq;
    struct sel4_ioreq_pci *pci;

    assert(io_proxy);

    ioreq = io_proxy_slot_to_ioreq(io_proxy, slot);
    assert(ioreq);

    if (!ioreq_state_complete(ioreq)) {
        ZF_LOGE("io request is not complete");
        return 0;
    }

    pci = ioreq_to_pci(ioreq);

    if (pci->direction == SEL4_IO_DIR_READ)
        memcpy(&data, &pci->data, pci->len);

    ioreq_set_state(ioreq, SEL4_IOREQ_STATE_FREE);

    return data;
}

io_proxy_t *io_proxy_init(void *ctrl, void *iobuf)
{
    io_proxy_t *io_proxy;
    ps_io_ops_t ops;

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
