/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/boot.h>

#include <tii/emulated_device.h>
#include <tii/reservations.h>
#include <tii/irq_line.h>
#include <tii/pci.h>

typedef struct emudev_handler {
    vm_t *vm;
    memory_fault_callback_fn fault_handler;
} emudev_handler_t;

static emudev_handler_t emudev_handler;

static inline bool irq_is_emulated_device(uint32_t irq)
{
    return irq >= PCI_NUM_SLOTS;
}

static irq_line_t *emudev_irq_register(io_proxy_t *io_proxy,
                                       vm_vcpu_t *vcpu,
                                       uint32_t irq)
{
    int err = irq_res_assign(io_proxy, vcpu, irq);
    if (err) {
        ZF_LOGE("Failed to register emulated device IRQ %u for backend %p",
                irq, io_proxy);
        return NULL;
    }

    return irq_res_find(io_proxy, irq);
}

static int emudev_irq_set(io_proxy_t *io_proxy, uint32_t irq, bool level)
{
    if (!irq_is_emulated_device(irq)) {
        ZF_LOGE("Interrupt %u is not a valid emulated device interrupt", irq);
        return -1;
    }

    irq_line_t *irq_line = irq_res_find(io_proxy, irq);
    if (!irq_line) {
        /* IRQ does not exist. Try to register a new one */
        irq_line = emudev_irq_register(io_proxy,
                                       emudev_handler.vm->vcpus[BOOT_VCPU],
                                       irq);
    }

    if (irq_line) {
        return irq_line_change(irq_line, level);
    }

    return -1;
}

static int emudev_mmio_config(io_proxy_t *io_proxy,
                              uint64_t addr,
                              uint64_t size,
                              uint64_t flags)
{
    if (flags & ~(SEL4_MMIO_REGION_FREE)) {
        ZF_LOGE("Unknown mmio region flags 0x%" PRIx64, flags);
        return -1;
    }

    if (flags & SEL4_MMIO_REGION_FREE) {
        return mmio_res_free(io_proxy, addr, size);
    }

    return mmio_res_assign(emudev_handler.vm,
                           emudev_handler.fault_handler,
                           io_proxy, addr, size);
}

int handle_emudev(io_proxy_t *io_proxy, unsigned int op, rpcmsg_t *msg)
{
    int err = 0;

    switch (op) {
    case QEMU_OP_MMIO_REGION_CONFIG:
        err = emudev_mmio_config(io_proxy, msg->mr1, msg->mr2, msg->mr3);
        break;
    case QEMU_OP_SET_IRQ:
        err = emudev_irq_set(io_proxy, msg->mr1, true);
        break;
    case QEMU_OP_CLR_IRQ:
        err = emudev_irq_set(io_proxy, msg->mr1, false);
        break;
    default:
        return RPCMSG_RC_NONE;
    }

    if (err) {
        return RPCMSG_RC_ERROR;
    }

    return RPCMSG_RC_HANDLED;
}

int emudev_init(vm_t *vm, memory_fault_callback_fn fault_callback_fn)
{
    int err = 0;

    emudev_handler.vm = vm;
    emudev_handler.fault_handler = fault_callback_fn;

    err = irq_res_init();
    if (err) {
        ZF_LOGE("Initializing IRQ reservations failed");
        return err;
    }

    err = mmio_res_init();
    if (err) {
        ZF_LOGE("Initializing MMIO reservations failed");
    }

    return err;
}

