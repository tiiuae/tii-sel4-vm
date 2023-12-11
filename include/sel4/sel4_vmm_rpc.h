/*
 * Copyright 2022, Technology Innovation Institute
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

#if !defined(SEL4_VMM)
typedef unsigned long seL4_Word;
#endif

#define atomic_load_acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, i)  __atomic_store_n(ptr, i, __ATOMIC_RELEASE)

#if defined(__KERNEL__)
#define rpc_assert(_cond) BUG_ON(!(_cond))
#else
#define rpc_assert assert
#endif

#define IOBUF_PAGE_DRIVER_RX    2
#define IOBUF_PAGE_DRIVER_TX    1
#define IOBUF_PAGE_VMM_MMIO     0

#define IOBUF_PAGE_DEVICE_RX    IOBUF_PAGE_DRIVER_TX
#define IOBUF_PAGE_DEVICE_TX    IOBUF_PAGE_DRIVER_RX
#define IOBUF_PAGE_EMU_MMIO     0

#define iobuf_page(_iobuf, _page) (((uintptr_t)(_iobuf)) + (4096 * (_page)))

#define driver_tx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DRIVER_TX))
#define driver_rx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DRIVER_RX))
#define vmm_mmio_reqs(_iobuf) ((struct sel4_iohandler_buffer *)iobuf_page((_iobuf), IOBUF_PAGE_VMM_MMIO))

#define device_tx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DEVICE_TX))
#define device_rx_queue(_iobuf) ((rpcmsg_queue_t *)iobuf_page((_iobuf), IOBUF_PAGE_DEVICE_RX))
#define emu_mmio_reqs(_iobuf) ((struct sel4_iohandler_buffer *)iobuf_page((_iobuf), IOBUF_PAGE_EMU_MMIO))

/* from VMM to QEMU */
#define QEMU_OP_IO_HANDLED  0
#define QEMU_OP_PUTC_LOG    2

/* from QEMU to VMM */
#define QEMU_OP_SET_IRQ     16
#define QEMU_OP_CLR_IRQ     17
#define QEMU_OP_START_VM    18
#define QEMU_OP_REGISTER_PCI_DEV    19
#define QEMU_OP_MMIO_REGION_CONFIG  20

#define QEMU_OP_MASK        0xffULL
#define QEMU_OP_SHIFT       0
#define QEMU_OP(__x__)      ((unsigned int)(((__x__) & QEMU_OP_MASK) >> QEMU_OP_SHIFT))

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

static inline bool rpcmsg_queue_full(rpcmsg_queue_t *q)
{
    return QUEUE_NEXT(q->tail) == q->head;
}

static inline bool rpcmsg_queue_empty(rpcmsg_queue_t *q)
{
    return q->tail == q->head;
}

static inline rpcmsg_t *rpcmsg_queue_tail(rpcmsg_queue_t *q)
{

    return rpcmsg_queue_full(q) ? NULL : (q->data + q->tail);
}

static inline void rpcmsg_queue_advance_tail(rpcmsg_queue_t *q)
{
    rpc_assert((!rpcmsg_queue_full(q)));
    q->tail = QUEUE_NEXT(q->tail);
}

static inline void rpcmsg_queue_enqueue(rpcmsg_queue_t *q, rpcmsg_t *msg)
{
    rpc_assert(!rpcmsg_queue_full(q));
    memcpy(q->data + q->tail, msg, sizeof(*msg));
    q->tail = QUEUE_NEXT(q->tail);
}

static inline int sel4_rpc_doorbell(sel4_rpc_t *rpc)
{
    rpc_assert(rpc);
    rpc_assert(rpc->tx_queue);
    rpc_assert(rpc->doorbell);

    rpc->doorbell(rpc->doorbell_cookie);

    return 0;
}

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
