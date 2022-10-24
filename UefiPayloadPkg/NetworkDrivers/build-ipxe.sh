#!/usr/bin/env bash

rm -rf ipxe/
git clone https://github.com/ipxe/ipxe.git

cd ipxe/src
sed 's|.*DOWNLOAD_PROTO_HTTPS|#define DOWNLOAD_PROTO_HTTPS|g' config/general.h > config/general.h.tmp
sed 's|//#define\s*IMAGE_BZIMAGE.*|#define IMAGE_BZIMAGE|' config/general.h > config/general.h.tmp
sed 's|//#define\s*IMAGE_GZIP.*|#define IMAGE_GZIP|' config/general.h > config/general.h.tmp
sed 's|//#define\s*NSLOOKUP_CMD.*|#define NSLOOKUP_CMD|' config/general.h > config/general.h.tmp
sed 's|//#define\s*IMAGE_TRUST_CMD.*|#define IMAGE_TRUST_CMD|' config/general.h > config/general.h.tmp
sed 's|//#define\s*PING_CMD.*|#define PING_CMD|' config/general.h > config/general.h.tmp
mv config/general.h.tmp config/general.h

cp dasharo.ipxe ipxe/src
cd ipxe/src
make bin-x86_64-efi/ipxe.efi EMBED=dasharo.ipxe
cd -
cp ipxe/src/bin-x86_64-efi/ipxe.efi .
