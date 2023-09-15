#
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: Apache-2.0
#

include(${CAMKES_ARM_VM_HELPERS_PATH})

# Let's make assumption for the workspace layout. Can be overridden in cmake
# invocation/cmake cache editor
set(VM_IMAGES_DIR "${TII_CAMKES_VM_DIR}/../../vm-images/build/tmp/deploy/images/vm-raspberrypi4-64" CACHE STRING "")
set(VM_IMAGE_LINUX "${VM_IMAGES_DIR}/Image" CACHE STRING "VM kernel image")
set(VM_IMAGE_INITRD "${VM_IMAGES_DIR}/vm-image-boot-vm-raspberrypi4-64.cpio.gz" CACHE STRING "VM initramfs")

CAmkESAddImportPath(${KernelARMPlatform})

CAmkESAddTemplatesPath(${TII_CAMKES_VM_DIR}/templates)

CAmkESAddCPPInclude(${TII_CAMKES_VM_DIR})

set(CAmkESCPP ON CACHE BOOL "" FORCE)

set(configure_string "")

add_config_library(tii_camkes_vm "${configure_string}")

config_option(
    VmSWIOTLB
    VM_SWIOTLB
    "Compile examples with SWIOTLB enabled"
    DEFAULT
    ON
)

AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmSWIOTLB)

file(
    GLOB
        tii_camkes_vm_sources
        ${TII_CAMKES_VM_DIR}/src/camkes/*.c
        ${TII_CAMKES_VM_DIR}/src/camkes/modules/*.c
)

function(DeclareTIICAmkESVM name)
    DeclareCAmkESARMVM(${name})
    DeclareCAmkESComponent(
        ${name}
        SOURCES
        ${tii_camkes_vm_sources}
        LIBS
        tii_sel4vm
        tii_camkes_vm_Config
        virtioarm
        C_FLAGS
        "-DSEL4_VMM"
        TEMPLATE_SOURCES
        seL4VirtIODeviceVM.template.c
        seL4VirtIODriverVM.template.c
        TEMPLATE_HEADERS
        seL4VirtIODeviceVM.template.h
    )
endfunction(DeclareTIICAmkESVM)
