#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(supported "rpi4")
if(NOT "${PLATFORM}" IN_LIST supported)
    message(FATAL_ERROR "PLATFORM: ${PLATFORM} not supported.
         Supported: ${supported}")
endif()

set(LibUSB OFF CACHE BOOL "" FORCE)

if(${PLATFORM} STREQUAL "rpi4")
    set(VmInitRdFile OFF CACHE BOOL "" FORCE)
    set(VmDtbFile ON CACHE BOOL "" FORCE)
    set(VmPCISupport ON CACHE BOOL "" FORCE)
    set(VmVirtioConsole OFF CACHE BOOL "" FORCE)
    
    # Keep these flags as reminder if needed later during debug
    #set(LibUtilsDefaultZfLogLevel "0" CACHE STRING "" FORCE)
    #set(CapDLLoaderPrintDeviceInfo ON CACHE BOOL "" FORCE)
    #set(CapDLLoaderPrintUntypeds ON CACHE BOOL "" FORCE)
    #set(CapDLLoaderPrintCapDLObjects ON CACHE BOOL "" FORCE)
endif()
