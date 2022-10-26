#!/usr/bin/env bash

rm -rf ipxe/
git clone https://github.com/Dasharo/ipxe.git -b skinit_lz-next

cd ipxe/src
sed -i 's|.*DOWNLOAD_PROTO_HTTPS|#define DOWNLOAD_PROTO_HTTPS|g' config/general.h
sed -i 's|//#define\s*IMAGE_MULTIBOOT2.*|#define IMAGE_MULTIBOOT2|' config/general.h
# sed -i 's|//#define\s*IMAGE_BZIMAGE.*|#define IMAGE_BZIMAGE|' config/general.h
sed -i 's|//#define\s*IMAGE_GZIP.*|#define IMAGE_GZIP|' config/general.h
sed -i 's|//#define\s*NSLOOKUP_CMD.*|#define NSLOOKUP_CMD|' config/general.h
sed -i 's|//#define\s*IMAGE_TRUST_CMD.*|#define IMAGE_TRUST_CMD|' config/general.h
sed -i 's|//#define\s*PING_CMD.*|#define PING_CMD|' config/general.h
sed -i 's|//#define\s*CONSOLE_SERIAL.*|#define CONSOLE_SERIAL|' config/console.h

cp ../../dasharo.ipxe .
make bin-x86_64-efi/ipxe.efi EMBED=dasharo.ipxe
cd -
cp ipxe/src/bin-x86_64-efi/ipxe.efi .
