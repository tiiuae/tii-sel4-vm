/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <camkes.h>
#include <arm_vm/gen_config.h>
#include <vm_qemu_virtio/gen_config.h>

#ifdef CONFIG_VM_SWIOTLB
/*- set swiotlb_config = configuration[me.name].get('swiotlb_config') -*/

/*- if swiotlb_config -*/
const unsigned long swiotlb_gpa = /*? swiotlb_config.get('gpa') ?*/;
const unsigned long swiotlb_size = /*? swiotlb_config.get('size') ?*/;
/*- else -*/
#warning SWIOTLB buffer not configured
const unsigned long swiotlb_gpa = 0;
const unsigned long swiotlb_size = 0;
/*- endif -*/
#endif
