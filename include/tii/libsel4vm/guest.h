/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

typedef struct guest_config {
    vm_t *vm;
    void *dtb;
} guest_config_t;
