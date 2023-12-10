/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler_attributes.h>
#else
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <inttypes.h>

#define __maybe_unused __attribute__ ((unused))
#endif

typedef unsigned long seL4_Word;

#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE)

#if defined(__KERNEL__)
#define rpc_assert(_cond) BUG_ON(!(_cond))
#else
#define rpc_assert(_cond) assert(_cond)
#endif

#define IOBUF_PAGE_DRIVER_RX    1
#define IOBUF_PAGE_DRIVER_TX    0

#define IOBUF_PAGE_DEVICE_RX    IOBUF_PAGE_DRIVER_TX
#define IOBUF_PAGE_DEVICE_TX    IOBUF_PAGE_DRIVER_RX

#define iobuf_page(_iobuf, _page) (((uintptr_t)(_iobuf)) + (4096 * (_page)))

#define driver_tx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DRIVER_TX))
#define driver_rx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DRIVER_RX))

#define device_tx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DEVICE_TX))
#define device_rx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DEVICE_RX))

#ifndef MASK
#define MASK(_n)                        ((1UL << (_n)) - 1)
#endif

#define BIT_FIELD_MASK(_name)           (MASK(_name ## _WIDTH) << (_name ## _SHIFT))

#define BIT_FIELD_CLR_ALL(_v, _name)    ((_v) & ~BIT_FIELD_MASK(_name))
#define BIT_FIELD_SET_ALL(_v, _name)    ((_v) | BIT_FIELD_MASK(_name))

#define BIT_FIELD_GET(_v, _name)        (((_v) >> (_name ## _SHIFT)) & MASK(_name ## _WIDTH))
#define BIT_FIELD_SET(_v, _name, _n)    ((BIT_FIELD_CLR_ALL(_v, _name)) | (((_n) << (_name ## _SHIFT)) & BIT_FIELD_MASK(_name)))

#define RPCMSG_STATE_COMPLETE           0
#define RPCMSG_STATE_RESERVED           1
#define RPCMSG_STATE_PENDING            2
#define RPCMSG_STATE_PROCESSING         3

#define RPC_MR0_OP_WIDTH                6
#define RPC_MR0_OP_SHIFT                0

#define RPC_MR0_COMMON_WIDTH            (RPC_MR0_OP_WIDTH + RPC_MR0_OP_SHIFT)
#define RPC_MR0_COMMON_SHIFT            0

/************************** defines for RPC_MR0_OP ***************************/

/* start/finish MMIO access */
#define RPC_MR0_OP_MMIO                 0

/* from device to driver */
#define RPC_MR0_OP_SET_IRQ              16
#define RPC_MR0_OP_CLR_IRQ              17
#define RPC_MR0_OP_START_VM             18
#define RPC_MR0_OP_REGISTER_PCI_DEV     19

/************************ defines for RPC_MR0_OP_MMIO ************************/
/*
 *     33222222222211111111110000000000
 *     10987654321098765432109876543210
 *
 * MR0 .......LLLLAAAAAAAADSSSSSS......
 * MR1             address
 * MR2               data
 *
 * MR0 fields:
 *
 *   L is MMIO request length
 *   A is address space identifier (all ones is global, otherwise PCI device)
 *   D is direction (read=0, write=1)
 *   S is slot number (used internally at driver side)
 *
 * Write acknowledgements have meaningful value for slot field only. For read
 * requests, the data field is also defined.
 */

#define RPC_MR0_MMIO_SLOT_WIDTH         6
#define RPC_MR0_MMIO_SLOT_SHIFT         (RPC_MR0_COMMON_WIDTH + RPC_MR0_COMMON_SHIFT)

#define RPC_MR0_MMIO_DIRECTION_WIDTH    1
#define RPC_MR0_MMIO_DIRECTION_SHIFT    (RPC_MR0_MMIO_SLOT_WIDTH + RPC_MR0_MMIO_SLOT_SHIFT)

#define RPC_MR0_MMIO_DIRECTION_READ     0
#define RPC_MR0_MMIO_DIRECTION_WRITE    1

