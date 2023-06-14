/*
 * Copyright 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <camkes.h>
#include <vmlinux.h>
#include <sync/sem.h>
#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_vcpu_fault.h>
#include <sel4vm/guest_ram.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/boot.h>
#include <sel4vm/guest_irq_controller.h>

#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <pci/helper.h>

#include "sel4-qemu.h"
#include "trace.h"
#include "ioreq.h"
#include "pci_intx.h"

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>

#include <virtioarm/virtio_plat.h>

#define VIRTIO_PLAT_INTERRUPT_LINE VIRTIO_CON_PLAT_INTERRUPT_LINE

/********************* main type definitions begin here *********************/

typedef struct virtio_proxy_config {
    unsigned int vcpu_id;
    unsigned int irq;
    uintptr_t pci_mmio_base;
    size_t pci_mmio_size;
} virtio_proxy_config_t;

/********************** main type definitions end here **********************/

/****************** PCI proxy type definitions begin here *******************/

typedef struct pci_proxy {
    unsigned int idx;
} pci_proxy_t;

/******************* PCI proxy type definitions end here ********************/

/******************* CAmkES adaptation externs begin here *******************/

extern const int vmid;

/******************** CAmkES adaptation externs end here ********************/

/************************* main externs begin here **************************/

extern void *ctrl;
extern void *iobuf;
/* TODO: is 'vka' global variable dependent on CAmkES? */
extern vka_t _vka;

/* TODO: even though virtual PCI is in libsel4vmmplatsupport, isn't 'pci'
 * global variable dependent on CAmkES VM?
 */
extern vmm_pci_space_t *pci;

/************************** main externs end here ***************************/

/**************** CAmkES adaptation declarations begin here *****************/

/* VM0 does not have these */
int WEAK intervm_sink_reg_callback(void (*)(void *), void *);

static int camkes_init(void);
static void camkes_backend_notify(void);
static void camkes_intervm_callback(void *opaque);

/***************** CAmkES adaptation declarations end here ******************/

/*********************** main declarations begin here ***********************/

static int (*framework_init)(void) = camkes_init;
static void (*backend_notify)(void) = camkes_backend_notify;

static vm_t *vm;

static sync_sem_t handoff;
static sync_sem_t backend_started;

static volatile int ok_to_run = 0;

static ps_io_ops_t ops;

static pci_proxy_t *pci_devs[16];
static unsigned int pci_dev_count;
static intx_t *intx;

static memory_fault_result_t qemu_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie);
/************************ main declarations end here ************************/

/******************** PCI proxy declarations begin here *********************/

static pci_proxy_t *pci_proxy_init(vm_t *vm, vmm_pci_space_t *pci);

/********************* PCI proxy declarations end here **********************/

/************************** main code begins here ***************************/

static void register_pci_device(void)
{
    ZF_LOGI("Registering PCI device");

    pci_proxy_t *dev = pci_proxy_init(vm, pci);
    if (!dev) {
        ZF_LOGF("Failed to initialize PCI proxy");
    }

    pci_devs[pci_dev_count] = dev;
    pci_devs[pci_dev_count]->idx = pci_dev_count;
    pci_dev_count++;
}

static bool handle_async(rpcmsg_t *msg)
{
    int err;

    switch (QEMU_OP(msg->mr.mr0)) {
    case QEMU_OP_IO_HANDLED:
        if (ioreq_mmio_finish(vm, iobuf, msg->mr.mr1))
            return false;
        break;
    case QEMU_OP_SET_IRQ:
        intx_change_level(intx, msg->intx.int_source, true);
        break;
    case QEMU_OP_CLR_IRQ:
        intx_change_level(intx, msg->intx.int_source, false);
        break;
    case QEMU_OP_START_VM: {
        ioreq_init(iobuf);
        ok_to_run = 1;
        sync_sem_post(&backend_started);
        break;
    }
    case QEMU_OP_REGISTER_PCI_DEV:
        register_pci_device();
        break;
    default:
        return false;
    }

    return true;
}

