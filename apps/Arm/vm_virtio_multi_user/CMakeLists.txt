# Copyright 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.8.2)

project(vm-virtio-multi-user C)

include(${TII_CAMKES_VM_HELPERS_PATH})

AddToFileServer("linux" "${VM_IMAGE_LINUX}")
AddToFileServer("linux-initrd" ${VM_IMAGE_INITRD})

DefineCAmkESVMFileServer()

DeclareTIICAmkESVM(VM0)
DeclareTIICAmkESVM(VM1)
DeclareTIICAmkESVM(VM2)

# Declare root server
DeclareCAmkESRootserver(
    vm_virtio_multi_user.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
