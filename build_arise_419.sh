#!/bin/bash
set -e

export PATH="/opt/toolchains/clang-23/bin:$PATH"
export ARCH=arm64
export SUBARCH=arm64
export CC=clang
export LD=ld.lld
export AR=llvm-ar
export NM=llvm-nm
export OBJCOPY=llvm-objcopy
export OBJDUMP=llvm-objdump
export READELF=llvm-readelf
export STRIP=llvm-strip
export CLANG_TRIPLE=aarch64-linux-gnu-
export CROSS_COMPILE=aarch64-linux-gnu-
export LLVM=1
export LLVM_IAS=1

echo "=========================================================="
echo " Building Sol-Kernel Arise Linux 4.19 for Infinix X698    "
echo " Toolchain: ZyC Clang 23.0.0git + LLD 23.0.0              "
echo "=========================================================="

cd /opt/kernel_x698_4.19

# Ensure defconfig is synced if requested
if [ "$1" == "defconfig" ]; then
    make arise_x698_419_defconfig
fi

# Build Kernel Image.gz and DTBs
make -j$(nproc) Image.gz dtbs

echo "=========================================================="
echo " Build Completed Successfully!                            "
echo " Kernel: arch/arm64/boot/Image.gz                         "
echo " DTB:    arch/arm64/boot/dts/mediatek/mt6781.dtb          "
echo "=========================================================="
