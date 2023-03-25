#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(vm-qemu-virtio C)

include(${CAMKES_ARM_VM_HELPERS_PATH})
# Let's make assumption for the workspace layout. Can be overridden in cmake
# invocation/cmake cache editor
set(VM_IMAGES_DIR "${TII_CAMKES_VM_DIR}/../../vm-images/build/tmp/deploy/images/vm-raspberrypi4-64" CACHE STRING "")
set(VM_IMAGE_LINUX "${VM_IMAGES_DIR}/Image" CACHE STRING "VM kernel image")
set(VM_IMAGE_INITRD "${VM_IMAGES_DIR}/vm-image-boot-vm-raspberrypi4-64.cpio.gz" CACHE STRING "VM initramfs")

AddToFileServer("linux" "${VM_IMAGE_LINUX}")
AddToFileServer("linux-initrd" ${VM_IMAGE_INITRD})

DefineCAmkESVMFileServer()

CAmkESAddImportPath(${KernelARMPlatform})

DeclareCAmkESComponent(
    VM
    SOURCES
    src/cross_vm_connection.c
    src/init_dataport_ram.c
    src/pl011.c
    src/qemu.c
    src/trace.c
    src/ioreq.c
    LIBS
    tii_camkes_vm
    virtioarm
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
