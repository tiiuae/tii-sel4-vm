#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#
cmake_minimum_required(VERSION 3.7.2)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/apps/Arm/${CAMKES_VM_APP}")
    set(AppArch "Arm" CACHE STRING "" FORCE)
else()
    message(FATAL_ERROR "App does not exist for supported architecture")
endif()

if(AppArch STREQUAL "Arm")
    set(CAMKES_ARM_LINUX_DIR "${CMAKE_CURRENT_LIST_DIR}/linux" CACHE STRING "")

    set(project_dir "${CMAKE_CURRENT_LIST_DIR}/../../")
    file(GLOB project_modules ${project_dir}/projects/*)
    list(
        APPEND
            CMAKE_MODULE_PATH
            ${project_dir}/kernel
            ${project_dir}/tools/seL4/cmake-tool/helpers/
            ${project_dir}/tools/seL4/elfloader-tool/
            ${project_modules}
    )
    set(SEL4_CONFIG_DEFAULT_ADVANCED ON)
    set(CAMKES_CONFIG_DEFAULT_ADVANCED ON)
    mark_as_advanced(CMAKE_INSTALL_PREFIX)
    include(application_settings)

    include(${CMAKE_CURRENT_LIST_DIR}/easy-settings.cmake)

    # Kernel settings
    set(KernelArch "arm" CACHE STRING "" FORCE)
    if(AARCH64)
        set(KernelSel4Arch "aarch64" CACHE STRING "" FORCE)
    endif()
    set(KernelArmHypervisorSupport ON CACHE BOOL "" FORCE)
    set(KernelRootCNodeSizeBits 18 CACHE STRING "" FORCE)
    set(KernelArmVtimerUpdateVOffset OFF CACHE BOOL "" FORCE)
    set(KernelArmDisableWFIWFETraps ON CACHE BOOL "" FORCE)

    # capDL settings
    set(CapDLLoaderMaxObjects 90000 CACHE STRING "" FORCE)

    # CAmkES Settings
    set(CAmkESCPP ON CACHE BOOL "" FORCE)

    # Release settings
    # message(FATAL_ERROR "release is   ${RELEASE}")
    ApplyCommonReleaseVerificationSettings(${RELEASE} FALSE)

    if(NOT CAMKES_VM_APP)
        message(
            FATAL_ERROR
                "CAMKES_VM_APP is not defined. Pass CAMKES_VM_APP to specify the VM application to build e.g. vm_minimal, odroid_vm"
        )
    endif()

    # Add VM application
    include("${CMAKE_CURRENT_LIST_DIR}/apps/Arm/${CAMKES_VM_APP}/settings.cmake")

    correct_platform_strings()

    find_package(seL4 REQUIRED)
    sel4_configure_platform_settings()

    ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})

    if(NUM_NODES MATCHES "^[0-9]+$")
        set(KernelMaxNumNodes ${NUM_NODES} CACHE STRING "" FORCE)
    else()
        set(KernelMaxNumNodes 1 CACHE STRING "" FORCE)
    endif()
else()
    message(FATAL_ERROR "Unsupported Setting")
endif()
