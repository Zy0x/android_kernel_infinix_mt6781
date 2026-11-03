### AnyKernel3 Ramdisk Mod Script
## Custom Kernel for Infinix Note 11 Pro / Note 11S (X698 / sword)

### AnyKernel setup
# global properties
properties() { '
kernel.string=Infinix Note 11 Pro (X698 / sword) Performance Kernel
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=Infinix-X698
device.name2=X698
device.name3=sword
device.name4=x698_h812
device.name5=Infinix-X697
supported.versions=11-14
supported.patchlevels=
supported.vendorpatchlevels=
'; } # end properties

### AnyKernel install
## boot files attributes
boot_attributes() {
set_perm_recursive 0 0 755 644 $RAMDISK/*;
set_perm_recursive 0 0 750 750 $RAMDISK/init* $RAMDISK/sbin;
} # end attributes

# boot shell variables
BLOCK=boot;
IS_SLOT_DEVICE=auto;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

# import functions/variables and setup patching - see for reference (DO NOT REMOVE)
. tools/ak3-core.sh;

# boot install
ui_print " ";
ui_print "📦 Unpacking boot partition...";
dump_boot;

ui_print "⚡ Injecting optimized MT6781 kernel & DTB...";
# AnyKernel3 automatically replaces Image.gz-dtb or Image.gz + dtb if present

ui_print "💾 Repacking and writing to flash storage...";
write_boot;
## end boot install
