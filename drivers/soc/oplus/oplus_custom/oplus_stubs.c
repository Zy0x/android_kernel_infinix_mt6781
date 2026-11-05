// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stub implementations for Oplus proprietary symbols on Infinix platform
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/of.h>

/* Audio amplifier stubs */
unsigned char aw87319_audio_off(void)
{
	return 0;
}
EXPORT_SYMBOL(aw87319_audio_off);

unsigned char aw87319_audio_speaker(void)
{
	return 0;
}
EXPORT_SYMBOL(aw87319_audio_speaker);

unsigned char aw87319_audio_receiver(void)
{
	return 0;
}
EXPORT_SYMBOL(aw87319_audio_receiver);

struct snd_soc_card;
int soc_aux_init_only_sia81xx(struct platform_device *pdev, struct snd_soc_card *card)
{
	return 0;
}
EXPORT_SYMBOL(soc_aux_init_only_sia81xx);

/* Battery / Charger stubs */
int odm_select_bat_ntc_support = 0;
EXPORT_SYMBOL(odm_select_bat_ntc_support);

int battery_type_check(void)
{
	return 0;
}
EXPORT_SYMBOL(battery_type_check);

int fg_read_dts_val(const struct device_node *np, const char *node_string, int *param, int unit)
{
	if (param)
		*param = 0;
	return -1;
}
EXPORT_SYMBOL(fg_read_dts_val);

int oplus_battery_get_bat_temperature(void)
{
	return 250; /* 25.0 C */
}
EXPORT_SYMBOL(oplus_battery_get_bat_temperature);

int oplus_gauge_get_batt_temperature(void)
{
	return 250;
}
EXPORT_SYMBOL(oplus_gauge_get_batt_temperature);

int oplus_force_get_subboard_temp(void)
{
	return 250;
}
EXPORT_SYMBOL(oplus_force_get_subboard_temp);

int oplus_chg_get_subboard_temp_cal(void)
{
	return 0;
}
EXPORT_SYMBOL(oplus_chg_get_subboard_temp_cal);

bool oplus_chg_get_voocphy_support(void)
{
	return false;
}
EXPORT_SYMBOL(oplus_chg_get_voocphy_support);

int oplus_voocphy_get_cp_vbat(void)
{
	return 0;
}
EXPORT_SYMBOL(oplus_voocphy_get_cp_vbat);

bool prj_for_mtk_60w_support(void)
{
	return false;
}
EXPORT_SYMBOL(prj_for_mtk_60w_support);

bool prj_is_subboard_temp_support(void)
{
	return false;
}
EXPORT_SYMBOL(prj_is_subboard_temp_support);

/* Camera / Flash stubs */
void camera_ldo_set_en_ldo(int ldonum, unsigned int en)
{
}
EXPORT_SYMBOL(camera_ldo_set_en_ldo);

void camera_ldo_set_ldo_value(int ldonum, unsigned int value)
{
}
EXPORT_SYMBOL(camera_ldo_set_ldo_value);

void oplus_chg_set_camera_on(bool val)
{
}
EXPORT_SYMBOL(oplus_chg_set_camera_on);

void oplus_chg_set_camera_status(bool val)
{
}
EXPORT_SYMBOL(oplus_chg_set_camera_status);

void oplus_chg_set_flash_led_status(bool val)
{
}
EXPORT_SYMBOL(oplus_chg_set_flash_led_status);

/* Project & System stubs */
unsigned int is_project(int project)
{
	return 0;
}
EXPORT_SYMBOL(is_project);

unsigned int get_PCB_Version(void)
{
	return 0;
}
EXPORT_SYMBOL(get_PCB_Version);

int32_t get_Operator_Version(void)
{
	return 0;
}
EXPORT_SYMBOL(get_Operator_Version);

unsigned int get_eng_version(void)
{
	return 0;
}
EXPORT_SYMBOL(get_eng_version);

int phx_is_system_boot_completed(void)
{
	return 1;
}
EXPORT_SYMBOL(phx_is_system_boot_completed);

int register_device_proc_for_ufsplus(char *name, int *hpb_status, int *tw_status)
{
	return 0;
}
EXPORT_SYMBOL(register_device_proc_for_ufsplus);

void register_oplus_wfd_wlan_ops(void *ops)
{
}
EXPORT_SYMBOL(register_oplus_wfd_wlan_ops);

/* Scheduler & Process stubs */
int sysctl_boost_task_threshold = 51;
EXPORT_SYMBOL(sysctl_boost_task_threshold);

int sysctl_slide_boost_enabled = 0;
EXPORT_SYMBOL(sysctl_slide_boost_enabled);

int hans_report(int type, int caller_pid, int caller_uid, int target_pid, int target_uid, const char *rpc_name, int code)
{
	return 0;
}
EXPORT_SYMBOL(hans_report);

int register_device_proc(char *name, char *version, char *manufacture)
{
	return 0;
}
EXPORT_SYMBOL(register_device_proc);