#define RPC_MR0_MMIO_ADDR_SPACE_WIDTH   8
#define RPC_MR0_MMIO_ADDR_SPACE_SHIFT   (RPC_MR0_MMIO_DIRECTION_WIDTH + RPC_MR0_MMIO_DIRECTION_SHIFT)

#define RPC_MR0_MMIO_LENGTH_WIDTH       4
#define RPC_MR0_MMIO_LENGTH_SHIFT       (RPC_MR0_MMIO_ADDR_SPACE_WIDTH + RPC_MR0_MMIO_ADDR_SPACE_SHIFT)

/*****************************************************************************/

#define RPCMSG_BUFFER_SIZE  32

typedef struct {
    seL4_Word mr0;
    seL4_Word mr1;
    seL4_Word mr2;
    seL4_Word mr3;
} rpcmsg_t;

typedef struct {
    uint32_t head;
    uint32_t tail;
    uint32_t rsvd[2];
    rpcmsg_t data[RPCMSG_BUFFER_SIZE];
} rpcmsg_queue_t;

typedef struct sel4_rpc {
    rpcmsg_queue_t *tx_queue;
    rpcmsg_queue_t *rx_queue;
    void (*doorbell)(void *doorbell_cookie);
    void *doorbell_cookie;
} sel4_rpc_t;

#define QUEUE_NEXT(_i) (((_i) + 1) & (RPCMSG_BUFFER_SIZE - 1))

