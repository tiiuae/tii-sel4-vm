# Release Instructions

This document holds information for on how Hypervisor team creates releases.
By following these instructions, any Hypervisor team member should be able to create a release.

## Naming scheme

Use following naming scheme to refer the release in *all* release content,
such as release documents, commits, filenames etc.

`sel4_tii_<version>`

* All lower case
* `version` follows [semantic versioning](https://semver.org/).

##### Example

`sel4_tii_0.1`

## Release Package

Release is a bzip2-compressed tar file with following layout:

```text
tii_sel4_<release>
├── bin                      - directory for non-boot binaries (e.g. rootfs)
│   └── boot                 - directory for boot binaries (e.g. seL4 images)
└── tii_sel4_<release>.md
```

## Creating a Release Manifest

Hypervisor team uses repo tool to manage source code revisions, and it largely
defines what is in the releases. For release to be reproducible, the
manifest must explicitly define git SHAs, or tags that are used to create up
the release - branches must not be used.

First create and checkout workspace, if you have not done so. Install required
tools by following [environment setup instructions].

```bash
$ # create workspace
$ export TII_SEL4_VERSION=<version>
$ export TII_SEL4_RELEASE=tii_sel4_${TII_SEL4_VERSION}
$ export WORKSPACE=~/sel4-release
$ mkdir -p "${WORKSPACE}" && cd "${WORKSPACE}"
$ repo init -u git@github.com:tiiuae/tii_sel4_manifest.git -b tii/development
$ repo sync
```

Checkout correct revisions from repositories; repeat this step as many times
as needed.

```bash
$ cd <path/to/repository>
$ git checkout <revision>
$ cd "$(repo --show-toplevel)"
```

Then create manifest `releases` within `tiiuae/tii_sel4_manifest` repository:

```bash
$ # create manifest
$ mkdir -p .repo/manifests/releases
$ repo manifest \
    -r \
    --suppress-upstream-revision \
    --suppress-dest-branch \
    -o ".repo/manifests/releases/${TII_SEL4_RELEASE}.xml"
```

Commit, and push to release candidate branch, and open pull request for the release, but do *NOT* merge the pull request yet:

```bash
$ cd .repo/manifests
$ git add releases/tii_sel4_<version>.xml
$ git commit -s -m "Manifest for release ${TII_SEL4_VERSION}"
$ git push origin "HEAD:release/${TII_SEL4_RELEASE}-rc1"
$ gh pr create -a "@me" -t "Release candidate for ${TII_SEL4_VERSION}"
```

## Building the Release

The release build *must* always be done on clean environment, with all
components built from scratch. This also applies to containers that may be
used during the process.

The build should primarily take place either in dedicated build machines or
on ephemeral environments, such as VMs or containers. To ensure
reproducibility, ephemeral environments are preferred over bare metal setups.

Initialize the environment for the release with the manifest created in the
previous step:

```bash
$ export TII_SEL4_VERSION=<version>
$ export TII_SEL4_RELEASE=tii_sel4_${TII_SEL4_VERSION}
$ export WORKSPACE=~/${TII_SEL4_RELEASE}
$ mkdir -p "${WORKSPACE}" && cd "${WORKSPACE}"
$ repo init \
    -u git@github.com:tiiuae/tii_sel4_manifest.git \
    -b "release/${TII_SEL4_RELEASE}-rc1" \
    -m "releases/${TII_SEL4_RELEASE}.xml"
$ repo sync
```

Build the containers:

```bash
$ make docker
```

Build root file systems and kernel images for VMs:

```bash
$ make shell
$ cd vm-images
$ . setup.sh
$ bitbake vm-image-driver
$ exit
$ # Copy kernel in place for seL4 builds
$ cp vm-images/build/tmp/deploy/images/vm-raspberrypi4-64/Image \
     projects/camkes-vm-images/rpi4
```

Build supported seL4 reference images:

```bash
$ make rpi4_defconfig
$ make vm_minimal
$ make vm_multi
$ make sel4test
```

Build supported TII seL4 images:

```bash
$ make vm_qemu_virtio
```

Collect artifacts:

```bash
$ mkdir -p "${TII_SEL4_RELEASE}/bin/boot"
$ repo manifest \
    -r \
    --suppress-upstream-revision \
    --suppress-dest-branch \
    -o "${TII_SEL4_RELEASE}/manifest.xml"
$ declare -A targets=(
    ["rpi4_vm_minimal/images/capdl-loader-image-arm-bcm2711"]=rpi4_vm_minimal
    ["rpi4_vm_multi/images/capdl-loader-image-arm-bcm2711"]=rpi4_vm_multi
    ["rpi4_sel4test/images/sel4test-driver-image-arm-bcm2711"]=rpi4_sel4test
    ["rpi4_vm_qemu_virtio/images/capdl-loader-image-arm-bcm2711"]=rpi4_vm_qemu_virtio
    )
$ for target in "${!targets[@]}"; do
    cp "$target" "${TII_SEL4_RELEASE}/bin/boot/${targets[$target]}"
  done
$ declare yocto_targets=(
    "vm-image-driver-vm-*.ext3"
    "vm-image-driver-vm-*.tar.bz2"
    "vm-image-driver-vm-*.manifest"
    "Image*"
    )
$ for target in "${yocto_targets[@]}"; do
    cp -P vm-images/build/tmp/deploy/images/vm-raspberrypi4-64/$target "${TII_SEL4_RELEASE}/bin/"
  done
$ declare yocto_boot_targets=(
    "u-boot.bin"
    "bcm2711-rpi-4-b.dtb"
    "bootcode.bin"
    "start4.elf"
    "fixup4.dat"
    "config.txt"
    )
$ for target in "${yocto_boot_targets[@]}"; do
    cp tii_sel4_build/hardware/rpi4/$target "${TII_SEL4_RELEASE}/bin/boot/"
  done
```

## Prepare Release Documentation

Create a copy of release documentation and use the instructions within
the template to fill out the release document:

```bash
$ cp projects/tii-sel4-vm/docs/release_doc_template.md ${TII_SEL4_RELEASE}/${TII_SEL4_RELEASE}.md
$ ${EDITOR} ${TII_SEL4_RELEASE}/${TII_SEL4_RELEASE}.md
```

## Release Testing

Release documents contain per image run instructions, and all binaries within
the release must be tested against the instructions. In addition, each release
must be tested against per image release test suite described in this chapter.

### Testing `vm_minimal`

Follow the release document run instructions. The VM should boot up and be
responsive.

### Testing `vm_multi`

Follow the release document run instructions. The all three VMs should boot up and
be responsive.

### Testing `sel4test`

Follow the release document run instructions. All tests should pass.

### Testing `vm_qemu_virtio`

Follow the release document run instructions.

Ensure that:
* All VMs are running and responsive (via serial connection)
* User VM Ethernet connection works (e.g. ping something outside the board)
* User VM user VM is able store and read persistent data via Storage VM

## Deploy Release

The release package directory contains the required contents, and that the
layout follows the one specified in chapter [Release Package](#release-package):

```bash
$ tree ${TII_SEL4_RELEASE}
tii_sel4_0.1/
├── bin
│   ├── boot
│   │   ├── bootcode.bin
│   │   ├── config.txt
│   │   ├── fixup4.dat
│   │   ├── rpi4_sel4test
│   │   ├── rpi4_vm_minimal
│   │   ├── rpi4_vm_multi
│   │   ├── start4.elf
│   │   └── u-boot.bin
│   ├── Image -> Image-1-5.10.83+git0+e1979ceb17_111a297d94-r0-vm-raspberrypi4-64-20220318110254.bin
│   ├── Image-1-5.10.83+git0+e1979ceb17_111a297d94-r0-vm-raspberrypi4-64-20220318110254.bin
│   ├── Image-vm-raspberrypi4-64.bin -> Image-1-5.10.83+git0+e1979ceb17_111a297d94-r0-vm-raspberrypi4-64-20220318110254.bin
│   ├── vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.ext3
│   ├── vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.manifest
│   ├── vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.tar.bz2
│   ├── vm-image-driver-vm-raspberrypi4-64.ext3 -> vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.ext3
│   ├── vm-image-driver-vm-raspberrypi4-64.manifest -> vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.manifest
│   └── vm-image-driver-vm-raspberrypi4-64.tar.bz2 -> vm-image-driver-vm-raspberrypi4-64-20220318110254.rootfs.tar.bz2
└── tii_sel4_0.1.md

2 directories, 18 files
```

Create release package:

```bash
$ fakeroot tar -cjf "${TII_SEL4_RELEASE}.tar.bz2" "${TII_SEL4_RELEASE}"
```

Finally upload the release to [TII JFrog artifactory TII seL4 release repository](https://artifactory.ssrcdevops.tii.ae:443/artifactory/tii-sel4-releases/). Make sure to use correct release version.

---

[environment setup instructions]: https://github.com/tiiuae/tii_sel4_build#setting-up-the-build-environment

