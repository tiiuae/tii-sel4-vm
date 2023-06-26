/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <sync/sem.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>


#include <sel4vm/guest_irq_controller.h>

#include "sel4-qemu.h"
#include "trace.h"
#include "ioreq.h"
#include "pci_intx.h"
#include "virtio_proxy.h"
#include "pci_proxy.h"

typedef struct virtio_proxy {
    intx_t *intx;
    sync_sem_t backend_started;
    int ok_to_run;
    const virtio_proxy_config_t *config;
    pci_proxy_t *pci_devs[16];
    unsigned int pci_dev_count;
    io_proxy_t *io_proxy;
    vmm_pci_space_t *pci;
    vm_t *vm;
} virtio_proxy_t;

static int register_pci_device(virtio_proxy_t *proxy)
{
    ZF_LOGI("Registering PCI device");

    unsigned int idx = proxy->pci_dev_count;

    pci_proxy_t *pci_proxy = pci_proxy_init(proxy->io_proxy, proxy->pci, idx);
    if (!pci_proxy) {
        ZF_LOGE("Failed to initialize PCI proxy");
        return -1;
    }

    proxy->pci_devs[idx] = pci_proxy;
    proxy->pci_dev_count++;

    return 0;
}

static int handle_intx(rpcmsg_t *msg, void *cookie)
{
    rpcmsg_union_t *u = (rpcmsg_union_t *)msg;
    virtio_proxy_t *proxy = cookie;

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_SET_IRQ:
        intx_change_level(proxy->intx, u->intx.int_source, true);
        break;
    case QEMU_OP_CLR_IRQ:
        intx_change_level(proxy->intx, u->intx.int_source, false);
        break;
    default:
        /* not handled in this callback */
        return 0;
    }

    return 1;
}	

static int handle_async(rpcmsg_t *msg, void *cookie)
{
    virtio_proxy_t *proxy = cookie;

    int err;

    switch (QEMU_OP(msg->mr0)) {
    case QEMU_OP_START_VM: {
        proxy->ok_to_run = 1;
        sync_sem_post(&proxy->backend_started);
        break;
    }
    case QEMU_OP_REGISTER_PCI_DEV:
        err = register_pci_device(proxy);
        if (err) {
            return -1;
        }
        break;
    default:
        /* not handled in this callback */
        return 0;
    }

    return 1;
}

static void wait_for_backend(virtio_proxy_t *proxy)
{
    // camkes_protect_reply_cap() ??
    /* TODO: in theory it should be enough to wait for the semaphore
     * but seems that any virtio console input makes the wait operation
     * complete without the corresponding post operation ever called.
     * It could be some kind of programming error but first we need
     * to study seL4 IPC in more depth.
     */
    do {
        int err = sync_sem_wait(&proxy->backend_started);
    } while (!(volatile int)proxy->ok_to_run);
}

int virtio_proxy_init(virtio_proxy_t *virtio_proxy, vm_t *vm,
                      vmm_pci_space_t *pci, vka_t *vka, rpc_t *rpc,
                      io_proxy_t *io_proxy,
                      const virtio_proxy_config_t *config)
{
    virtio_proxy->config = config;
    virtio_proxy->vm = vm;
    virtio_proxy->pci = pci;

    virtio_proxy->io_proxy = io_proxy;

    virtio_proxy->intx = intx_init(vm->vcpus[config->vcpu_id], config->irq);
    if (!virtio_proxy->intx) {
        ZF_LOGE("intx_init() failed");
        return -1;
    }

    int err = rpc_register_callback(rpc, handle_async, virtio_proxy); 
    if (err) {
        ZF_LOGE("Cannot register RPC callback");
        return -1;
    }

    err = rpc_register_callback(rpc, handle_intx, virtio_proxy); 
    if (err) {
        ZF_LOGE("Cannot register RPC callback");
        return -1;
    }

    err = mmio_proxy_create(io_proxy, vm, config->pci_mmio_base,
                            config->pci_mmio_size);
    if (err) {
        ZF_LOGE("Cannot create PCI config space MMIO virtio_proxy");
        return -1;
    }

    /* fdt_generate_vpci_node() uses PCI emulation callbacks to determine IRQ
     * line and pin, hence it would block unless backend happens to be running
     * already. Fix this once we have more proper PCI frontend in place
     * (virtio-specific patched status words and timeouts in proxied MMIO in
     * general).
     */
    if (sync_sem_new(vka, &virtio_proxy->backend_started, 0)) {
        ZF_LOGE("Unable to allocate backend_started semaphore");
        return -1;
    }

    ZF_LOGI("waiting for virtio backend");
    wait_for_backend(virtio_proxy);
    ZF_LOGI("virtio backend up, continuing");

    return 0;
}

virtio_proxy_t *virtio_proxy_new(vm_t *vm, vmm_pci_space_t *pci, vka_t *vka,
                                 rpc_t *rpc, io_proxy_t *io_proxy,
                                 const virtio_proxy_config_t *config)
{
    virtio_proxy_t *virtio_proxy = calloc(1, sizeof(*virtio_proxy));

    if (virtio_proxy) {
        int err = virtio_proxy_init(virtio_proxy, vm, pci, vka, rpc, io_proxy,
                                    config);
        if (err) {
            return NULL;
        }
    }

    return virtio_proxy;
}
