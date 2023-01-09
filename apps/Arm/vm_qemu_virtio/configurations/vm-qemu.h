#pragma once

#define VM_QEMU_INIT_DEF() \
    control; \
    uses FileServerInterface fs; \
    DEF_TK1DEVICEFWD \
    DEF_KERNELARMPLATFORM_EXYNOS5410 \
    maybe consumes restart restart_event; \
    has semaphore vm_sem; \
    consumes HaveNotification notification_ready; \
    emits HaveNotification notification_ready_connector; \
    maybe uses VMDTBPassthrough dtb_self; \
    provides VMDTBPassthrough dtb; \
    attribute int base_prio; \
    attribute int num_vcpus = 1; \
    attribute int num_extra_frame_caps; \
    attribute int extra_frame_map_address; \
    attribute int tracebuffer_base; \
    attribute int tracebuffer_size; \
    attribute int ramoops_base; \
    attribute int ramoops_size; \
    attribute { \
        string linux_ram_base; \
        string linux_ram_paddr_base; \
        string linux_ram_size; \
        string linux_ram_offset; \
        string dtb_addr; \
        string initrd_max_size; \
        string initrd_addr; \
    } linux_address_config; \
    attribute { \
        string linux_name = "linux"; \
        string dtb_name = "linux-dtb"; \
        string initrd_name = "linux-initrd"; \
        string linux_bootcmdline = ""; \
        string linux_stdout = ""; \
    } linux_image_config; \

#define VM_QEMU_CONFIGURATION_DEF(num) \
    vm##num.fs_shmem_size = 0x100000; \
    vm##num.global_endpoint_base = 1 << 27; \
    vm##num.asid_pool = true; \
    vm##num.simple = true; \
    vm##num.sem_value = 0; \
    vm##num.heap_size = 0x300000;

