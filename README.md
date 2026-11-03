# 🚀 Infinix Note 11 Pro / 11S (X698 / MT6781) — Custom Kernel Tree

[![Target Device](https://img.shields.io/badge/Device-Infinix%20Note%2011%20Pro%20%2F%2011S-00c853?style=for-the-badge&logo=android)](https://www.infinixmobility.com)
[![Codename](https://img.shields.io/badge/Codename-sword%20%2F%20X698-blue?style=for-the-badge)](https://github.com)
[![Chipset](https://img.shields.io/badge/SoC-MediaTek%20Helio%20G96%20(MT6781)-ff6f00?style=for-the-badge&logo=mediatek)](https://www.mediatek.com)
[![Kernel Version](https://img.shields.io/badge/Linux-4.14.186%20LTS-purple?style=for-the-badge&logo=linux)](https://kernel.org)
[![KernelSU](https://img.shields.io/badge/Root-KernelSU%20Native-red?style=for-the-badge)](https://kernelsu.org)
[![Author](https://img.shields.io/badge/Maintainer-ɴᴏɪʀ-black?style=for-the-badge)](https://github.com)

A high-performance custom Linux kernel source tree tailored specifically for the **Infinix Note 11 Pro / Note 11S (Codename: `sword` / Model: `X698` / Board: `x698_h812`)**, powered by the **MediaTek Helio G96 (MT6781)** platform.

---

## ⚡ Key Features & Performance Enhancements

1. **KernelSU (KSU) Native In-Tree Driver**:
   - Integrated directly at the C driver level (`drivers/kernelsu/kernel/`).
   - Pure Ring 0 kernel root execution without user-space patcher overhead or SELinux policy conflicts.
2. **Google TCP BBR (v2/v3) Congestion Control**:
   - `CONFIG_TCP_CONG_BBR=y` enabled natively and set as default (`CONFIG_DEFAULT_TCP_CONG="bbr"`).
   - Minimizes mobile gaming ping jitter and optimizes high-throughput packet delivery.
   - Retains `CONFIG_TCP_CONG_WESTWOOD=y` as high-stability secondary algorithm.
3. **In-Kernel WireGuard VPN Subsystem**:
   - `CONFIG_WIREGUARD=y` integrated directly in `net/wireguard/`.
   - Native cryptographic tunnel execution without user-space TUN/TAP bottleneck.
4. **ZSTD High-Density ZRAM Compression**:
   - `CONFIG_ZRAM_DEF_COMP_ZSTD=y` for ultra-fast, high-compression memory swapping on 8GB LPDDR4X.
5. **Transsion Bloat & Telemetry Stripping**:
   - Disabled heavy kernel-level ANR monitoring loops (`CONFIG_TRAN_ANR_MONITOR_SUPPORT=n`).
   - Cleaned up debug symbols and tracing overhead for maximum Instructions-Per-Cycle (IPC).
6. **EAS Schedutil & Display Pacing Synergy**:
   - Harmonized with 120Hz display refresh rate and 240Hz touch sampling rate on Infinix X698.

---

## 🛠️ Defconfig Configurations

| Defconfig Target | Description | Features Included |
| :--- | :--- | :--- |
| **`infinix_x698_performance_defconfig`** | **Primary Custom Kernel Build** | KernelSU, Google BBR, WireGuard, ZSTD ZRAM, De-bloat |
| **`infinix_x698_defconfig`** | Factory OEM Baseline | 100% Stock Transsion XOS 10 kernel configuration |

---

## 🏗️ Automated Cloud Compilation (GitHub Actions CI)

When you are ready to compile or push to your GitHub repository:
1. Push this tree to your GitHub repository:
   ```bash
   git remote add origin https://github.com/Zy0x/android_kernel_infinix_mt6781.git
   git push -u origin main
   ```
2. Navigate to **Actions** → **Build Infinix Note 11 Pro Kernel** → **Run workflow**.
3. Select **`infinix_x698_performance_defconfig`** and click **Run**.
4. Within **~8–10 minutes**, the cloud builder (Ubuntu 22.04 with 16 vCPUs) will compile the kernel with AOSP Clang and produce an auto-installer flashable ZIP:
   `Infinix-X698-CustomKernel-KSU-<timestamp>.zip`

---

## 📱 How to Flash

Once compiled, flash the generated ZIP in **OrangeFox Recovery**:
1. Boot into **OrangeFox Recovery** (`ofox_sword.img`).
2. Flash `Infinix-X698-CustomKernel-KSU-xxxx.zip`.
3. Wipe Cache & Dalvik.
4. Reboot System.
5. Install the **KernelSU Manager APK** to manage root permissions and modules.

---

## 🤝 Credits & Upstream Base

- **Maintainer**: **`ɴᴏɪʀ`**
- **MT6781 4.14 Base**: `realme-mt6781-dev` (Cosmos Kernel Team)
- **KernelSU Project**: `tiann` & KernelSU contributors
- **WireGuard**: Jason A. Donenfeld (`zx2c4`)
- **Google Android Open Source Project (AOSP)**: Clang/LLVM toolchain & Linux 4.14 common kernel
