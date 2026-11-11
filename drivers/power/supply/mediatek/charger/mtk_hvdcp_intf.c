/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/kernel.h>

#include <upmu_common.h>
#include "mtk_charger_intf.h"
#include "mtk_charger_init.h"

extern unsigned int mt6358_upmu_set_rg_vusb_vocal(unsigned int val);
extern unsigned int mt6358_upmu_set_rg_vusb_vosel(unsigned int val);

static inline u32 hvdcp20_get_vbus(void)
{
	return battery_get_vbus() * 1000;
}

static inline u32 hvdcp20_get_ibat(void)
{
	return battery_get_bat_current() * 100;
}

static int hvdcp20_enable_vbus_ovp(struct charger_manager *pinfo, bool enable)
{
	int ret;

	ret = pmic_enable_hw_vbus_ovp(enable);
	if (ret < 0) {
		chr_err("%s: hw vbus ovp(%d) failed, ret = %d\n",
			__func__, enable, ret);
		return ret;
	}

	charger_enable_vbus_ovp(pinfo, enable, 0);
	chr_debug("%s: enable = %d\n", __func__, enable);

	return ret;
}

int mtk_hvdcp20_reset_ta_vchr(struct charger_manager *pinfo)
{
	int ret = 0, chr_volt = 0;
	int i;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;
	bool chg2_chip_enabled = false;

	chr_debug("%s: starts\n", __func__);

	ret = charger_dev_set_dp_dm_recover(pinfo->chg1_dev);
	if (ret < 0) {
		chr_err("%s: dp_dm_recover failed, ret = %d\n", __func__, ret);
		return ret;
	}

	for (i = 0; i < 3; i++) {
		if (pinfo->chg2_dev) {
			charger_dev_is_chip_enabled(pinfo->chg2_dev,
						    &chg2_chip_enabled);
			if (chg2_chip_enabled) {
				charger_dev_enable(pinfo->chg2_dev, false);
				charger_dev_set_eoc_current(pinfo->chg1_dev,
							    200000);
			}
		}

		chr_volt = hvdcp20_get_vbus();
		if (abs(chr_volt - hvdcp20->ta_vchr_org) <= 1000000) {
			hvdcp20->is_connect = false;
			chr_info("%s: OK, vbus = %d\n", __func__, chr_volt);
			return 0;
		}
		if (i < 2)
			msleep(100);
	}

	chr_err("%s: vbus(%d) did not return to 5V\n", __func__, chr_volt);
	return ret;
}

static int hvdcp20_leave(struct charger_manager *pinfo)
{
	int ret;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	chr_debug("%s: starts\n", __func__);
	ret = mtk_hvdcp20_reset_ta_vchr(pinfo);
	if (ret < 0 || hvdcp20->is_connect) {
		chr_err("%s: failed, is_connect = %d, ret = %d\n",
			__func__, hvdcp20->is_connect, ret);
		return ret;
	}

	hvdcp20_enable_vbus_ovp(pinfo, true);
	chr_info("%s: OK\n", __func__);
	return ret;
}

static int __hvdcp20_set_ta_vchr(struct charger_manager *pinfo)
{
	int ret = 0;
	bool chg2_chip_enabled = false;

	if (pinfo->chg2_dev) {
		charger_dev_is_chip_enabled(pinfo->chg2_dev,
					    &chg2_chip_enabled);
		if (chg2_chip_enabled)
			charger_dev_enable(pinfo->chg2_dev, false);
	}

	ret = charger_dev_set_dp_0_6_v(pinfo->chg1_dev);
	if (ret < 0)
		chr_err("%s: set dp 0.6v fail, ret = %d\n", __func__, ret);

	ret = charger_dev_set_dp_3_0_v(pinfo->chg1_dev, true);
	if (ret < 0)
		chr_err("%s: set dp 3.0v fail, ret = %d\n", __func__, ret);

	ret = charger_dev_set_dm_0_6_v(pinfo->chg1_dev);
	if (ret < 0)
		chr_err("%s: set dm 0.6v fail, ret = %d\n", __func__, ret);

	msleep(250);
	return ret;
}

static int hvdcp20_set_ta_vchr(struct charger_manager *pinfo)
{
	int ret = 0, retry_cnt = 0;
	int vbus = 0;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	do {
		vbus = hvdcp20_get_vbus();
		ret = __hvdcp20_set_ta_vchr(pinfo);
		vbus = hvdcp20_get_vbus();
		if (ret == 0 && abs(vbus - hvdcp20->ta_vchr_target) < 1000000)
			return 0;

		if (hvdcp20->is_cable_out_occur ||
		    mt_get_charger_type() == CHARGER_UNKNOWN ||
		    !pinfo->enable_hv_charging)
			break;
		retry_cnt++;
	} while (retry_cnt < 2);

	chr_err("%s: failed, vbus = %d, target = %d, ret = %d\n",
		__func__, vbus, hvdcp20->ta_vchr_target, ret);
	return -EIO;
}

