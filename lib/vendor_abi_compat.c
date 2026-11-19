// SPDX-License-Identifier: GPL-2.0
/*
 * Zero-overhead exported ABI compatibility stubs for prebuilt vendor .ko
 * modules (MTK connsys wmt_drv/wlan_drv_gen4m/bt_drv/gps_drv/fmradio_drv,
 * met.ko) when runtime debug/hardening checks are disabled in vmlinux.
 */
#define __VENDOR_ABI_COMPAT__
#include <linux/export.h>
#include <linux/list.h>
#include <linux/thread_info.h>

#ifndef CONFIG_DEBUG_LIST
bool __list_add_valid(struct list_head *new,
		      struct list_head *prev,
		      struct list_head *next)
{
	return true;
}
EXPORT_SYMBOL(__list_add_valid);

bool __list_del_entry_valid(struct list_head *entry)
{
	return true;
}
EXPORT_SYMBOL(__list_del_entry_valid);
#endif

#ifndef CONFIG_HARDENED_USERCOPY
void __check_object_size(const void *ptr, unsigned long n, bool to_user)
{
}
EXPORT_SYMBOL(__check_object_size);
#endif
