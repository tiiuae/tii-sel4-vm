#
# Copyright 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(TII_SEL4_VM_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "")
mark_as_advanced(TII_SEL4_VM_DIR)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(tii-sel4-vm DEFAULT_MSG TII_SEL4_VM_IMAGES_DIR)
