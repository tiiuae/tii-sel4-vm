/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <camkes.h>
#include <vmlinux.h>
#include <utils/util.h>

#include <tii/camkes/pl011.h>
/*- set pl011 = configuration[me.name].get('pl011') -*/
/*- if pl011 is not none -*/
static pl011_t pl011 = {
    .base = /*? pl011 ?*/,
    .size = BIT(PAGE_BITS_4K),
};

DEFINE_MODULE(pl011, &pl011, pl011_init)
/*- endif -*/
