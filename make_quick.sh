#!/bin/bash
set -e
cd /opt/kernel
export PATH=/opt/toolchains/proton-clang/bin:/usr/bin:/bin
make -j4 \
    ARCH=arm64 \
    CC=clang \
    LD=ld.lld \
    AR=llvm-ar \
    NM=llvm-nm \
    OBJCOPY=llvm-objcopy \
    OBJDUMP=llvm-objdump \
    STRIP=llvm-strip \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    CROSS_COMPILE=aarch64-linux-gnu- \
    CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
    CONFIG_NO_ERROR_ON_MISMATCH=y \
    KCFLAGS="-w" \
    KCPPFLAGS="-w" \
    Image.gz Image.gz-dtb dtbs

echo "BUILD_COMPLETE_SUCCESS"
