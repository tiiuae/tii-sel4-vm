#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(vm-qemu-virtio C)

include(${TII_CAMKES_VM_HELPERS_PATH})

config_option(
    UserVmDtb
    USE_USERVM_DTB_FILE
    "Use provided user-vm dtb file instead of generated one"
    DEFAULT
    OFF
)
AddCamkesCPPFlag(cpp_flags CONFIG_VARS UserVmDtb)

AddToFileServer("linux" "${VM_IMAGE_LINUX}")
AddToFileServer("linux-initrd" ${VM_IMAGE_INITRD})

if("${KernelARMPlatform}" STREQUAL "qemu-arm-virt")
    include(simulation)
    set(SIMULATION ON CACHE BOOL "Generate simulation script to run qemu with the proper arguments")
    if(SIMULATION)
        GenerateSimulateScript()
    endif()
endif()

if(UserVmDtb)
    set(USERVM_DTB_FILE "${CMAKE_CURRENT_LIST_DIR}/${PLATFORM}/user-vm.dtb"
        CACHE STRING "User-vm device tree to use over generated one")

    AddToFileServer("user-vm.dtb" "${USERVM_DTB_FILE}")
endif()

DefineCAmkESVMFileServer()

DeclareTIICAmkESVM(VM0)
DeclareTIICAmkESVM(VM1)

# Declare root server
DeclareCAmkESRootserver(
    vm_qemu_virtio.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
