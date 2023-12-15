/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sel4vm/guest_vm.h>

#include <tii/io_proxy.h>
#include <tii/irq_line.h>

/* IRQ reservations */
irq_line_t *irq_res_find(io_proxy_t *io_proxy, uint32_t irq);
int irq_res_assign(io_proxy_t *io_proxy, vm_vcpu_t *vcpu, uint32_t irq);
int irq_res_free(io_proxy_t *io_proxy, uint32_t irq);
int irq_res_init(void);
