#!/bin/sh
#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

set -e

/usr/bin/dataport_read /dev/uio0 4096
echo -ne "This is a test user string\n\0" | /usr/bin/dataport_write /dev/uio0 4096
/usr/bin/dataport_read /dev/uio0 4096
/usr/bin/consumes_event_wait /dev/uio0 &
sleep 1
/usr/bin/emits_event_emit /dev/uio0
wait
echo "Finished Cross-VM test script"
