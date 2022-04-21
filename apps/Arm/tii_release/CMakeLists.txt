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
include(${CAMKES_VM_LINUX_SOURCE_HELPERS_PATH})
include(${CAMKES_VM_LINUX_MODULE_HELPERS_PATH})
include(ExternalProject)

set(linux_ver "")

# Create our CPP Flags based on ARM VM config variables
if("${PLATFORM}" STREQUAL "rpi4")

    set(cpp_flags "-DKERNELARMPLATFORM_RPI4")

    # Parent of build directory
    get_filename_component(CMAKE_BINARY_DIR_PARENT ${CMAKE_BINARY_DIR} DIRECTORY)
    set(linux_src_dir       "${CMAKE_BINARY_DIR_PARENT}/projects/torvalds/linux")
    set(linux_build_dir     "${CMAKE_BINARY_DIR_PARENT}/guest_component_builds/linux-build")
    set(linux_repo          "file:///${CMAKE_BINARY_DIR_PARENT}/projects/torvalds/linux")
    set(linux_arch          "arm64")
    set(linux_cross_compile "aarch64-linux-gnu-")

    # Get the kernel version
    # NOTE! The ECHO_OUTPUT_VARIABLE needs
    # to stay, otherwise everything breaks 
    # for some unknown reason.
    execute_process(
        COMMAND bash "-c" "ARCH=${linux_arch} CROSS_COMPILE=${linux_cross_compile} make -C ${linux_src_dir} -s kernelversion"
        OUTPUT_VARIABLE linux_ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ECHO_OUTPUT_VARIABLE
    )

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

#set(connection_module          "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux-modules/lib/modules/${linux_ver}/extra/connection.ko")

# Prepare CrossVM connection module related stuff
set(connection_module_name "connection.ko")
set(connection_module_path "lib/modules/${linux_ver}/kernel/drivers/vmm")

set(uio_module_name        "uio.ko")
set(uio_module_src_path    "${CAMKES_VM_IMAGES_DIR}/${KernelARMPlatform}/linux-modules/lib/modules/${linux_ver}/kernel/drivers/uio/uio.ko")
set(uio_module_dest_path   "lib/modules/${linux_ver}/kernel/drivers/uio")

# Prepare the Cross-VM scripts
set(cross_vm_init_script_in    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_init.sh.in")
set(cross_vm_init_script       "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_init.sh")
set(cross_vm_test_script       "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/cross_vm_test.sh")

# Edit the correct Linux version
# to init script and write the
# result into a file
execute_process(
    COMMAND cat "${cross_vm_init_script_in}"
    COMMAND sed -r "s|^([^#][a-zA-Z /]{1,}\\/)__LINUX_VERSION__(\\/.{1,})$|\\1${linux_ver}\\2|g"
    COMMAND tee "${cross_vm_init_script}"
    COMMAND chmod 0775 "${cross_vm_init_script}"
)

# Compile CrossVM connection module
DefineLinuxModule(
    ${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-modules/camkes-connector-modules/connection
    connection_module
    connection_module_target
    KERNEL_DIR
    ${linux_build_dir}
    ARCH
    ${linux_arch}
    CROSS_COMPILE
    ${linux_cross_compile}
)


# Compile CrossVM dataport apps
ExternalProject_Add(
    dataport-apps
    URL
    file:///${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-apps/camkes-connector-apps/pkgs/dataport
    PREFIX
    ${CMAKE_CURRENT_BINARY_DIR}/dataport-apps
    INSTALL_COMMAND
    ""
    BUILD_ALWAYS
    ON
    EXCLUDE_FROM_ALL
    CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)

# Compile CrossVM emit event app
ExternalProject_Add(
    emit-event-app
    URL
    file:///${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-apps/camkes-connector-apps/pkgs/emits_event
    PREFIX
    ${CMAKE_CURRENT_BINARY_DIR}/emit-event-app
    INSTALL_COMMAND
    ""
    BUILD_ALWAYS
    ON
    EXCLUDE_FROM_ALL
    CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)

# Compile CrossVM consume event app
ExternalProject_Add(
    consume-event-app
    URL
    file:///${CAMKES_VM_LINUX_DIR}/camkes-linux-artifacts/camkes-linux-apps/camkes-connector-apps/pkgs/consumes_event
    PREFIX
    ${CMAKE_CURRENT_BINARY_DIR}/consume-event-app
    INSTALL_COMMAND
    ""
    BUILD_ALWAYS
    ON
    EXCLUDE_FROM_ALL
    CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)

# Setup User VM overlay

# Add inittab
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_user_vm
)

# Add network init script
AddFileToOverlayDir(
    "S90client_net_setup"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/client_vm_net_setup.sh
    "etc/init.d"
    overlay_user_vm
)

# Add CrossVM dataport apps
AddExternalProjFilesToOverlay(
    dataport-apps
    ${CMAKE_CURRENT_BINARY_DIR}/dataport-apps/src/dataport-apps-build
    overlay_user_vm
    "usr/bin"
    FILES
    dataport_read
    dataport_write
)

# Add CrossVM emit event app
AddExternalProjFilesToOverlay(
    emit-event-app
    ${CMAKE_CURRENT_BINARY_DIR}/emit-event-app/src/emit-event-app-build
    overlay_user_vm
    "usr/bin"
    FILES
    emits_event_emit
)

# Add CrossVM consume event app
AddExternalProjFilesToOverlay(
    consume-event-app
    ${CMAKE_CURRENT_BINARY_DIR}/consume-event-app/src/consume-event-app-build
    overlay_user_vm
    "usr/bin"
    FILES
    consumes_event_wait
)

# Add UIO and CrossVM connection modules,
# and init/test scripts
AddFileToOverlayDir(
    "${uio_module_name}"
    "${uio_module_src_path}"
    "${uio_module_dest_path}"
    overlay_user_vm
)
AddFileToOverlayDir(
    "${connection_module_name}"
    "${connection_module}"
    "${connection_module_path}"
    overlay_user_vm
    DEPENDS
    connection_module_target
)
AddFileToOverlayDir(
    "S91cross_vm_init"
    ${cross_vm_init_script}
    "etc/init.d"
    overlay_user_vm
)
AddFileToOverlayDir(
    "S92cross_vm_test"
    ${cross_vm_test_script}
    "etc/init.d"
    overlay_user_vm
)

# Smush the overlay on the rootfs
AddOverlayDirToRootfs(
    overlay_user_vm
    ${rootfs_file_user_vm}
    "buildroot"
    "rootfs_install"
    user_vm_output_overlayed_rootfs_location
    rootfs_target_user_vm
    GZIP
)



# Setup Connection VM overlay

# Add inittab
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_connection_vm
)