static int hvdcp20_detect_ta(struct charger_manager *pinfo)
{
	int ret;
	int vbus = 0, retry_cnt = 0;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	chr_debug("%s: starts\n", __func__);
	hvdcp20_enable_vbus_ovp(pinfo, false);

	do {
		vbus = hvdcp20_get_vbus();
		ret = __hvdcp20_set_ta_vchr(pinfo);
		vbus = hvdcp20_get_vbus();
		if (ret == 0 && abs(vbus - hvdcp20->ta_vchr_target) < 1000000) {
			hvdcp20->is_connect = true;
			pinfo->pe2.is_connect = false;
			pinfo->pe.is_connect = false;
			pinfo->tc30.is_connect = false;
			hvdcp20->fast_status = true;
			mtk_chgstat_notify(pinfo);
			chr_info("%s: OK, vbus = %d\n", __func__, vbus);
			return 0;
		}

		if (hvdcp20->is_cable_out_occur ||
		    mt_get_charger_type() == CHARGER_UNKNOWN ||
		    !pinfo->enable_hv_charging)
			break;
		retry_cnt++;
	} while (retry_cnt < 2);

	hvdcp20->to_check_chr_type = false;
	chr_err("%s: failed, vbus = %d, ret = %d\n", __func__, vbus, ret);
	hvdcp20->is_connect = false;
	mtk_hvdcp20_reset_ta_vchr(pinfo);
	hvdcp20_enable_vbus_ovp(pinfo, true);
	return -EIO;
}

int mtk_hvdcp20_set_charging_current(struct charger_manager *pinfo,
				     unsigned int *ichg, unsigned int *aicr)
{
	*aicr = 2000000;
	*ichg = pinfo->data.ta_ac_charger_current;
	chr_debug("%s: aicr = %u, ichg = %u\n", __func__, *aicr, *ichg);
	return 0;
}

int mtk_hvdcp20_start_algorithm(struct charger_manager *pinfo)
{
	int ret = 0;
	int vchr, ichg;
	static int retry_cnt;
	static bool vusb_toggle_flag;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	if (!pinfo->enable_hvdcp20 || !hvdcp20->is_enabled) {
		chr_debug("%s: stop, hvdcp20 is disabled\n", __func__);
		return 0;
	}

	mutex_lock(&hvdcp20->access_lock);
	__pm_stay_awake(&hvdcp20->suspend_lock);

	if (!hvdcp20->is_connect) {
		ret = -EIO;
		chr_debug("%s: stop, hvdcp20 is not connected\n", __func__);
		goto out;
	}

	vchr = hvdcp20_get_vbus();
	if (abs(vchr - hvdcp20->ta_vchr_org) < 1000000) {
		retry_cnt++;
		if (retry_cnt <= 2)
			hvdcp20->to_check_chr_type = true;
		ret = hvdcp20_leave(pinfo);
		goto out;
	}

	if (!pinfo->enable_hv_charging) {
		chr_info("%s: stop due to enable_hv_charging=0\n", __func__);
		hvdcp20->to_check_chr_type = true;
		ret = hvdcp20_leave(pinfo);
		goto out;
	}

	ichg = hvdcp20_get_ibat();
	if (ichg >= 1 && battery_get_soc() > pinfo->data.ta_stop_battery_soc &&
	    ichg < pinfo->data.hvdcp20_ichg_level_threshold) {
		ret = hvdcp20_leave(pinfo);
		chr_info("%s: OK, SOC = (%d,%d), ichg = %d, stop HVDCP20\n",
			 __func__, battery_get_soc(),
			 pinfo->data.ta_stop_battery_soc, ichg);
		if (ret < 0 || !hvdcp20->is_connect)
			goto out;
	}

	if (hvdcp20->is_connect) {
		vchr = hvdcp20_get_vbus();
		if (abs(vchr - hvdcp20->ta_vchr_target) >= 1000000) {
			ret = hvdcp20_set_ta_vchr(pinfo);
			if (ret < 0) {
				hvdcp20_leave(pinfo);
				goto out;
			}
		}

		/* Keep VUSB rail vocal/vosel synchronized across active cycle */
		mt6358_upmu_set_rg_vusb_vocal(vusb_toggle_flag ? 4 : 3);
		mt6358_upmu_set_rg_vusb_vosel(vusb_toggle_flag ? 0 : 10);
		vusb_toggle_flag = !vusb_toggle_flag;
	}

out:
	__pm_relax(&hvdcp20->suspend_lock);
	mutex_unlock(&hvdcp20->access_lock);
	return ret;
}

