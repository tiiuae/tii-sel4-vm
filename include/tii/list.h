/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <utils/list.h>

/* Returns the data of the given element in the list or NULL if the element is
 * not found.
 */
void *list_item(list_t *l, void *data, int(*cmp)(void *, void *));