__maybe_unused static void rpcmsg_queue_init(rpcmsg_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

/* we use load-acquire to make sure all loads reading the contents of message
 * happen only after reading the head -- likewise, we use store-release to make
 * sure all stores to message happen before we write the head.
 */
#define rpcmsg_queue_iterate(_qp, _msgp) \
    unsigned int _h; \
    for (_h = atomic_load_acquire(&(_qp)->head), \
         (_msgp) = (_qp)->data + _h; \
         _h != (_qp)->tail; \
         _h = QUEUE_NEXT(_h), \
         atomic_store_release(&(_qp)->head, _h), \
         (_msgp) = (_qp)->data + _h)

static void plat_yield(void)
{
    /* TODO: add proper platform specific implementations */
}

#define rpcmsg_state_ptr(_msg_ptr)          (&(_msg_ptr)->mr3)
#define rpcmsg_state_get(_msg_ptr)          atomic_load_acquire(rpcmsg_state_ptr(_msg_ptr))
#define rpcmsg_state_set(_msg_ptr, _state)  atomic_store_release(rpcmsg_state_ptr(_msg_ptr), _state)

static inline rpcmsg_t *rpcmsg_new(rpcmsg_queue_t *q)
{
    rpc_assert(q);

    unsigned int next;

    do {
        for (;;) {
            next = QUEUE_NEXT(q->tail);

            if (next != q->head) {
                break;
            }

            /* messages are produced faster than they are consumed */
            plat_yield();
        }

        /* if there are multiple writers, only one of those will succeed doing
         * CAS on state below -- the others will keep looping until queue's tail
         * is updated by that specific writer which broke out of loop -- the
         * process will repeat inductively until only one writer is left.
         */
    } while (__sync_bool_compare_and_swap(rpcmsg_state_ptr(q->data + next),
                                          RPCMSG_STATE_COMPLETE,
                                          RPCMSG_STATE_RESERVED));

    /* since we modified state of message pointed by 'next', we want that
     * store to finish before updating tail.
     */
    atomic_store_release(&q->tail, next);

    return q->data + next;
}

static inline bool rpcmsg_queue_full(rpcmsg_queue_t *q)
{
    return QUEUE_NEXT(q->tail) == q->head;
}

static inline bool rpcmsg_queue_empty(rpcmsg_queue_t *q)
{
    return q->tail == q->head;
}

static inline int sel4_rpc_doorbell(sel4_rpc_t *rpc)
{
    rpc_assert(rpc);
    rpc_assert(rpc->tx_queue);
    rpc_assert(rpc->doorbell);

    rpc->doorbell(rpc->doorbell_cookie);

    return 0;
}

static inline sel4_rpc_t *rpcmsg_compose(sel4_rpc_t *rpc, unsigned int op,
                                         seL4_Word mr0, seL4_Word mr1,
                                         seL4_Word mr2)
{
    rpcmsg_t *msg = rpcmsg_new(rpc->tx_queue);

    rpc_assert(msg);
    rpc_assert(rpcmsg_state_get(msg) == RPCMSG_STATE_RESERVED);

    msg->mr0 = mr0;
    msg->mr1 = mr1;
    msg->mr2 = mr2;

    msg->mr0 = BIT_FIELD_SET(msg->mr0, RPC_MR0_OP, op);

    rpcmsg_state_set(msg, RPCMSG_STATE_PENDING);

    return rpc;
}

/******************** message constructors for both sides ********************/

static inline
sel4_rpc_t *ntfn_status(sel4_rpc_t *rpc, unsigned int status)
{
    return rpcmsg_compose(rpc, RPC_MR0_OP_NOTIFY_STATUS, 0, status, 0);
}

/************ message constructors for device (sending to driver) ************/

static inline
sel4_rpc_t *driver_ntfn_device_status(sel4_rpc_t *rpc, unsigned int status)
{
    return ntfn_status(rpc, status);
}

static inline
sel4_rpc_t *driver_req_create_vpci_device(sel4_rpc_t *rpc, seL4_Word pcidev)
{
    return rpcmsg_compose(rpc, RPC_MR0_OP_REGISTER_PCI_DEV, 0, pcidev, 0);
}

static inline
sel4_rpc_t *driver_req_set_irqline(sel4_rpc_t *rpc, seL4_Word irq, unsigned int state)
{
    return rpcmsg_compose(rpc, RPC_MR0_OP_SET_IRQ, 0, irq, state);
}

static inline
sel4_rpc_t *driver_ack_mmio_finish(sel4_rpc_t *rpc, unsigned int slot, seL4_Word data)
{
    seL4_Word mr0 = 0;

    mr0 = BIT_FIELD_SET(mr0, RPC_MR0_MMIO_SLOT, slot);

    return rpcmsg_compose(rpc, RPC_MR0_OP_MMIO, mr0, 0, data);
}

/************ message constructors for driver (sending to device) ************/

static inline
sel4_rpc_t *device_ntfn_driver_status(sel4_rpc_t *rpc, unsigned int status)
{
    return ntfn_status(rpc, status);
}

static inline
sel4_rpc_t *device_req_mmio_start(sel4_rpc_t *rpc, unsigned int direction,
                                  unsigned int addr_space, unsigned int slot,
                                  seL4_Word addr, seL4_Word len, seL4_Word data)
{
    seL4_Word mr0 = 0;
    seL4_Word mr1 = 0;
    seL4_Word mr2 = 0;

    mr0 = BIT_FIELD_SET(mr0, RPC_MR0_MMIO_DIRECTION, direction);
    mr0 = BIT_FIELD_SET(mr0, RPC_MR0_MMIO_ADDR_SPACE, addr_space);
    mr0 = BIT_FIELD_SET(mr0, RPC_MR0_MMIO_LENGTH, len);
    mr0 = BIT_FIELD_SET(mr0, RPC_MR0_MMIO_SLOT, slot);
    mr1 = addr;
    mr2 = data;

    return rpcmsg_compose(rpc, RPC_MR0_OP_MMIO, mr0, mr1, mr2);
}

/*****************************************************************************/

static inline int sel4_rpc_init(sel4_rpc_t *rpc, rpcmsg_queue_t *rx,
                                rpcmsg_queue_t *tx,
                                void (*doorbell)(void *doorbell_cookie),
                                void *doorbell_cookie)
{
    if (!rpc || !rx || !tx || !doorbell) {
        return -1;
    }

    rpc->rx_queue = rx;
    rpc->tx_queue = tx;
    rpc->doorbell = doorbell;
    rpc->doorbell_cookie = doorbell_cookie;

    /* note that we do not initialize the queues themselves -- they are
     * initialized statically on boot and dynamically on VM reboot
     */

    return 0;
}
