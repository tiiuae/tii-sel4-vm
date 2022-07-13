#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(camkes-vm-minimal C)

include(${CAMKES_ARM_VM_HELPERS_PATH})

find_package(camkes-vm-linux REQUIRED)
include(${CAMKES_VM_LINUX_HELPERS_PATH})
set(cpp_flags "-DKERNELARMPLATFORM_RPI4")

# Add kernel image to file server
AddToFileServer("linux" "${CAMKES_VM_IMAGES_DIR}/rpi4/linux")

# Generate overlayed rootfs
set(rootfs_file "${CAMKES_VM_IMAGES_DIR}/rpi4/rootfs.cpio.gz")
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_ttys0
    "etc"
    overlay_vm
)
AddOverlayDirToRootfs(
    overlay_vm
    ${rootfs_file}
    "buildroot"
    "rootfs_install"
    vm_output_overlayed_rootfs_location
    rootfs_target_vm
    GZIP
)
AddToFileServer(
    "linux-initrd"
    "${vm_output_overlayed_rootfs_location}"
    DEPENDS
    rootfs_target_vm
)

#AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmEmmc2NoDMA VmVUSB Tk1DeviceFwd Tk1Insecure)

DefineCAmkESVMFileServer()

CAmkESAddImportPath(${KernelARMPlatform})

# Declare root server
DeclareCAmkESRootserver(
    vm_single_test_serial.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
