#!/bin/bash
set -e
export PATH=/opt/toolchains/clang-23/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd /opt/kernel_r1017_touch_work
MAKE_ARGS="ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump READELF=llvm-readelf STRIP=llvm-strip CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- LLVM_IAS=1"
echo '[ARISE] Updating .config...'
make $MAKE_ARGS arise_x698_defconfig
echo '[ARISE] Building kernel Image.gz with safe thread limit (-j4)...'
make -j4 $MAKE_ARGS Image.gz modules
echo 'BUILD_ARISE_SUCCESS'
