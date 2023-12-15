/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tii/reservations.h>
#include <tii/list.h>

typedef struct mmio_reservation {
    uint64_t addr;
    uint64_t size;
    vm_memory_reservation_t *res;
    io_proxy_t *io_proxy;
} mmio_reservation_t;

static list_t mmio_reservations;

static int mmio_res_cmp(void *l, void *r)
{
    mmio_reservation_t *a = l;
    mmio_reservation_t *b = r;

    return !(a->addr == b->addr &&
             a->size == b->size &&
             a->io_proxy == b->io_proxy);
}

static inline mmio_reservation_t *mmio_res_find(io_proxy_t *io_proxy,
                                                uint64_t addr,
                                                uint64_t size)
{
    mmio_reservation_t res = {
        .addr = addr,
        .size = size,
        .io_proxy = io_proxy,
    };

    return list_item(&mmio_reservations, &res, &mmio_res_cmp);
}

int mmio_res_assign(vm_t *vm, memory_fault_callback_fn fault_handler,
                    io_proxy_t *io_proxy, uint64_t addr, uint64_t size)
{
    vm_memory_reservation_t *res;

    res = vm_reserve_memory_at(vm, addr, size, fault_handler, io_proxy);
    if (!res) {
        ZF_LOGE("Failed to reserve MMIO region 0x%" PRIx64 " size 0x%"
                PRIx64 " for backend %p", addr, size, io_proxy);
        return -1;
    }

    mmio_reservation_t *mmio = calloc(1, sizeof(*mmio));
    if (!mmio) {
        ZF_LOGE("Failed to allocate object for mmio reservation");
        return -1;
    }

    mmio->addr = addr;
    mmio->size = size;
    mmio->res = res;
    mmio->io_proxy = io_proxy;

    int err = list_append(&mmio_reservations, mmio);
    if (err) {
        ZF_LOGE("Failed to add mmio reservation to list");
        free(mmio);
    }

    return err;
}

int mmio_res_free(io_proxy_t *io_proxy, uint64_t addr, uint64_t size)
{
    mmio_reservation_t match = {
        .addr = addr,
        .size = size,
        .io_proxy = io_proxy,
    };

    mmio_reservation_t *res = list_item(&mmio_reservations, &match, &mmio_res_cmp);
    if (!res) {
        ZF_LOGE("Failed to find mmio reservation for 0x%" PRIx64 " size 0x%"
                PRIx64 " for backend %p", addr, size, io_proxy);
        return -1;
    }

    int err = list_remove(&mmio_reservations, &match, &mmio_res_cmp);
    ZF_LOGE_IF(err, "list_remove() failed");

    err = vm_reservation_free(res->res);
    ZF_LOGE_IF(err, "Failed to free mmio reservation 0x%" PRIx64 " size 0x%"
               PRIx64  "for backend %p", addr, size, io_proxy);
    free(res);

    return err;
}

int mmio_res_init(void)
{
    return list_init(&mmio_reservations);
}
