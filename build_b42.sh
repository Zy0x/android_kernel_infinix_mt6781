#!/bin/bash
set -e
export PATH=/opt/toolchains/proton-clang/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH
cd /opt/kernel
echo "[B42] Building kernel Image.gz..."
make -j4 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- Image.gz
echo "[B42] Building DTBs..."
make -j4 ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- dtbs
echo "BUILD42_COMPILE_SUCCESS"
