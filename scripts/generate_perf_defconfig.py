import sys

src_path = r'e:\Data\GitHub\android_kernel_infinix_mt6781\arch\arm64\configs\infinix_x698_defconfig'
dst_path = r'e:\Data\GitHub\android_kernel_infinix_mt6781\arch\arm64\configs\infinix_x698_performance_defconfig'

with open(src_path, 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if line.startswith('# CONFIG_TCP_CONG_BBR is not set'):
        new_lines.append('CONFIG_TCP_CONG_BBR=y\n')
    elif line.startswith('CONFIG_TCP_CONG_WESTWOOD=m'):
        new_lines.append('CONFIG_TCP_CONG_WESTWOOD=y\n')
    elif line.startswith('CONFIG_DEFAULT_TCP_CONG='):
        new_lines.append('CONFIG_DEFAULT_TCP_CONG="bbr"\n')
    elif line.startswith('CONFIG_TRAN_ANR_MONITOR_SUPPORT=y'):
        new_lines.append('# CONFIG_TRAN_ANR_MONITOR_SUPPORT is not set\n')
    else:
        new_lines.append(line)

new_lines.append('\n# Supercharged Performance & Custom Features (Infinix X698 / MT6781)\n')
new_lines.append('CONFIG_KSU=y\n')
new_lines.append('CONFIG_WIREGUARD=y\n')
new_lines.append('CONFIG_ZRAM_DEF_COMP_ZSTD=y\n')

with open(dst_path, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print(f"Generated {dst_path} successfully ({len(new_lines)} lines).")