static rpcmsg_t *peek_sync_msg(rpcmsg_queue_t *q)
{
    for (;;) {
        rpcmsg_t *msg = rpcmsg_queue_head(q);
        if (!msg) {
            return NULL;
        }
        if (!handle_async(msg)) {
            return msg;
        }
        rpcmsg_queue_advance_head(q);
    }
}

static void rpc_handler(void)
{
    rpcmsg_t *msg = peek_sync_msg(rx_queue);
    if (msg) {
        sync_sem_post(&handoff);
    }
}

static void wait_for_backend(void)
{
    // camkes_protect_reply_cap() ??
    /* TODO: in theory it should be enough to wait for the semaphore
     * but seems that any virtio console input makes the wait operation
     * complete without the corresponding post operation ever called.
     * It could be some kind of programming error but first we need
     * to study seL4 IPC in more depth.
     */
    do {
        int err = sync_sem_wait(&backend_started);
    } while (!ok_to_run);
}

static int virtio_proxy_init(const virtio_proxy_config_t *config)
{
    vm_memory_reservation_t *reservation;

    int err = ps_new_stdlib_malloc_ops(&ops.malloc_ops);
    if (err) {
        ZF_LOGE("Failed to get malloc ops (%d)", err);
        return -1;
    }

    intx = intx_init(vm->vcpus[config->vcpu_id], config->irq);
    if (!intx) {
        ZF_LOGE("intx_init() failed");
        return -1;
    }

    reservation = vm_reserve_memory_at(vm, config->pci_mmio_base,
                                       config->pci_mmio_size,
                                       qemu_fault_handler, NULL);
    if (!reservation) {
        ZF_LOGE("Cannot reserve PCI MMIO region");
        return -1;
    }

    if (sync_sem_new(&_vka, &handoff, 0)) {
        ZF_LOGE("Unable to allocate handoff semaphore");
        return -1;
    }

    err = framework_init();
    if (err) {
        ZF_LOGE("framework_init() failed (%d)", err);
        return -1;
    }

    /* fdt_generate_vpci_node() uses PCI emulation callbacks to determine IRQ
     * line and pin, hence it would block unless backend happens to be running
     * already. Fix this once we have more proper PCI frontend in place
     * (virtio-specific patched status words and timeouts in proxied MMIO in
     * general).
     */
    if (sync_sem_new(&_vka, &backend_started, 0)) {
        ZF_LOGE("Unable to allocate backend_started semaphore");
        return -1;
    }

    ZF_LOGI("waiting for virtio backend");
    wait_for_backend();
    ZF_LOGI("virtio backend up, continuing");

    return 0;
}

/*************************** main code ends here ****************************/

/************************ PCI proxy code begins here ************************/

static inline uint32_t pci_proxy_start(pci_proxy_t *dev, unsigned int dir,
                                       uintptr_t offset, size_t size,
                                       uint32_t value)
{
    int slot = ioreq_pci_start(iobuf, dev->idx, dir, offset, size, value);
    assert(ioreq_slot_valid(slot));

    backend_notify();
    sync_sem_wait(&handoff);

    value = ioreq_pci_finish(iobuf, slot);
    rpcmsg_queue_advance_head(rx_queue);

    return value;
}

static inline uint32_t pci_proxy_read(void *cookie, unsigned int offset,
                                      size_t size)
{
    pci_proxy_t *dev = cookie;

    return pci_proxy_start(dev, SEL4_IO_DIR_READ, offset, size, 0);
}

static inline void pci_proxy_write(void *cookie, unsigned int offset,
                                   size_t size, uint32_t value)
{
    pci_proxy_t *dev = cookie;

    pci_proxy_start(dev, SEL4_IO_DIR_WRITE, offset, size, value);
}

static uint8_t pci_proxy_read8(void *cookie, vmm_pci_address_t addr,
                               unsigned int offset)
{
    if (offset == 0x3c) {
        return VIRTIO_PLAT_INTERRUPT_LINE;
    }
    return pci_proxy_read(cookie, offset, 1);
}

static uint16_t pci_proxy_read16(void *cookie, vmm_pci_address_t addr,
                                 unsigned int offset)
{
    return pci_proxy_read(cookie, offset, 2);
}

