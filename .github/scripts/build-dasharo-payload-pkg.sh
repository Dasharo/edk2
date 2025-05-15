#!/usr/bin/env bash

set -e

# edksetup.sh interprets the parameters passed to the script and aborts if it
# doesn't like them, so abort early if any are passed in to avoid confusion
if [ $# -ne 0 ]; then
    echo "The $0 script doesn't accept any parameters"
    exit 1
fi

make -C BaseTools
source ./edksetup.sh

export EDK2_PLATFORMS_PATH="$WORKSPACE/edk2-platforms"
export PACKAGES_PATH="$WORKSPACE:\
$WORKSPACE/ipxe/src/bin-x86_64-efi-sb:\
$EDK2_PLATFORMS_PATH/Platform/Intel:\
$EDK2_PLATFORMS_PATH/Silicon/Intel:\
$EDK2_PLATFORMS_PATH/Features/Intel:\
$EDK2_PLATFORMS_PATH/Features/Intel/Debugging:\
$EDK2_PLATFORMS_PATH/Features/Intel/Network:\
$EDK2_PLATFORMS_PATH/Features/Intel/OutOfBandManagement:\
$EDK2_PLATFORMS_PATH/Features/Intel/PowerManagement:\
$EDK2_PLATFORMS_PATH/Features/Intel/SystemInformation:\
$EDK2_PLATFORMS_PATH/Features/Intel/UserInterface:\
$EDK2_PLATFORMS_PATH/Drivers"

function build_dasharopayloadpkg_64() {
    build -a IA32 -a X64 -t GCC5 \
          -D ABOVE_4G_MEMORY=FALSE \
          -D BOOTLOADER=COREBOOT \
          -D BOOTSPLASH_IMAGE=TRUE \
          -D CAPSULE_SUPPORT=TRUE \
          -D CAPSULE_MAIN_FW_GUID=32a75a2a-17ff-4d1b-88c2-e9dff5db53e7 \
          -D CPU_TIMER_LIB_ENABLE=FALSE \
          -D DASHARO_SYSTEM_FEATURES_ENABLE=TRUE \
          -D DISABLE_SERIAL_TERMINAL=FALSE \
          -D NETWORK_IPXE=TRUE \
          -D OPAL_PASSWORD_ENABLE=TRUE \
          -D PERFORMANCE_MEASUREMENT_ENABLE=TRUE \
          -D PRIORITIZE_INTERNAL=TRUE \
          -D PS2_KEYBOARD_ENABLE=TRUE \
          -D SATA_PASSWORD_ENABLE=TRUE \
          -D SECURE_BOOT_DEFAULT_ENABLE=FALSE \
          -D SECURE_BOOT_ENABLE=TRUE \
          -D SERIAL_TERMINAL=TRUE \
          -D SETUP_PASSWORD_ENABLE=TRUE \
          -D SIO_BUS_ENABLE=TRUE \
          -D UART_ON_SUPERIO=TRUE \
          -D USE_CBMEM_FOR_CONSOLE=TRUE \
          -p DasharoPayloadPkg/DasharoPayloadPkg.dsc \
          "$@"
}

# According to DasharoPayloadPkg/DasharoPayloadPkg.fdf:
# SECTION PE32 = DasharoPayloadPkg/NetworkDrivers/ipxe.efi
mkdir -p ./DasharoPayloadPkg/NetworkDrivers
cp ./ipxe/src/bin-x86_64-efi-sb/ipxe.efi ./DasharoPayloadPkg/NetworkDrivers/

build_dasharopayloadpkg_64 -b RELEASE
build_dasharopayloadpkg_64 -b DEBUG
