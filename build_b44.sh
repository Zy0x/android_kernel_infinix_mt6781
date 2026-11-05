#!/bin/bash
set -e
export PATH=/opt/toolchains/proton-clang/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd /opt/kernel
echo '[B44] Ensuring KSU, KPROBES, WLAN_DRV_BUILD_IN in .config...'
./scripts/config --enable CONFIG_KPROBES --enable CONFIG_HAVE_KPROBES --enable CONFIG_KSU --enable CONFIG_WLAN_DRV_BUILD_IN
echo '[B44] Building kernel Image.gz...'
make -j8 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- Image.gz
echo '[B44] Building DTBs...'
make -j8 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- dtbs
echo 'BUILD44_COMPILE_SUCCESS'