int mtk_hvdcp20_plugout_reset(struct charger_manager *pinfo)
{
	int ret;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	chr_debug("%s: starts\n", __func__);
	hvdcp20->is_connect = false;
	hvdcp20->is_cable_out_occur = false;
	hvdcp20->fast_status = false;
	hvdcp20->to_check_chr_type = true;

	ret = hvdcp20_enable_vbus_ovp(pinfo, true);
	if (ret < 0)
		chr_err("%s: enable vbus ovp fail, ret = %d\n", __func__, ret);

	chr_info("%s: OK\n", __func__);
	return ret;
}

int mtk_hvdcp20_check_charger(struct charger_manager *pinfo)
{
	int ret = 0;
	int soc;
	struct mtk_hvdcp20 *hvdcp20 = &pinfo->hvdcp20;

	if (!pinfo->enable_hvdcp20 || !hvdcp20->is_enabled) {
		chr_debug("%s: stop, hvdcp20 is disabled\n", __func__);
		return 0;
	}

	mutex_lock(&hvdcp20->access_lock);
	__pm_stay_awake(&hvdcp20->suspend_lock);

	if (hvdcp20->is_cable_out_occur)
		mtk_hvdcp20_plugout_reset(pinfo);

	if (hvdcp20->to_check_chr_type) {
		soc = battery_get_soc();
		if ((mt_get_charger_type() == STANDARD_CHARGER ||
		     mt_get_charger_type() == CHARGING_HOST ||
		     mt_get_charger_type() == NONSTANDARD_CHARGER) &&
		    soc >= pinfo->data.ta_start_battery_soc &&
		    soc < pinfo->data.ta_stop_battery_soc) {
			hvdcp20->is_cable_out_occur = false;
			hvdcp20->is_connect = false;
			hvdcp20->fast_status = false;

			ret = mtk_hvdcp20_reset_ta_vchr(pinfo);
			if (ret < 0)
				goto out;

			ret = hvdcp20_detect_ta(pinfo);
			if (ret >= 0)
				hvdcp20->to_check_chr_type = false;
		}
	}

out:
	__pm_relax(&hvdcp20->suspend_lock);
	mutex_unlock(&hvdcp20->access_lock);
	return ret;
}

void mtk_hvdcp20_set_is_cable_out_occur(struct charger_manager *pinfo, bool out)
{
	chr_info("%s: out = %d\n", __func__, out);
	mutex_lock(&pinfo->hvdcp20.pmic_sync_lock);
	pinfo->hvdcp20.is_cable_out_occur = out;
	mutex_unlock(&pinfo->hvdcp20.pmic_sync_lock);
}

void mtk_hvdcp20_set_to_check_chr_type(struct charger_manager *pinfo,
				       bool check)
{
	mutex_lock(&pinfo->hvdcp20.access_lock);
	__pm_stay_awake(&pinfo->hvdcp20.suspend_lock);

	chr_info("%s: check = %d\n", __func__, check);
	pinfo->hvdcp20.to_check_chr_type = check;

	__pm_relax(&pinfo->hvdcp20.suspend_lock);
	mutex_unlock(&pinfo->hvdcp20.access_lock);
}

void mtk_hvdcp20_set_is_enable(struct charger_manager *pinfo, bool enable)
{
	mutex_lock(&pinfo->hvdcp20.access_lock);
	__pm_stay_awake(&pinfo->hvdcp20.suspend_lock);

	chr_info("%s: enable = %d\n", __func__, enable);
	pinfo->hvdcp20.is_enabled = enable;

	__pm_relax(&pinfo->hvdcp20.suspend_lock);
	mutex_unlock(&pinfo->hvdcp20.access_lock);
}

bool mtk_hvdcp20_get_to_check_chr_type(struct charger_manager *pinfo)
{
	return pinfo->hvdcp20.to_check_chr_type;
}

bool mtk_hvdcp20_get_is_connect(struct charger_manager *pinfo)
{
	if (pinfo->hvdcp20.is_cable_out_occur)
		return false;

	return pinfo->hvdcp20.is_connect;
}

bool mtk_hvdcp20_get_fast_status(struct charger_manager *pinfo)
{
	return pinfo->hvdcp20.fast_status;
}

bool mtk_hvdcp20_get_is_enable(struct charger_manager *pinfo)
{
	return pinfo->hvdcp20.is_enabled;
}

int mtk_hvdcp20_init(struct charger_manager *pinfo)
{
	wakeup_source_init(&pinfo->hvdcp20.suspend_lock,
			   "hvdcp20 suspend wakelock");
	mutex_init(&pinfo->hvdcp20.access_lock);
	mutex_init(&pinfo->hvdcp20.pmic_sync_lock);

	pinfo->hvdcp20.ta_vchr_org = 5000000;
	pinfo->hvdcp20.ta_vchr_target = 9000000;
	pinfo->hvdcp20.to_check_chr_type = true;
	pinfo->hvdcp20.is_cable_out_occur = false;
	pinfo->hvdcp20.is_connect = false;
	pinfo->hvdcp20.is_enabled = true;
	pinfo->hvdcp20.fast_status = false;

	chr_info("%s: OK\n", __func__);
	return 0;
}
