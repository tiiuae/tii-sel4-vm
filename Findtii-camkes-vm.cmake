#
# Copyright 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(TII_CAMKES_VM_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "")
mark_as_advanced(TII_CAMKES_VM_DIR)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(tii-camkes-vm DEFAULT_MSG TII_CAMKES_DIR)
