/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sel4vm/guest_vm.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/guest_ram.h>
#include <utils/util.h>

#include <tii/ram_dataport.h>

static USED SECTION("_ram_dataport_definition") struct {} dummy_ram_dataport_definition;
extern ram_dataport_t __start__ram_dataport_definition[];
extern ram_dataport_t __stop__ram_dataport_definition[];

int WEAK ram_dataport_setup(void)
{
    return 0;
}

static int ram_dataport_map(vm_t *vm, ram_dataport_t *dp)
{
    vm_memory_reservation_t *reservation;

    reservation = vm_ram_reserve_at(vm, dp->addr,
                                    dp->num_frames * BIT(dp->frame_size_bits));
    if (!reservation) {
        ZF_LOGE("vm_ram_reserve_at() failed");
        return -1;
    }

    int err = vm_map_reservation_frames(vm, reservation, dp->frames,
                                        dp->num_frames,
                                        dp->frame_size_bits);
    if (err) {
        ZF_LOGE("vm_map_reservation_frames() failed: %d", err);
        return -1;
    }

    return 0;
}

int ram_dataport_map_all(vm_t *vm)
{
    int err = ram_dataport_setup();
    if (err) {
        ZF_LOGE("ram_dataport_setup() failed (%d)", err);
        return -1;
    }

    for (ram_dataport_t *dp = __start__ram_dataport_definition;
         dp < __stop__ram_dataport_definition; dp++) {
        err = ram_dataport_map(vm, dp);
        if (err) {
            ZF_LOGE("ram_dataport_map() failed (%d)", err);
            return -1;
        }
    }

    return 0;
}
