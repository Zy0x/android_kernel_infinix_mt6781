#!/bin/bash
set -e
export PATH=/opt/toolchains/proton-clang/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd /opt/kernel
echo "[B49] Building kernel Image.gz..."
make -j8 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- Image.gz
echo "[B49] Building DTBs..."
make -j8 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- dtbs
echo "BUILD49_COMPILE_SUCCESS"
