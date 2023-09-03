#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(vm-qemu-virtio C)

include(${TII_CAMKES_VM_HELPERS_PATH})

AddToFileServer("linux" "${VM_IMAGE_LINUX}")
AddToFileServer("linux-initrd" ${VM_IMAGE_INITRD})

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
