#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.12.0)
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

set(configure_string "")

config_option(
    VmVirtioQEMU
    VM_VIRTIO_QEMU
    "Enable virtio QEMU module"
    DEPENDS
    "VmPCISupport"
    DEFAULT
    OFF
)

add_config_library(tii_camkes_vm "${configure_string}")

file(
    GLOB
        tii_camkes_vm_sources
        src/*.c
        src/modules/*.c
if(KernelPlatformRpi4)
	src/plat/rpi4/*.c
else()
        src/plat/${KernelPlatform}/*.c
endif()
)

list(
    APPEND tii_camkes_vm_includes
    ${ARM_VM_PROJECT_DIR}/components/VM_Arm/include
)

add_library(tii_camkes_vm OBJECT ${tii_camkes_vm_sources})

target_include_directories(
    tii_camkes_vm
    PRIVATE ${tii_camkes_vm_includes}
    PUBLIC include
)
target_link_libraries(
    tii_camkes_vm
    fdt
    sel4_autoconf
    sel4vm
    tii_camkes_vm_Config
)

# Add VM application
add_subdirectory("apps/${AppArch}/${CAMKES_VM_APP}")
# Should be done adding targets, can now generate the root server and the global configuration
GenerateCAmkESRootserver()
