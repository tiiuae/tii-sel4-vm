#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(vm-qemu-virtio C)

include(${CAMKES_ARM_VM_HELPERS_PATH})
find_package(camkes-vm-linux REQUIRED)
include(${CAMKES_VM_LINUX_HELPERS_PATH})

AddToFileServer("linux" "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux")

set(rootfs_file "${CAMKES_VM_IMAGES_DIR}/rpi4/rootfs.cpio.gz")
AddToFileServer("linux-initrd" ${rootfs_file} )

DefineCAmkESVMFileServer()

CAmkESAddImportPath(${KernelARMPlatform})

DeclareCAmkESComponent(
    VM
    SOURCES
    src/cross_vm_connection.c
    src/init_dataport_ram.c
    src/qemu.c
    src/trace.c
    src/ioreq.c
)

CAmkESAddCPPInclude("${CMAKE_CURRENT_LIST_DIR}/configurations/")

# Declare root server
DeclareCAmkESRootserver(
    vm_qemu_virtio.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
