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

#include <virtioarm/virtio_plat.h>

#define VIRTIO_PLAT_INTERRUPT_LINE VIRTIO_CON_PLAT_INTERRUPT_LINE

extern const int vmid;
extern vka_t _vka;
extern vmm_pci_space_t *pci;

/* TODO: support multi-VM */
extern void *ctrl;
extern void *iobuf;

/* TODO: support multi-VM */
static virtio_proxy_t *vm0_proxy;

static int camkes_init(io_proxy_t *opaque);
static void camkes_backend_notify(void);
static void camkes_intervm_callback(void *opaque);

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

int (*framework_init)(io_proxy_t *) = camkes_init;
void (*backend_notify)(void) = camkes_backend_notify;

void *framework_get_ctrl_shm(const virtio_proxy_config_t *config)
{
    /* TODO: support multi-VM */
    return ctrl;
}

void *framework_get_iobuf_shm(const virtio_proxy_config_t *config)
{
    /* TODO: support multi-VM */
    return iobuf;
}

static int camkes_init(io_proxy_t *io_proxy)
{
    camkes_intervm_callback(io_proxy);

    return 0;
}

static void camkes_intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(camkes_intervm_callback, opaque);
    assert(!err);

    io_proxy_process((io_proxy_t *)opaque);
}

static void camkes_backend_notify(void)
{
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

    vm0_proxy = virtio_proxy_init(vm, pci, &_vka, config);
    if (vm0_proxy == NULL) {
        ZF_LOGF("virtio_proxy_init() failed");
        /* no return */
    }
}

virtio_proxy_config_t virtio_proxy_config = {
    .vcpu_id = BOOT_VCPU,
    .irq = VIRTIO_PLAT_INTERRUPT_LINE,
    .pci_mmio_base = PCI_MEM_REGION_ADDR,
    .pci_mmio_size = 524288,
};

DEFINE_MODULE(virtio_proxy, &virtio_proxy_config, virtio_proxy_module_init)
