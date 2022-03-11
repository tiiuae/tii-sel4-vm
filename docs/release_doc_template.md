# TII seL4 release <version> (YYYY-MM-DD)

## Overview

*Describe the release on high level. All H2 level headings should be in
release document. If there were no updates, it should be stated verbally e.g. 'No updates.' or 'No deprecations.' etc. H3 and higher are optional, more detailed descriptions. This file should be saved as `tii_sel4_<release>.md`*

Following HW targets are supported by this release:

* Raspberry Pi 4 8GB

## Changes

The release includes following changes:

* feature: Short description, or link(s) to PRs/Jira/section within document.
* feature: [This can be a link to sub-heading](#feature-xyz)
* fix: Description of a bug or link to PRs/Jira/section within document
* change: Description of a changed feature or link to PRs/Jira/section within document
* deprecation: Description of a deprecation or link to PRs/Jira/section within document

### Feature XYZ

*Describe big important features in dedicated sections.*

## Limitations

*Document any limitations, such as HW imposed limitations, experimental features etc.*

The release has following limitations:

* Short description of a limitation, or link(s) to PRs/Jira items/section within document.

*Use this if no deprecations features*
* No limitations.

## Known Issues

*List of known open bugs. Copy known issues from previous release, and move
fixed/invalid ones to other sections of this document. Append new non-fixed
bugs here.*

Known issues:

* Short description, or link(s) to PRs/Jira items/section within document.

*Use this if no known issues*
* No known issues.


## Build instructions

*Update <SHA1> to point the release HEAD, and make sure that the link works.*

The release can be build by following [release build instructions](https://github.com/tiiuae/tii-sel4-vm/blob/<SHA1>/docs/release_instructions.md#building-the-release).

## Run Instructions

This sections describes the how to run images supported by this release.

The `vm_minimal`, `vm_multi`, `sel4test` images are target defined by upstream. The images are used to validate board adaptations, and other changes applied to the system software.

### Environment setup instructions

Running the images requires

* Raspberry Pi 4, 8GB model
* Micro SD-card
* Ethernet cable

In addition following services are required
* SD-card reader for creating bootable SD-card
* dhcp server advertising `root-path` to nfs server, f.ex.
  `option root-path "192.168.5.1:/srv/nfs/rpi4,vers=3,proto=tcp";`
* nfs server

Setting up the services are out of scope of this document.

#### Preparing SD-card

Create file boot system image, loop mount and copy boot files:

```bash
$ dd if=/dev/zero of=sel4_disk.img bs=1M count=128
$ parted sel4_disk.img \
    -a optimal \
    -s mklabel msdos \
    -- mkpart primary fat32 2048s -1s
$ sudo losetup -f --show -P sel4_disk.img
/dev/loop<number>
$ sudo mkfs.fat -F 32 -n BOOT /dev/loop<number>p1
$ mkdir boot
$ sudo mount -t auto -o loop /dev/loop<number>p1 boot/
$ sudo tar -C boot/ --strip-components=3 -xjf tii_sel4_<version>.tar.bz2 tii_sel4_<version>/bin/boot
$ sudo umount boot/
$ sudo losetup -d /dev/loop<number>
```

Insert micro SD-card to reader and write the disk image:

```bash
$ lsblk # list block devices
$ dd if=sel4_disk.img of=/dev/<mmc device> bs=1M
```

Boot the board for the first time, and enter following commands:

```text
U-Boot> setenv sel4_bootfile rpi4_vm_minimal
U-Boot> setenv sel4_bootcmd_mmc 'fatload mmc 0 ${loadaddr} ${sel4_bootfile}; bootelf ${loadaddr}'
U-Boot> setenv bootcmd 'run sel4_bootcmd_mmc'
U-Boot> saveenv
```

This sets u-boot `rpi4_vm_minimal` the default boot target. Other images can
be booted with `setenv sel4_bootfile <image>`.

#### Preparing NFS server to serve rootfs

Either unpack, or loop mount image's rootfs to a path NFS server is serving,
for example:

```bash
$ # unpack...
$ tar -C /srv/nfs/rpi4 -xjpvf <image>-raspberrypi4-64.tar.bz2
$ # ... or loop mount
$ systemctl stop nfs-server
$ sudo mount -o loop <image>-raspberrypi4-64.ext3 /srv/nfs/rpi4
$ sudo systemctl start nfs-server
```

### `vm_minimal`

The purpose of `vm_minimal` image is to demonstrate hypervisor capabilities of seL4
and CAmkES in a 'Hello World' fashion. The image boots up single Linux VM.

Power on the device, and interrupt `u-boot` auto-boot by pressing any key. In
u-boot shell, type following commands:

```text
Hit any key to stop autoboot:  0
U-Boot> setenv sel4_bootfile=rpi4_vm_minimal
U-Boot> boot
...
```

The image should boot to Linux login. Typing `root` followed by enter logins
to shell.

```
Poky (Yocto Project Reference Distro) 3.4 vm-raspberrypi4-64 /dev/hvc0

vm-raspberrypi4-64 login: root
root@vm-raspberrypi4-64:~#
```

### `vm_multi`

The `vm_multi` image demonstrates running multiple VMs with seL4
and CAmkES. The image boots up three Linux VMs, each with it's own VMM.

Power on the device, and interrupt `u-boot` auto-boot by pressing any key. In
u-boot shell, type following commands:

```text
Hit any key to stop autoboot:  0
U-Boot> setenv sel4_bootfile=rpi4_vm_multi
U-Boot> boot
...
```

The image should boot to one of the Linux VM guests. Typing `root` followed by enter logins
to shell. Serial is multiplexed across VMs, and `@<number>` can be used to
switch between the VMs:

```text
Welcome to Buildroot
buildroot login: root
# cat /proc/iomem |grep "System RAM"
50000000-57ffffff : System RAM
#
Switching input to 1

Welcome to Buildroot
buildroot login: root
# cat /proc/iomem |grep "System RAM"
40000000-47ffffff : System RAM
Switching input to 2

Welcome to Buildroot
buildroot login: root
# cat /proc/iomem |grep "System RAM"
48000000-4fffffff : System RAM
Switching input to 0

#
```

### `sel4test`

The `sel4test` image contains collection of tests for seL4 system.

Power on the device, and interrupt `u-boot` auto-boot by pressing any key. In
u-boot shell, type following commands:

```text
Hit any key to stop autoboot:  0
U-Boot> setenv sel4_bootfile=rpi4_sel4test
U-Boot> boot
...
```

A successful test run should eventually produce a print similar to:

```text
Starting test 129: Test all tests ran
Test suite passed. 129 tests passed. 41 tests disabled.
All is well in the universe
```

### `tii_release`

The `tii_release` image forms a system where VMs are isolated per function.
The VMs provide system with multiplexed access to HW resources as a service,
such as network connectivity, or consume services provided by other VMs for
example to provide user facing application or functionality.

Currently `tii_release` provides three VMs:
* User VM
* Connection VM
* Storage VM

Power on the device, and interrupt `u-boot` auto-boot by pressing any key. In
u-boot shell, type following commands:

```text
Hit any key to stop autoboot:  0
U-Boot> setenv sel4_bootfile=rpi4_sel4test
U-Boot> boot
...
```

Switch between VMs with `@<number>`. The VM can be identified with console
prompt.
