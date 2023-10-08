#!/usr/bin/env bash

make -C BaseTools
source ./edksetup.sh

export EDK2_PLATFORMS_PATH="$WORKSPACE/edk2-platforms"
export PACKAGES_PATH="$WORKSPACE:\
$EDK2_PLATFORMS_PATH/Platform/Intel:\
$EDK2_PLATFORMS_PATH/Silicon/Intel:\
$EDK2_PLATFORMS_PATH/Features/Intel:\
$EDK2_PLATFORMS_PATH/Features/Intel/Debugging:\
$EDK2_PLATFORMS_PATH/Features/Intel/Network:\
$EDK2_PLATFORMS_PATH/Features/Intel/OutOfBandManagement:\
$EDK2_PLATFORMS_PATH/Features/Intel/PowerManagement:\
$EDK2_PLATFORMS_PATH/Features/Intel/SystemInformation:\
$EDK2_PLATFORMS_PATH/Features/Intel/UserInterface"

function build_ovmf_64() {
    build -a IA32 -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc \
          -DNETWORK_ENABLE=TRUE \
          "$@"
}

build_ovmf_64 -b RELEASE
build_ovmf_64 -b DEBUG
