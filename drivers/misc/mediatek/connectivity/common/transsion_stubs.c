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

/* Transsion WLAN / vendor hooks for wlan_drv_gen4m.ko */
int get_low_latency_mode(void)
{
	return 0;
}
EXPORT_SYMBOL(get_low_latency_mode);

int register_file_buf_handler(void *handler)
{
	return 0;
}
EXPORT_SYMBOL(register_file_buf_handler);

int register_set_p2p_mode_handler(void *handler)
{
	return 0;
}
EXPORT_SYMBOL(register_set_p2p_mode_handler);

void update_driver_loaded_status(int status)
{
}
EXPORT_SYMBOL(update_driver_loaded_status);

int wifi_fwlog_event_func_register(void *func)
{
	return 0;
}
EXPORT_SYMBOL(wifi_fwlog_event_func_register);

int wifi_reset_start(void)
{
	return 0;
}
EXPORT_SYMBOL(wifi_reset_start);

int wifi_reset_end(int status)
{
	return 0;
}
EXPORT_SYMBOL(wifi_reset_end);

void *xgf_lookup_name(const char *name)
{
	return NULL;
}
EXPORT_SYMBOL(xgf_lookup_name);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Transsion connectivity stubs for vendor modules");
