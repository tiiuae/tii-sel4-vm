#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
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
CAmkESAddTemplatesPath(templates)

set(CAmkESCPP ON CACHE BOOL "" FORCE)

AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmSWIOTLB)

set(configure_string "")

config_option(
    VmVirtioQEMU
    VM_VIRTIO_QEMU
    "Enable virtio QEMU module"
    DEPENDS
    "KernelPlatformQEMUArmVirt OR KernelPlatformRpi4;VmPCISupport"
    DEFAULT
    OFF
)

config_option(
    VmSWIOTLB
    VM_SWIOTLB
    "Use SWIOTLB instead of exposing whole guest RAM to host"
    DEPENDS
    "VmVirtioQEMU"
    DEFAULT
    ON
)

add_config_library(tii_camkes_vm "${configure_string}")

file(
    GLOB
        sources
        src/*.c
)

add_library(tii_camkes_vm STATIC EXCLUDE_FROM_ALL ${sources})
target_include_directories(
    tii_camkes_vm
    PUBLIC include
)

# Add VM application
add_subdirectory("apps/${AppArch}/${CAMKES_VM_APP}")
# Should be done adding targets, can now generate the root server and the global configuration
GenerateCAmkESRootserver()
