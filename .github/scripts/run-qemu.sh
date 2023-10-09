#!/usr/bin/env bash

cp ./Build/OvmfX64/RELEASE_GCC5/FV/OVMF_VARS.fd /tmp/OVMF_VARS.fd 

q35_params="-machine q35,smm=on \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive if=pflash,format=raw,unit=0,file=Build/OvmfX64/RELEASE_GCC5/FV/OVMF_CODE.fd,readonly=on \
    -drive if=pflash,format=raw,unit=1,file=/tmp/OVMF_VARS.fd \
    -debugcon file:debug.log -global isa-debugcon.iobase=0x402 \
    -global ICH9-LPC.disable_s3=1 \
    -qmp unix:/tmp/qmp-socket,server,nowait \
    -net none \
    -serial telnet:localhost:1234,server,nowait"

if [ "$1" == "nographic" ]; then
    qemu-system-x86_64 $q35_params -nographic
else
    qemu-system-x86_64 $q35_params
fi