static uint32_t pci_proxy_read32(void *cookie, vmm_pci_address_t addr,
                                 unsigned int offset)
{
    return pci_proxy_read(cookie, offset, 4);
}

static void pci_proxy_write8(void *cookie, vmm_pci_address_t addr,
                             unsigned int offset, uint8_t val)
{
    pci_proxy_write(cookie, offset, 1, val);
}

static void pci_proxy_write16(void *cookie, vmm_pci_address_t addr,
                              unsigned int offset, uint16_t val)
{
    pci_proxy_write(cookie, offset, 2, val);
}

static void pci_proxy_write32(void *cookie, vmm_pci_address_t addr,
                              unsigned int offset, uint32_t val)
{
    pci_proxy_write(cookie, offset, 4, val);
}

static vmm_pci_config_t pci_proxy_make_config(pci_proxy_t *dev)
{
    return (vmm_pci_config_t) {
        .cookie = dev,
        .ioread8 = pci_proxy_read8,
        .ioread16 = pci_proxy_read16,
        .ioread32 = pci_proxy_read32,
        .iowrite8 = pci_proxy_write8,
        .iowrite16 = pci_proxy_write16,
        .iowrite32 = pci_proxy_write32,
    };
}

static pci_proxy_t *pci_proxy_init(vm_t *vm, vmm_pci_space_t *pci)
{
    pci_proxy_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        ZF_LOGE("Failed to allocate memory");
        return NULL;
    }

    vmm_pci_address_t bogus_addr = {
        .bus = 0,
        .dev = 0,
        .fun = 0,
    };
    vmm_pci_entry_t entry = vmm_pci_create_passthrough(bogus_addr,
                                                       pci_proxy_make_config(dev));
    vmm_pci_add_entry(pci, entry, NULL);

    return dev;
}

/************************* PCI proxy code ends here *************************/

static inline void qemu_read_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    int err;

    err = ioreq_mmio_start(iobuf, vcpu, SEL4_IO_DIR_READ, paddr, len, 0);
    if (err < 0) {
        ZF_LOGF("Failure starting mmio read request");
    }
    backend_notify();
}

static inline void qemu_write_fault(vm_vcpu_t *vcpu, uintptr_t paddr, size_t len)
{
    uint32_t mask;
    uint32_t value;
    int err;

    seL4_Word s = (get_vcpu_fault_address(vcpu) & 0x3) * 8;
    mask = get_vcpu_fault_data_mask(vcpu) >> s;
    value = get_vcpu_fault_data(vcpu) & mask;

    err = ioreq_mmio_start(iobuf, vcpu, SEL4_IO_DIR_WRITE, paddr, len, value);
    if (err < 0) {
        ZF_LOGF("Failure starting mmio write request");
    }
    backend_notify();
}

static memory_fault_result_t qemu_fault_handler(vm_t *vm, vm_vcpu_t *vcpu,
                                                uintptr_t paddr, size_t len,
                                                void *cookie)
{
    if (is_vcpu_read_fault(vcpu)) {
        qemu_read_fault(vcpu, paddr, len);
    } else {
        qemu_write_fault(vcpu, paddr, len);
    }

    /* Let's not advance the fault here -- the reply from QEMU does that */
    return FAULT_HANDLED;
}

/******************** CAmkES adaptation code begins here *********************/

static int camkes_init(void)
{
    camkes_intervm_callback(ctrl);

    return 0;
}

static void camkes_intervm_callback(void *opaque)
{
    int err = intervm_sink_reg_callback(camkes_intervm_callback, opaque);
    assert(!err);

    rpc_handler();
}

static void camkes_backend_notify(void)
{
    intervm_source_emit();
}

static void virtio_proxy_module_init(vm_t *_vm, void *cookie)
{
    const virtio_proxy_config_t *config = cookie;

    vm = _vm;

    if (vmid == 0) {
        ZF_LOGI("Not configuring virtio proxy for VM %d", vmid);
        return;
    }

    int err = virtio_proxy_init(config);
    if (err) {
        ZF_LOGF("virtio_proxy_init() failed (%d)", err);
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

/********************* CAmkES adaptation code ends here **********************/
