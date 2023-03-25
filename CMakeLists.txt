#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)
include(settings.cmake)
project(camkes-vm C CXX ASM)

set(TII_CAMKES_VM_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "")
mark_as_advanced(TII_CAMKES_VM_DIR)

if(AppArch STREQUAL "Arm")
	find_package(camkes-arm-vm REQUIRED)
	camkes_arm_vm_setup_arm_vm_environment()
else()
	message(FATAL_ERROR "Unsupported")
endif()

file(
    GLOB
        tii_camkes_vm_sources
        src/*.c
        src/modules/*.c
        src/plat/${KernelPlatform}/*.c
)

if(KernelPlatformRpi4)
    list(
        APPEND tii_camkes_vm_sources "${TII_CAMKES_VM_DIR}/src/plat/rpi4")
    )
else()
    list(
        APPEND tii_camkes_vm_sources "${TII_CAMKES_VM_DIR}/src/plat/${KernelPlatform}")
    )
endif()

add_library(tii_camkes_vm STATIC EXCLUDE_FROM_ALL ${tii_camkes_vm_sources})
target_include_directories(
    tii_camkes_vm
    PUBLIC include
)

# Add VM application
add_subdirectory("apps/${AppArch}/${CAMKES_VM_APP}")
# Should be done adding targets, can now generate the root server and the global configuration
GenerateCAmkESRootserver()