# Add network init script
AddFileToOverlayDir(
    "S90bridge_net_setup"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/connection_vm_bridge_setup.sh
    "etc/init.d"
    overlay_connection_vm
)

# Smush the overlay on the rootfs
AddOverlayDirToRootfs(
    overlay_connection_vm
    ${rootfs_file_connection_vm}
    "buildroot"
    "rootfs_install"
    connection_vm_output_overlayed_rootfs_location
    rootfs_target_connection_vm
    GZIP
)



# Setup Storage VM overlay

# Add inittab
AddFileToOverlayDir(
    "inittab"
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/inittab_hvc0
    "etc"
    overlay_storage_vm
)

# Add CrossVM dataport apps
AddExternalProjFilesToOverlay(
    dataport-apps
    ${CMAKE_CURRENT_BINARY_DIR}/dataport-apps/src/dataport-apps-build
    overlay_storage_vm
    "usr/bin"
    FILES
    dataport_read
    dataport_write
)

# Add CrossVM emit event app
AddExternalProjFilesToOverlay(
    emit-event-app
    ${CMAKE_CURRENT_BINARY_DIR}/emit-event-app/src/emit-event-app-build
    overlay_storage_vm
    "usr/bin"
    FILES
    emits_event_emit
)

# Add CrossVM consume event app
AddExternalProjFilesToOverlay(
    consume-event-app
    ${CMAKE_CURRENT_BINARY_DIR}/consume-event-app/src/consume-event-app-build
    overlay_storage_vm
    "usr/bin"
    FILES
    consumes_event_wait
)

# Add UIO and CrossVM connection modules,
# and init/test scripts
AddFileToOverlayDir(
    "${uio_module_name}"
    "${uio_module_src_path}"
    "${uio_module_dest_path}"
    overlay_storage_vm
)
AddFileToOverlayDir(
    "${connection_module_name}"
    "${connection_module}"
    "${connection_module_path}"
    overlay_storage_vm
    DEPENDS
    connection_module_target
)
AddFileToOverlayDir(
    "S91cross_vm_init"
    ${cross_vm_init_script}
    "etc/init.d"
    overlay_storage_vm
)
AddFileToOverlayDir(
    "S92cross_vm_test"
    ${cross_vm_test_script}
    "etc/init.d"
    overlay_storage_vm
)

# Smush the overlay on the rootfs
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
