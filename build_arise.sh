#!/bin/bash
set -e
export PATH=/opt/toolchains/proton-clang/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd /opt/kernel_b67_prelim
echo '[ARISE] Updating .config...'
make ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- arise_x698_defconfig
echo '[ARISE] Building kernel Image.gz...'
make -j12 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- Image.gz
echo 'BUILD_ARISE_SUCCESS'
