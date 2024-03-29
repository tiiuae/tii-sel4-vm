#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.12.0)
include(settings.cmake)
project(tii-camkes-vm C CXX ASM)

find_package(tii-camkes-vm REQUIRED)
tii_camkes_vm_setup()

file(
    GLOB
        tii_sel4vm_sources
        src/*.c
)

if(KernelPlatformRpi4)
    file(GLOB plat_deps src/plat/rpi4/*.c)
else()
    file(GLOB plat_deps src/plat/${KernelPlatform}/*.c)
endif()
list(APPEND tii_sel4vm_sources ${plat_deps})


add_library(tii_sel4vm OBJECT ${tii_sel4vm_sources})

target_include_directories(
    tii_sel4vm
    PUBLIC include
)
target_link_libraries(
    tii_sel4vm
    fdt
    sel4_autoconf
    sel4sync
    sel4vm
    sel4vmmplatsupport
    virtioarm
)

# Add VM application
add_subdirectory("apps/${AppArch}/${CAMKES_VM_APP}")
# Should be done adding targets, can now generate the root server and the global configuration
GenerateCAmkESRootserver()
