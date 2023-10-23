#!/usr/bin/env bash

export CROSS_COMPILE="x86_64-elf-"
make -C src bin-x86_64-efi-sb/ipxe.efi EMBED=$PWD/dasharo.ipxe BUILD_ID_CMD="echo 0x1234567890" \
  EXTRA_CFLAGS="-Wno-address-of-packed-member  -m64  -fuse-ld=bfd \
  -Wl,--build-id=none -fno-delete-null-pointer-checks -Wlogical-op -march=nocona \
  -malign-data=abi -mcmodel=large -mno-red-zone -fno-pic"

