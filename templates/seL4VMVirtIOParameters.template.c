/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <camkes.h>
#include <vm_qemu_virtio/gen_config.h>

/*- set virtio_config = configuration[me.name].get('virtio_config') -*/

/*- if virtio_config -*/
const uintptr_t virtio_fe_base = virtio_config.get('base');
const size_t virtio_fe_size = virtio_config.get('size');
/*- else -*/
#warning No virtio front-end configured for this VM
const uintptr_t virtio_fe_base = 0;
const size_t virtio_fe_size = 0;
/*- endif -*/
