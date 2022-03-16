#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(tii_release C)

include(${CAMKES_ARM_VM_HELPERS_PATH})
find_package(camkes-vm-linux REQUIRED)
include(${CAMKES_VM_LINUX_HELPERS_PATH})

if("${PLATFORM}" STREQUAL "rpi4")
# Create our CPP Flags based on ARM VM config variables
    set(cpp_flags "-DKERNELARMPLATFORM_RPI4")
elseif("${PLATFORM}" STREQUAL "qemu-arm-virt")
    set(cpp_flags "-DKERNELARMPLATFORM_QEMU-ARM-VIRT")
    set(SIMULATION ON CACHE BOOL "Generate simulation script to run qemu with the proper arguments")
    include(simulation)
    if(SIMULATION)
        GenerateSimulateScript()
    endif()
endif()

# Rootfs files for each VM
set(rootfs_file_user_vm        "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/rootfs.cpio.gz")
set(rootfs_file_connection_vm  "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/rootfs.cpio.gz")
set(rootfs_file_storage_vm     "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/rootfs.cpio.gz")

# Setup User VM overlay
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_user_vm
)
AddFileToOverlayDir(
    "S90client_net_setup"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/client_vm_net_setup.sh
    "etc/init.d"
    overlay_user_vm
)
AddOverlayDirToRootfs(
    overlay_user_vm
    ${rootfs_file_user_vm}
    "buildroot"
    "rootfs_install"
    user_vm_output_overlayed_rootfs_location
    rootfs_target_user_vm
    GZIP
)

# Setup Connection VM overlays
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_connection_vm
)
AddFileToOverlayDir(
    "S90bridge_net_setup"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/connection_vm_bridge_setup.sh
    "etc/init.d"
    overlay_connection_vm
)
AddOverlayDirToRootfs(
    overlay_connection_vm
    ${rootfs_file_connection_vm}
    "buildroot"
    "rootfs_install"
    connection_vm_output_overlayed_rootfs_location
    rootfs_target_connection_vm
    GZIP
)

# Setup Storage VM overlays
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_storage_vm
)
AddOverlayDirToRootfs(
    overlay_storage_vm
    ${rootfs_file_storage_vm}
    "buildroot"
    "rootfs_install"
    storage_vm_output_overlayed_rootfs_location
    rootfs_target_storage_vm
    GZIP
)

# Add rootfs and linux images to FileServer
AddToFileServer(
    "linux-initrd-user-vm"
    "${user_vm_output_overlayed_rootfs_location}"
    DEPENDS
    rootfs_target_user_vm
)

AddToFileServer(
    "linux-initrd-connection-vm"
    "${connection_vm_output_overlayed_rootfs_location}"
    DEPENDS
    rootfs_target_connection_vm
)

AddToFileServer(
    "linux-initrd-storage-vm"
    "${storage_vm_output_overlayed_rootfs_location}"
    DEPENDS
    rootfs_target_storage_vm
)

AddToFileServer("linux-vm"            "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux")
AddToFileServer("linux-storage-vm"    "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux")

AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmOnDemandDeviceInstall VmEmmc2NoDMA)

DefineCAmkESVMFileServer()

#DeclareCAmkESComponent(VM SOURCES src/shared_memory.c)

CAmkESAddImportPath(${CMAKE_CURRENT_SOURCE_DIR}/${KernelARMPlatform})

# Declare root server
DeclareCAmkESRootserver(
    tii_release.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
