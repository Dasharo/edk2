#!/usr/bin/env bash

rm -rf ipxe/
git clone git@github.com:Dasharo/ipxe.git -b v1.21.1

cp dasharo.ipxe ipxe/src
cd ipxe/src
make bin-x86_64-efi/ipxe.efi EMBED=dasharo.ipxe
cd -
cp ipxe/src/bin-x86_64-efi/ipxe.efi .
