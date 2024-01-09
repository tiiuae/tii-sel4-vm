#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, 2023, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(supported "exynos5422;qemu-arm-virt;rpi4")
if(NOT "${PLATFORM}" IN_LIST supported)
    message(FATAL_ERROR "PLATFORM: ${PLATFORM} not supported.
         Supported: ${supported}")
endif()
set(LibUSB OFF CACHE BOOL "" FORCE)
set(VmPCISupport ON CACHE BOOL "" FORCE)
set(VmVirtioConsole OFF CACHE BOOL "" FORCE)
set(VmVirtioQEMU ON CACHE BOOL "" FORCE)
set(VmVirtioNetArping OFF CACHE BOOL "" FORCE)
set(VmVirtioNetVirtqueue OFF CACHE BOOL "" FORCE)
set(VmInitRdFile ON CACHE BOOL "" FORCE)
if("${PLATFORM}" STREQUAL "qemu-arm-virt")
    set(VM_IMAGE_MACHINE "qemuarm64")

    set(ARM_CPU "cortex-a57")
    set(QEMU_MEMORY "2048")

    set(qemu_sim_extra_args "-smp 2 -netdev user,id=net0 -device virtio-net-pci,netdev=net0,mac=52:55:00:d1:55:01 -drive id=disk0,file=${VM_IMAGES_DIR}/${VM_IMAGE_MACHINE}/vm-image-driver-${VM_IMAGE_MACHINE}.ext4,if=none,format=raw -device virtio-blk-pci,drive=disk0")

    set(KernelArmExportPCNTUser ON CACHE BOOL "" FORCE)
    set(KernelArmExportPTMRUser ON CACHE BOOL "" FORCE)

elseif("${PLATFORM}" STREQUAL "rpi4")
    set(VM_IMAGE_MACHINE "vm-raspberrypi4-64")

    set(KernelCustomDTS
        "${CMAKE_CURRENT_LIST_DIR}/../../../hardware/${PLATFORM}/dts/rpi4.dts"
        CACHE FILEPATH "" FORCE)
    set(KernelCustomDTSOverlay
        "${CMAKE_CURRENT_LIST_DIR}/../../../hardware/${PLATFORM}/dts/overlay-rpi4.dts"
        CACHE FILEPATH "" FORCE)
endif()
