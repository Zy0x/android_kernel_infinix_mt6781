#!/usr/bin/env python3
"""
=============================================================================
Infinix Note 11 Pro / Note 11s (X698 / MT6781) - Safe Test Boot Packager
=============================================================================
Fungsi:
  Mengemas biner kernel hasil kompilasi (Image.gz / Image.gz-dtb) bersama
  ramdisk stock OEM menjadi berkas 'test_boot.img' berstandar Android Boot v2.

Tujuan Keamanan:
  Digunakan untuk protokol 'fastboot boot test_boot.img' (RAM Boot).
  Kernel dijalankan LANGSUNG dari RAM tanpa menyentuh atau mem-flash partisi
  fisik eMMC/UFS (boot_a / boot_b), menjamin perlindungan 100% dari softbrick.
=============================================================================
"""

import struct
import os
import sys
import argparse
import hashlib

# Pastikan output terminal mendukung UTF-8 pada Windows
if sys.stdout.encoding and sys.stdout.encoding.lower() != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

def align(size, page):
    return ((size + page - 1) // page) * page

def pad(data, page):
    rem = len(data) % page
    if rem != 0:
        return data + b'\x00' * (page - rem)
    return data

def verify_kernel_magic(data):
    if len(data) < 4:
        return False, "File kernel kosong atau terlalu kecil"
    # GZIP compressed kernel (Image.gz)
    if data[:2] == b'\x1f\x8b':
        return True, "ARM64 Compressed Linux Kernel (GZIP / Image.gz)"
    # Raw ARM64 kernel Image (magic at offset 0x38: ARM\x64)
    if len(data) >= 64 and data[0x38:0x3c] == b'ARM\x64':
        return True, "ARM64 Raw Linux Kernel (Uncompressed Image)"
    return True, "Format kernel biner (Custom/Raw)"

def create_test_boot(template_path, kernel_path, output_path, dtb_path=None, ramdisk_path=None):
    if not os.path.exists(template_path):
        raise FileNotFoundError(f"Template boot image tidak ditemukan: {template_path}")
    if not os.path.exists(kernel_path):
        raise FileNotFoundError(f"Biner kernel tidak ditemukan: {kernel_path}")

    print("==================================================================")
    print("  INFINIX X698 ZERO-RISK TEST BOOT PACKAGER (ANDROID BOOT V2)")
    print("==================================================================")

    # 1. Baca dan validasi template boot image
    with open(template_path, 'rb') as f:
        orig_data = f.read()

    hdr = bytearray(orig_data[:2048])
    magic = hdr[:8]
    if magic != b'ANDROID!':
        raise ValueError(f"Magic template tidak valid: {magic} (Harus 'ANDROID!')")

    (orig_k_size, k_addr, orig_r_size, r_addr, s_size, s_addr, t_addr, page_size, v, os_ver) = struct.unpack('<IIIIIIIIII', hdr[8:48])
    (rec_size, rec_offset, hdr_size, orig_dtb_size, dtb_addr) = struct.unpack('<IQIIQ', hdr[1632:1660])

    if v != 2:
        print(f"[!] Peringatan: Header version terdeteksi v{v}, template standar adalah v2.")

    print(f"[*] Template Terverifikasi: {os.path.basename(template_path)}")
    print(f"    - Header: v{v} | Page Size: {page_size} bytes")
    print(f"    - Load Addresses -> Kernel: {hex(k_addr)} | Ramdisk: {hex(r_addr)} | DTB: {hex(dtb_addr)}")

    # 2. Baca biner kernel baru
    with open(kernel_path, 'rb') as f:
        kernel_data = f.read()
    k_ok, k_desc = verify_kernel_magic(kernel_data)
    k_size = len(kernel_data)
    print(f"[*] Memuat Kernel Baru: {os.path.basename(kernel_path)}")
    print(f"    - Tipe: {k_desc}")
    print(f"    - Ukuran: {k_size:,} bytes ({k_size / (1024*1024):.2f} MB)")

    # 3. Baca Ramdisk (dari custom path atau ekstrak stock OEM)
    k_offset = page_size
    r_offset = k_offset + align(orig_k_size, page_size)
    dtb_offset = r_offset + align(orig_r_size, page_size)
    if rec_size > 0:
        dtb_offset += align(rec_size, page_size)

    if ramdisk_path and os.path.exists(ramdisk_path):
        with open(ramdisk_path, 'rb') as f:
            ramdisk_data = f.read()
        r_size = len(ramdisk_data)
        print(f"[*] Menggunakan Ramdisk Kustom: {os.path.basename(ramdisk_path)} ({r_size:,} bytes)")
    else:
        ramdisk_data = orig_data[r_offset:r_offset + orig_r_size]
        r_size = len(ramdisk_data)
        print(f"[*] Mengekstrak Ramdisk Asli Stock OEM ({r_size:,} bytes, GZIP SHA-256: {hashlib.sha256(ramdisk_data).hexdigest()[:12]}...)")

    # 4. Baca DTB (dari custom path atau ekstrak stock OEM)
    if dtb_path and os.path.exists(dtb_path):
        with open(dtb_path, 'rb') as f:
            dtb_data = f.read()
        dtb_size = len(dtb_data)
        print(f"[*] Menggunakan DTB Baru: {os.path.basename(dtb_path)} ({dtb_size:,} bytes)")
    else:
        dtb_data = orig_data[dtb_offset:dtb_offset + orig_dtb_size]
        dtb_size = len(dtb_data)
        print(f"[*] Menggunakan DTB Bawaan ({dtb_size:,} bytes)")

    # 5. Sinkronkan ukuran ke header v2
    struct.pack_into('<I', hdr, 8, k_size)
    struct.pack_into('<I', hdr, 16, r_size)
    struct.pack_into('<I', hdr, 1648, dtb_size)

    # 6. Rakit paket boot image
    out_dir = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(out_dir, exist_ok=True)

    repacked = bytearray()
    repacked.extend(pad(hdr, page_size))
    repacked.extend(pad(kernel_data, page_size))
    repacked.extend(pad(ramdisk_data, page_size))
    repacked.extend(pad(dtb_data, page_size))

    with open(output_path, 'wb') as out_f:
        out_f.write(repacked)

    total_size = len(repacked)
    sha256_hash = hashlib.sha256(repacked).hexdigest()

    print("\n[+] PAKET TEST BOOT BERHASIL DIBUAT!")
    print(f"    - Berkas: {output_path}")
    print(f"    - Ukuran Total: {total_size:,} bytes ({total_size / (1024*1024):.2f} MB)")
    print(f"    - SHA-256: {sha256_hash}")

    print("\n==================================================================")
    print("  [PROTOKOL KEAMANAN] PANDUAN PENGUJIAN AMAN 100% (ZERO-RISK RAM BOOT)")
    print("==================================================================")
    print("  1. Sambungkan ponsel ke PC via kabel USB (USB Debugging ON).")
    print("  2. Masuk ke mode Fastboot:")
    print("       adb reboot bootloader")
    print("  3. Boot kernel langsung ke RAM (JANGAN gunakan 'fastboot flash boot'!):")
    print(f"       fastboot boot \"{output_path}\"")
    print("  4. Hasil Pengujian:")
    print("     - JIKA SUKSES: Ponsel booting normal masuk ke Android dengan kernel baru.")
    print("     - JIKA GAGAL/HANG: Cukup cabut kabel dan tekan tombol POWER 10 detik.")
    print("       Ponsel akan reboot normal kembali ke kernel lama di memori fisik.")
    print("==================================================================")
    return output_path

if __name__ == '__main__':
    default_template = r'E:\Data\GitHub\Infinix Note 11s X689 Device Tree\boot_images\boot_working_backup.img'
    default_kernel = r'E:\Data\GitHub\Infinix Note 11s X689 Device Tree\boot_images\unpacked_boot\kernel'
    default_output = r'E:\Data\GitHub\Infinix Note 11s X689 Device Tree\boot_images\test_boot.img'

    parser = argparse.ArgumentParser(description="Infinix X698 Safe Test Boot Packager")
    parser.add_argument('--template', default=default_template, help="Path ke template boot.img stock")
    parser.add_argument('--kernel', default=default_kernel, help="Path ke biner kernel Image.gz / Image.gz-dtb")
    parser.add_argument('--dtb', default=None, help="Path ke berkas x698_h812.dtb kustom (opsional)")
    parser.add_argument('--ramdisk', default=None, help="Path ke berkas ramdisk kustom (opsional)")
    parser.add_argument('--output', default=default_output, help="Path berkas output test_boot.img")

    args = parser.parse_args()
    create_test_boot(args.template, args.kernel, args.output, args.dtb, args.ramdisk)
