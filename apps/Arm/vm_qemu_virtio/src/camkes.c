/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <camkes.h>
#include <vmlinux.h>

#include <sel4vm/boot.h>
#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include "io_proxy.h"
#include "virtio_proxy.h"
#include "rpc.h"

#include <virtioarm/virtio_plat.h>

#define VIRTIO_PLAT_INTERRUPT_LINE VIRTIO_CON_PLAT_INTERRUPT_LINE

extern const int vmid;
extern vka_t _vka;
extern vmm_pci_space_t *pci;

/* TODO: support multi-VM */
extern void *ctrl;
extern void *iobuf;

#define vm0_vm1_ctrl ctrl
#define vm0_vm1_iobuf iobuf

static virtio_proxy_t *vm0_vm1_virtio_proxy;

static rpc_t *vm0_vm1_rpc;
static io_proxy_t *vm0_vm1_io_proxy;

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

static void camkes_intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(camkes_intervm_callback, opaque);
    assert(!err);

    err = rpc_run((rpc_t *)opaque);
    ZF_LOGF_IF(err, "RPC handler returned error, no point to continue");
}

static void camkes_backend_notify(rpc_t *rpc)
{
    /* TODO: support multi-VM */
    intervm_source_emit();
}

static void virtio_proxy_module_init(vm_t *vm, void *cookie)
{
    const virtio_proxy_config_t *config = cookie;

    /* TODO: support multi-VM */
    if (vmid == 0) {
        ZF_LOGI("Not configuring virtio proxy for VM %d", vmid);
        return;
    }

    /* TODO: support multi-VM */
    vm0_vm1_rpc = rpc_new(((rpcmsg_queue_t *) vm0_vm1_ctrl) + 1,
                          camkes_backend_notify);

    vm0_vm1_io_proxy = io_proxy_new(&vm0_vm1_iobuf, &_vka, vm0_vm1_rpc);
    if (!vm0_vm1_io_proxy) {
        ZF_LOGF("io_proxy_init() failed");
        /* no return */
    }

    vm0_vm1_virtio_proxy = virtio_proxy_new(vm, pci, &_vka, vm0_vm1_rpc,
                                            vm0_vm1_io_proxy, config);
    if (!vm0_vm1_virtio_proxy) {
        ZF_LOGF("virtio_proxy_init() failed");
        /* no return */
    }
    
    camkes_intervm_callback(&vm0_vm1_rpc);
}

virtio_proxy_config_t virtio_proxy_config = {
    .vcpu_id = BOOT_VCPU,
    .irq = VIRTIO_PLAT_INTERRUPT_LINE,
    .pci_mmio_base = PCI_MEM_REGION_ADDR,
    .pci_mmio_size = 524288,
};

DEFINE_MODULE(virtio_proxy, &virtio_proxy_config, virtio_proxy_module_init)
