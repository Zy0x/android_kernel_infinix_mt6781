#!/bin/bash
set -e

FLAVOR="${1:-apatch}"
ACTION="${2:-all}"

case "$FLAVOR" in
    apatch)
        DEFCONFIG="infinix_x698_apatch_defconfig"
        ;;
    ksu)
        DEFCONFIG="infinix_x698_ksu_defconfig"
        ;;
    stock)
        DEFCONFIG="infinix_x698_defconfig"
        ;;
    perf)
        DEFCONFIG="infinix_x698_performance_defconfig"
        ;;
    *)
        echo "[!] Unknown flavor: $FLAVOR (use apatch, ksu, stock, perf)"
        exit 1
        ;;
esac

export ARCH=arm64
export SUBARCH=arm64
export PATH="/opt/toolchains/proton-clang/bin:${PATH}"
export KBUILD_BUILD_USER="Antigravity"
export KBUILD_BUILD_HOST="InfinixDev"

MAKE_ARGS=(
    ARCH=arm64
    CC=clang
    LD=ld.lld
    AR=llvm-ar
    NM=llvm-nm
    OBJCOPY=llvm-objcopy
    OBJDUMP=llvm-objdump
    STRIP=llvm-strip
    CLANG_TRIPLE=aarch64-linux-gnu-
    CROSS_COMPILE=aarch64-linux-gnu-
    CROSS_COMPILE_ARM32=arm-linux-gnueabi-
    CONFIG_NO_ERROR_ON_MISMATCH=y
    KCFLAGS="-w"
    KCPPFLAGS="-w"
)

NPROC=$(nproc --all)
echo "=========================================================="
echo "  Infinix Note 11 Pro / 11s (MT6781) Kernel Builder"
echo "  Flavor   : $FLAVOR ($DEFCONFIG)"
echo "  Action   : $ACTION"
echo "  Threads  : $NPROC"
echo "=========================================================="

cd /opt/kernel

if [ "$ACTION" == "config" ]; then
    make "${MAKE_ARGS[@]}" "$DEFCONFIG"
    exit 0
fi

if [ "$ACTION" == "dtbs" ]; then
    make "${MAKE_ARGS[@]}" "$DEFCONFIG"
    make -j"$NPROC" "${MAKE_ARGS[@]}" dtbs
    exit 0
fi

# Full build
make "${MAKE_ARGS[@]}" "$DEFCONFIG"
make -j"$NPROC" "${MAKE_ARGS[@]}" Image.gz Image.gz-dtb dtbs

echo "=========================================================="
echo "  Build successful! Exporting artifacts..."
echo "=========================================================="

mkdir -p /opt/kernel/dist
cp -v arch/arm64/boot/Image.gz arch/arm64/boot/Image.gz-dtb /opt/kernel/dist/
cp -v arch/arm64/boot/dts/mediatek/x698_h812.dtb /opt/kernel/dist/

# Also sync to /mnt/e/
mkdir -p /mnt/e/Data/GitHub/android_kernel_infinix_mt6781/out/arch/arm64/boot/dts/mediatek
cp -v arch/arm64/boot/Image.gz arch/arm64/boot/Image.gz-dtb /mnt/e/Data/GitHub/android_kernel_infinix_mt6781/out/arch/arm64/boot/
cp -v arch/arm64/boot/dts/mediatek/x698_h812.dtb /mnt/e/Data/GitHub/android_kernel_infinix_mt6781/out/arch/arm64/boot/dts/mediatek/

echo "[+] Artifacts copied to /opt/kernel/dist/ and out/arch/arm64/boot/"
