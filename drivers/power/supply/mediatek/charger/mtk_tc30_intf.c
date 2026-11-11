/*
 * Copyright (C) 2020 Transsion Holdings.
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

static inline u32 tc30_get_vbus(void)
{
	return battery_get_vbus() * 1000;
}

static inline u32 tc30_get_ibat(void)
{
	return battery_get_bat_current() * 100;
}

static int tc30_enable_vbus_ovp(struct charger_manager *pinfo, bool enable)
{
	int ret;

	ret = pmic_enable_hw_vbus_ovp(enable);
	if (ret < 0) {
		chr_err("%s: hw vbus ovp(%d) failed, ret = %d\n",
			__func__, enable, ret);
		return ret;
	}

	charger_enable_vbus_ovp(pinfo, enable, 1);
	chr_debug("%s: enable = %d\n", __func__, enable);

	return ret;
}

int mtk_tc30_reset_ta_vchr(struct charger_manager *pinfo)
{
	int ret = 0, chr_volt = 0;
	int i;
	struct mtk_tc30 *tc30 = &pinfo->tc30;
	bool chg2_chip_enabled = false;

	chr_debug("%s: starts\n", __func__);

	ret = charger_dev_set_tc30_gpio_enable(pinfo->chg1_dev, false);
	if (ret < 0) {
		chr_err("%s: set tc30 gpio disable failed, ret = %d\n",
			__func__, ret);
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

		chr_volt = tc30_get_vbus();
		if (abs(chr_volt - tc30->ta_vchr_org) <= 1000000) {
			tc30->is_connect = false;
			chr_info("%s: OK, vbus = %d\n", __func__, chr_volt);
			return 0;
		}
		if (i < 2)
			msleep(100);
	}

	chr_err("%s: vbus(%d) did not return to 5V\n", __func__, chr_volt);
	return ret;
}

static int tc30_leave(struct charger_manager *pinfo)
{
	int ret;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	chr_debug("%s: starts\n", __func__);
	ret = mtk_tc30_reset_ta_vchr(pinfo);
	if (ret < 0 || tc30->is_connect) {
		chr_err("%s: failed, is_connect = %d, ret = %d\n",
			__func__, tc30->is_connect, ret);
		return ret;
	}

	tc30_enable_vbus_ovp(pinfo, true);
	chr_info("%s: OK\n", __func__);
	return ret;
}

static int __tc30_set_ta_vchr(struct charger_manager *pinfo)
{
	int ret;
	bool chg2_chip_enabled = false;

	if (pinfo->chg2_dev) {
		charger_dev_is_chip_enabled(pinfo->chg2_dev,
					    &chg2_chip_enabled);
		if (chg2_chip_enabled)
			charger_dev_enable(pinfo->chg2_dev, false);
	}

	ret = charger_dev_set_tc30_gpio_enable(pinfo->chg1_dev, true);
	if (ret < 0)
		chr_err("%s: set tc30_dm_gpio fail, ret = %d\n",
			__func__, ret);

	msleep(250);
	return ret;
}

static int tc30_set_ta_vchr(struct charger_manager *pinfo, int target_vchr)
{
	int ret = 0, retry_cnt = 0;
	int vbus = 0;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	charger_dev_set_input_current(pinfo->chg1_dev, 1000000);
	msleep(100);

	do {
		vbus = tc30_get_vbus();
		ret = __tc30_set_ta_vchr(pinfo);
		vbus = tc30_get_vbus();
		if (ret == 0 && abs(vbus - target_vchr) < 1000000)
			return 0;

		if (tc30->is_cable_out_occur ||
		    mt_get_charger_type() == CHARGER_UNKNOWN ||
		    !pinfo->enable_hv_charging)
			break;
		retry_cnt++;
	} while (retry_cnt < 2);

	chr_err("%s: failed, vbus = %d, target = %d, ret = %d\n",
		__func__, vbus, target_vchr, ret);
	return -EIO;
}

static int tc30_detect_ta(struct charger_manager *pinfo)
{
	int ret;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	chr_debug("%s: starts\n", __func__);
	tc30_enable_vbus_ovp(pinfo, false);

	ret = tc30_set_ta_vchr(pinfo, tc30->target_vchr);
	if (ret < 0) {
		tc30->to_check_chr_type = false;
		tc30->is_connect = false;
		mtk_tc30_reset_ta_vchr(pinfo);
		tc30_enable_vbus_ovp(pinfo, true);
		chr_err("%s: failed, ret = %d\n", __func__, ret);
		return -EIO;
	}

	tc30->is_connect = true;
	pinfo->pe2.is_connect = false;
	pinfo->pe.is_connect = false;
	pinfo->hvdcp20.is_connect = false;
	tc30->fast_status = true;
	mtk_chgstat_notify(pinfo);
	chr_info("%s: OK, target = %d\n", __func__, tc30->target_vchr);
	return 0;
}

int mtk_tc30_set_charging_current(struct charger_manager *pinfo,
				  unsigned int *ichg, unsigned int *aicr)
{
	*aicr = 2000000;
	*ichg = pinfo->data.ta_ac_charger_current;
	chr_info("%s:tc30 input = %u, current = %u\n", __func__, *aicr, *ichg);
	return 0;
}

int mtk_tc30_start_algorithm(struct charger_manager *pinfo)
{
	int ret = 0;
	int vchr, ichg;
	static int retry_cnt;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	if (!pinfo->enable_tc30) {
		chr_debug("%s: not enable tc30\n", __func__);
		return -ENOTSUPP;
	}

	if (!tc30->is_enabled) {
		chr_debug("%s: stop, tc30 is disabled\n", __func__);
		return 0;
	}

	mutex_lock(&tc30->access_lock);
	__pm_stay_awake(&tc30->suspend_lock);

	if (!tc30->is_connect) {
		ret = -EIO;
		chr_debug("%s: stop, tc30 is not connected\n", __func__);
		goto out;
	}

	vchr = tc30_get_vbus();
	if (abs(vchr - tc30->ta_vchr_org) < 1000000) {
		retry_cnt++;
		if (retry_cnt <= 2)
			tc30->to_check_chr_type = true;
		ret = tc30_leave(pinfo);
		goto out;
	}

	if (!pinfo->enable_hv_charging) {
		chr_info("%s: stop due to enable_hv_charging=0\n", __func__);
		tc30->to_check_chr_type = true;
		ret = tc30_leave(pinfo);
		goto out;
	}

	ichg = tc30_get_ibat();
	if (ichg >= 1 && battery_get_soc() > pinfo->data.ta_stop_battery_soc &&
	    ichg < pinfo->data.tc30_ichg_level_threshold) {
		chr_info("tc30->is_ui_soc_90=%d,tc30->to_check_chr_type =%d\n",
			 tc30->is_ui_soc_90, tc30->to_check_chr_type);
		ret = tc30_leave(pinfo);
		if (ret < 0 || !tc30->is_connect)
			goto out;
	}

	if (tc30->is_connect) {
		vchr = tc30_get_vbus();
		if (abs(vchr - tc30->target_vchr) >= 1000000) {
			ret = tc30_set_ta_vchr(pinfo, tc30->target_vchr);
			if (ret < 0) {
				tc30_leave(pinfo);
				goto out;
			}
		}
	}

out:
	__pm_relax(&tc30->suspend_lock);
	mutex_unlock(&tc30->access_lock);
	return ret;
}

int mtk_tc30_plugout_reset(struct charger_manager *pinfo)
{
	int ret;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	chr_debug("%s: starts\n", __func__);
	tc30->is_connect = false;
	tc30->is_cable_out_occur = false;
	tc30->fast_status = false;
	tc30->is_ui_soc_90 = false;
	tc30->to_check_chr_type = true;

	ret = tc30_enable_vbus_ovp(pinfo, true);
	if (ret < 0)
		chr_err("%s: enable vbus ovp fail, ret = %d\n", __func__, ret);

	chr_info("%s: OK\n", __func__);
	return ret;
}

int mtk_tc30_check_charger(struct charger_manager *pinfo)
{
	int ret = 0;
	int soc;
	struct mtk_tc30 *tc30 = &pinfo->tc30;

	if (!pinfo->enable_tc30) {
		chr_debug("%s: not enable tc30\n", __func__);
		return -ENOTSUPP;
	}

	if (!tc30->is_enabled) {
		chr_debug("%s: tc30 not enabled\n", __func__);
		return 0;
	}

	mutex_lock(&tc30->access_lock);
	__pm_stay_awake(&tc30->suspend_lock);

	if (tc30->is_cable_out_occur)
		mtk_tc30_plugout_reset(pinfo);

	if (!tc30->to_check_chr_type) {
		ret = -ENOTSUPP;
		goto out;
	}

	soc = battery_get_soc();
	if (mt_get_charger_type() == STANDARD_CHARGER &&
	    soc >= pinfo->data.ta_start_battery_soc &&
	    soc < pinfo->data.ta_stop_battery_soc) {
		tc30->is_cable_out_occur = false;
		tc30->is_connect = false;
		tc30->fast_status = false;

		ret = mtk_tc30_reset_ta_vchr(pinfo);
		if (ret < 0)
			goto out;

		ret = tc30_detect_ta(pinfo);
		if (ret >= 0)
			tc30->to_check_chr_type = false;
	} else {
		ret = -ENOTSUPP;
	}

out:
	__pm_relax(&tc30->suspend_lock);
	mutex_unlock(&tc30->access_lock);
	return ret;
}

void mtk_tc30_set_is_cable_out_occur(struct charger_manager *pinfo, bool out)
{
	chr_info("%s: out = %d\n", __func__, out);
	mutex_lock(&pinfo->tc30.pmic_sync_lock);
	pinfo->tc30.is_cable_out_occur = out;
	mutex_unlock(&pinfo->tc30.pmic_sync_lock);
}

void mtk_tc30_set_to_check_chr_type(struct charger_manager *pinfo, bool check)
{
	mutex_lock(&pinfo->tc30.access_lock);
	__pm_stay_awake(&pinfo->tc30.suspend_lock);

	chr_info("%s: check = %d\n", __func__, check);
	pinfo->tc30.to_check_chr_type = check;

	__pm_relax(&pinfo->tc30.suspend_lock);
	mutex_unlock(&pinfo->tc30.access_lock);
}

void mtk_tc30_set_is_enable(struct charger_manager *pinfo, bool enable)
{
	mutex_lock(&pinfo->tc30.access_lock);
	__pm_stay_awake(&pinfo->tc30.suspend_lock);

	chr_info("%s: enable = %d\n", __func__, enable);
	pinfo->tc30.is_enabled = enable;

	__pm_relax(&pinfo->tc30.suspend_lock);
	mutex_unlock(&pinfo->tc30.access_lock);
}

bool mtk_tc30_get_to_check_chr_type(struct charger_manager *pinfo)
{
	return pinfo->tc30.to_check_chr_type;
}

bool mtk_tc30_get_is_connect(struct charger_manager *pinfo)
{
	if (pinfo->tc30.is_cable_out_occur)
		return false;

	return pinfo->tc30.is_connect;
}

bool mtk_tc30_get_fast_status(struct charger_manager *pinfo)
{
	return pinfo->tc30.fast_status;
}

bool mtk_tc30_get_is_enable(struct charger_manager *pinfo)
{
	return pinfo->tc30.is_enabled;
}

int mtk_tc30_init(struct charger_manager *pinfo)
{
	wakeup_source_init(&pinfo->tc30.suspend_lock, "tc30 suspend wakelock");
	mutex_init(&pinfo->tc30.access_lock);
	mutex_init(&pinfo->tc30.pmic_sync_lock);

	pinfo->tc30.ta_vchr_org = 5000000;
	pinfo->tc30.target_vchr = 7500000;
	pinfo->tc30.to_check_chr_type = true;
	pinfo->tc30.is_cable_out_occur = false;
	pinfo->tc30.is_connect = false;
	pinfo->tc30.is_enabled = true;
	pinfo->tc30.fast_status = false;
	pinfo->tc30.is_ui_soc_90 = false;

	chr_info("%s: OK\n", __func__);
	return 0;
}
