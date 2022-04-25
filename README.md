# TII seL4 release 0.1

Build environment setup and building

## Setting up the environment

### Setup build directory

<pre>
# Choose a working directory, this will be visible in the container at /workspace
# (the WORKSPACE variable will point to /workspace as well inside the container)
host% <b>export WORKSPACE=~/sel4</b>

host% <b>mkdir ${WORKSPACE} && cd ${WORKSPACE}</b>
host% <b>repo init -u git@github.com:tiiuae/tii_sel4_manifest.git -b tii/release -m tii-release.xml</b>
host% <b>repo sync</b>
</pre>

### Compiling
<pre>
# Create a build directory for VM/guest components (not automated yet)
# in the WORKSPACE directory
host% <b>mkdir -p guest_component_builds</b>

# Compile prerequisites
host% <b>make tii_release_guest_components</b>

# Compile the release
host% <b>make tii_release</b>
</pre>

### Booting

The release is intended to be booted with TFTP.
For now, this guide assumes that you have a working
TFTP server already set up on your PC.

<pre>
# Copy all files from <b>'rpi4_tii_release/images'</b> directory
# to your TFTP share directory. The build system creates a
# boot script for U-Boot, <b>'boot.scr.uimg'</b> which handles
# downloading and booting the image from the TFTP server.

# Set up the U-Boot accordingly to download the script from your
# TFTP server and run it. Example:
u-boot% <b>setenv boot_tftp 'if tftp ${scriptaddr} boot.scr.uimg; then source ${scriptaddr}; fi'</b>
u-boot% <b>setenv bootcmd 'run boot_tftp'</b>
u-boot% <b>saveenv</b>

# If everything is setup correctly (files on TFTP share etc), you
# may boot the system directly from U-Boot terminal. 
u-boot% <b>boot</b>

# You can also power off the RPi4 and power it on when you are ready to boot
</pre>
