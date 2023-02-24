#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, Technology Innovation Institute
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
set(LibSel4VMAlignLPHack ON CACHE BOOL "" FORCE)
set(VmVirtioNetArping OFF CACHE BOOL "" FORCE)
set(VmVirtioNetVirtqueue OFF CACHE BOOL "" FORCE)
set(VmInitRdFile ON CACHE BOOL "" FORCE)
if("${PLATFORM}" STREQUAL "qemu-arm-virt")
    set(QEMU_MEMORY "2048")
    set(KernelArmCPU cortex-a53 CACHE STRING "" FORCE)
    set(qemu_sim_extra_args "-netdev tap,id=mynet0,ifname=tap0,script=no,downscript=no -device virtio-net,netdev=mynet0,mac=52:55:00:d1:55:01")
    set(KernelArmExportPCNTUser ON CACHE BOOL "" FORCE)
    set(KernelArmExportPTMRUser ON CACHE BOOL "" FORCE)
elseif("${PLATFORM}" STREQUAL "rpi4")
    set(KernelCustomDTS
        "${CMAKE_CURRENT_LIST_DIR}/${PLATFORM}/dts/rpi4.dts"
        CACHE FILEPATH "" FORCE)
    set(KernelCustomDTSOverlay
        "${CMAKE_CURRENT_LIST_DIR}/${PLATFORM}/dts/overlay-rpi4.dts"
        CACHE FILEPATH "" FORCE)
endif()
