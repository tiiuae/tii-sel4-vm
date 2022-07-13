#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
# Copyright 2022, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(supported "rpi4")
if(NOT "${PLATFORM}" IN_LIST supported)
    message(FATAL_ERROR "PLATFORM: ${PLATFORM} not supported.
         Supported: ${supported}")
endif()

set(LibUSB OFF CACHE BOOL "" FORCE)
set(VmPCISupport ON CACHE BOOL "" FORCE)
set(VmVirtioConsole ON CACHE BOOL "" FORCE)
set(VmVirtioNetArping OFF CACHE BOOL "" FORCE)
set(VmVirtioNetVirtqueue OFF CACHE BOOL "" FORCE)
set(VmInitRdFile ON CACHE BOOL "" FORCE)
set(VmDtbFile OFF CACHE BOOL "" FORCE)
set(KernelCustomDTS
  "${CMAKE_CURRENT_LIST_DIR}/${PLATFORM}/dts/rpi4.dts"
  CACHE FILEPATH "" FORCE)
set(KernelCustomDTSOverlay
  "${CMAKE_CURRENT_LIST_DIR}/${PLATFORM}/dts/overlay-rpi4.dts"
  CACHE FILEPATH "" FORCE)
