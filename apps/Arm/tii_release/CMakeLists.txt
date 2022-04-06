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


# Parent of build directory
get_filename_component(CMAKE_BINARY_DIR_PARENT ${CMAKE_BINARY_DIR} DIRECTORY)
set(guest_kernel_src_dir       "${CMAKE_BINARY_DIR_PARENT}/projects/torvalds/linux")
set(guest_kernel_build_dir     "${CMAKE_BINARY_DIR_PARENT}/linux-build")

execute_process(
    COMMAND bash "-c" "ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -C ${guest_kernel_src_dir} -s kernelversion"
    OUTPUT_VARIABLE guest_kernel_ver
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ECHO_OUTPUT_VARIABLE
    )
#message("Guest kernel version: ${guest_kernel_ver}")
set(connection_module          "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux-modules/lib/modules/${guest_kernel_ver}/extra/connection.ko")

# Prepare the Cross-VM scripts
set(cross_vm_test_script       "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_test.sh")
set(cross_vm_init_script_src   "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_module_init.template")
set(cross_vm_init_script_dest  "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_module_init.sh")

file(READ "${cross_vm_init_script_src}" cv_init_script_src)
string(REGEX REPLACE "\/__LINUX_VERSION__" "\/${guest_kernel_ver}" cv_init_script "${cv_init_script_src}")
file(WRITE "${cross_vm_init_script_dest}" "${cv_init_script}")

#DefineLinuxModule(
#    ${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-modules/camkes-connector-modules/connection
#    output_module
#    output_module_target
#    KERNEL_DIR
#    ${guest_kernel_build_dir}
#    ARCH
#    arm64
#    CROSS_COMPILE
#    aarch64-linux-gnu-
#)
message(FATAL_ERROR "test")

# Compile CrossVM dataport Apps
ExternalProject_Add(
    dataport-apps
    URL
    file:///${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-apps/camkes-connector-apps/pkgs/dataport
    BINARY_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/dataport_apps
    INSTALL_COMMAND
    ""
    BUILD_ALWAYS
    ON
    EXCLUDE_FROM_ALL
    CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DLINUX_KERNEL_DIR=${guest_kernel_src_dir}
)

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
AddExternalProjFilesToOverlay(
    dataport-apps
    ${CMAKE_CURRENT_BINARY_DIR}/dataport_apps
    overlay_user_vm
    "usr/bin"
    FILES
    dataport_read
    dataport_write
)
AddFileToOverlayDir(
    "connection.ko"
    ${connection_module}
    "lib/modules/${guest_kernel_ver}/kernel/drivers/vmm"
    overlay_user_vm
)
AddFileToOverlayDir(
    "S91cross_vm_init"
    ${cross_vm_init_script_dest}
    "etc/init.d"
    overlay_user_vm
)
AddFileToOverlayDir(
    "S92cross_vm_test"
    ${cross_vm_test_script}
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
AddExternalProjFilesToOverlay(
    dataport-apps
    ${CMAKE_CURRENT_BINARY_DIR}/dataport_apps
    overlay_storage_vm
    "usr/bin"
    FILES
    dataport_read
    dataport_write
)
AddFileToOverlayDir(
    "connection.ko"
    ${connection_module}
    "lib/modules/${guest_kernel_ver}/kernel/drivers/vmm"
    overlay_storage_vm
)
AddFileToOverlayDir(
    "S91cross_vm_init"
    ${cross_vm_init_script_dest}
    "etc/init.d"
    overlay_user_vm
)
AddFileToOverlayDir(
    "S92cross_vm_test"
    ${cross_vm_test_script}
    "etc/init.d"
    overlay_user_vm
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

AddToFileServer("linux-vm"         "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux")
AddToFileServer("linux-storage-vm" "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux")

AddCamkesCPPFlag(cpp_flags 
                 CONFIG_VARS
                 VmOnDemandDeviceInstall
                 VmEmmc2NoDMA
                 VmPCISupport
                 VmVirtioNetVirtqueue
                 VmVirtioConsole
                 VmInitRdFile)

DefineCAmkESVMFileServer()

# Add the shared_memory source file to only
# VM0 and VM2. The VM1 will not have the
# dataport buffer handles, and the linker
# will fail trying to find them.
#
DeclareCAmkESComponent(VM_instance_vm0 SOURCES src/shared_memory.c)
DeclareCAmkESComponent(VM_instance_vm2 SOURCES src/shared_memory.c)

CAmkESAddImportPath(${CMAKE_CURRENT_SOURCE_DIR}/${KernelARMPlatform})

# Declare root server
DeclareCAmkESRootserver(
    tii_release.camkes
    CPP_FLAGS
    ${cpp_flags}
    CPP_INCLUDES
    ${CAMKES_VM_DIR}/components/VM_Arm
)
