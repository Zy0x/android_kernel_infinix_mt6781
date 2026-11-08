/*
 * transsion_stubs.c - Exported stubs for Transsion proprietary vendor connectivity modules
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>

/* Transsion NFBD / telemetry stubs for wmt_drv.ko */
void *alloc_msg(void)
{
	return NULL;
}
EXPORT_SYMBOL(alloc_msg);

void free_msg(void *msg)
{
}
EXPORT_SYMBOL(free_msg);

int msg_put_string(void *msg, const char *key, const char *val)
{
	return 0;
}
EXPORT_SYMBOL(msg_put_string);

int submit_msg(void *msg)
{
	return 0;
}
EXPORT_SYMBOL(submit_msg);

int msg_put_int(void *msg, const char *key, int val)
{
	return 0;
}
EXPORT_SYMBOL(msg_put_int);

int msg_put_long(void *msg, const char *key, long val)
{
	return 0;
}
EXPORT_SYMBOL(msg_put_long);

int msg_put_ll(void *msg, const char *key, long long val)
{
	return 0;
}
EXPORT_SYMBOL(msg_put_ll);

/*
 * NOTE: get_low_latency_mode, register_file_buf_handler,
 * register_set_p2p_mode_handler, update_driver_loaded_status,
 * wifi_fwlog_event_func_register, wifi_reset_start and wifi_reset_end are
 * exported by the vendor wmt_chrdev_wifi.ko itself. Exporting them from the
 * kernel makes wmt_chrdev_wifi.ko fail with "exports duplicate symbol",
 * which prevents /dev/wmtWifi creation and therefore wlan0. Do not stub them.
 */

void *xgf_lookup_name(const char *name)
{
	return NULL;
}
EXPORT_SYMBOL(xgf_lookup_name);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Transsion connectivity stubs for vendor modules");
