/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/***
 * @module pci_intx.h
 * This module emulates the PCI #INTx interrupt pins. An interrupt pin can be
 * shared among multiple devices. The code computes logical OR on all the
 * signals and whenever there is a rising edge, predefined IRQ is injected onto
 * the given vCPU (typically the boot vCPU).
 */

typedef struct intx intx_t;

/***
 * @function intx_init(vcpu, irq)
 * Allocate and initialize #INTx object.
 * @param {vm_vcpu_t *} vcpu   vCPU to which IRQ will be injected
 * @param {unsigned int} irq   IRQ number that will be injected
 * @return                     NULL on error, otherwise pointer to #INTx object
 */
intx_t *intx_init(vm_vcpu_t *vcpu, unsigned int irq);

/***
 * @function intx_change_level(intx, dev, active)
 * @param {intx_t *} intx      Pointer to #INTx object
 * @param {int} dev            Index of device changing the pin level
 * @param {bool} active        New pin level
 * @return                     Zero on success, non-zero on failure
 */
int intx_change_level(intx_t *intx, unsigned int dev, bool active);
