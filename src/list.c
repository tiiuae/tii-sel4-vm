/*
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stddef.h>

#include <tii/list.h>

void *list_item(list_t *l, void *data, int(*cmp)(void *, void *))
{
    assert(l != NULL);
    for (struct list_node *n = l->head; n != NULL; n = n->next) {
        if (!cmp(n->data, data)) {
            return n->data;
        }
    }
    return NULL;
}

