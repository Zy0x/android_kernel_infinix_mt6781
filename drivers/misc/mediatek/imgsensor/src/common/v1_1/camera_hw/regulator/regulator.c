/*
 * Copyright (C) 2017 MediaTek Inc.
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

#include "regulator.h"
#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
#include "imgsensor.h"
#include <soc/oplus/system/oplus_project.h>
#endif
#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
static bool regulator_status[IMGSENSOR_SENSOR_IDX_MAX_NUM][REGULATOR_TYPE_MAX_NUM] = {{false}};
static void check_for_regulator_get(struct REGULATOR *preg,
struct device *pdevice, unsigned int sensor_index, unsigned int regulator_index);
static void check_for_regulator_put(struct REGULATOR *preg, unsigned int sensor_index, unsigned int regulator_index);
static struct device_node *of_node_record;
static struct IMGSENSOR_HW_DEVICE_COMMON *ghw_device_common;
static DEFINE_MUTEX(g_regulator_state_mutex);
#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
static const int regulator_voltage[] = {
	REGULATOR_VOLTAGE_0,
	REGULATOR_VOLTAGE_1000,
	REGULATOR_VOLTAGE_1050,
	REGULATOR_VOLTAGE_1100,
	REGULATOR_VOLTAGE_1200,
	REGULATOR_VOLTAGE_1210,
	REGULATOR_VOLTAGE_1220,
	REGULATOR_VOLTAGE_1500,
	REGULATOR_VOLTAGE_1800,
	REGULATOR_VOLTAGE_2500,
	REGULATOR_VOLTAGE_2800,
	REGULATOR_VOLTAGE_2900,
};

struct REGULATOR_CTRL regulator_control[REGULATOR_TYPE_MAX_NUM] = {
	{"vcama"},
	{"vcamd"},
	{"vcamio"},
	#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	{"vcamaf"},
	{"vcama1"},
	{"vcamd1"}
	#endif
};

static struct REGULATOR reg_instance;

#ifdef CONFIG_TRAN_CAMERA_EXT_LDO_REGULATOR
static int ext_ldo_regulator_enable(enum IMGSENSOR_HW_PIN pin, int voltage_uv);
static int ext_ldo_regulator_disable(enum IMGSENSOR_HW_PIN pin);
#endif

#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
extern struct IMGSENSOR gimgsensor;
struct regulator *regulator_get_regVCAMAF(void)
{
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	return regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), "vcamaf");
}
EXPORT_SYMBOL(regulator_get_regVCAMAF);
struct regulator *regulator_get_regVCAMAF_Chaka(int sensor_idx)
{
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	char str_regulator_name[LENGTH_FOR_SNPRINTF];
	snprintf(str_regulator_name, sizeof(str_regulator_name), "cam%d_%s", sensor_idx, "vcamaf");
	return regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), str_regulator_name);
}
EXPORT_SYMBOL(regulator_get_regVCAMAF_Chaka);

static struct regulator *regVCAMAF = NULL;
struct regulator *regulator_get_regVCAMAF_ALICE(void)
{
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	return regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), "vcammainaf");
}
#endif

#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
extern struct IMGSENSOR gimgsensor;
struct regulator *regulator_get_regVCAMAF_20609(void)
{
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	return regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), "vcammainaf");
}
EXPORT_SYMBOL(regulator_get_regVCAMAF_20609);

struct regulator *regulator_get_regVCAMAF_bladeb(void)
{
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	return regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), "vtp");
}
EXPORT_SYMBOL(regulator_get_regVCAMAF_bladeb);
#endif


static enum IMGSENSOR_RETURN regulator_init(
	void *pinstance,
	struct IMGSENSOR_HW_DEVICE_COMMON *pcommon)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int type, idx, ret = 0;
	char str_regulator_name[LENGTH_FOR_SNPRINTF];

	#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	ghw_device_common = pcommon;
	of_node_record = pcommon->pplatform_device->dev.of_node;
	#endif /*OPLUS_FEATURE_CAMERA_COMMON*/

	for (idx = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		idx < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		idx++) {
		for (type = 0;
			type < REGULATOR_TYPE_MAX_NUM;
			type++) {
			memset(str_regulator_name, 0,
				sizeof(str_regulator_name));
			ret = snprintf(str_regulator_name,
				sizeof(str_regulator_name),
				"cam%d_%s",
				idx,
				regulator_control[type].pregulator_type);
			if (ret < 0)
				return ret;
			preg->pregulator[idx][type] = regulator_get_optional(
					&pcommon->pplatform_device->dev,
					str_regulator_name);
			if (IS_ERR(preg->pregulator[idx][type])) {
				preg->pregulator[idx][type] = NULL;
				PK_INFO("ERROR: regulator[%d][%d]  %s fail!\n",
						idx, type, str_regulator_name);
			}
			atomic_set(&preg->enable_cnt[idx][type], 0);
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static enum IMGSENSOR_RETURN regulator_release(void *pinstance)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int type, idx;
	struct regulator *pregulator = NULL;
	atomic_t *enable_cnt = NULL;

	for (idx = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		idx < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		idx++) {

		for (type = 0; type < REGULATOR_TYPE_MAX_NUM; type++) {
			pregulator = preg->pregulator[idx][type];
			enable_cnt = &preg->enable_cnt[idx][type];
			if (pregulator != NULL) {
				for (; atomic_read(enable_cnt) > 0; ) {
					regulator_disable(pregulator);
					atomic_dec(enable_cnt);
				}
			}
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
static struct regulator *regulator_reinit(void *pinstance, int sensor_idx, int type)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	char str_regulator_name[LENGTH_FOR_SNPRINTF];
	struct IMGSENSOR *pimgsensor = &gimgsensor;
	snprintf(str_regulator_name,
			sizeof(str_regulator_name),
			"cam%d_%s",
			sensor_idx,
			regulator_control[type].pregulator_type);
	preg->pregulator[sensor_idx][type] =
		regulator_get(&((pimgsensor->hw.common.pplatform_device)->dev), str_regulator_name);
	pr_err("reinit %s regulator[%d][%d] = %pK\n", str_regulator_name,
			sensor_idx, type, preg->pregulator[sensor_idx][type]);
	msleep(10);
	if(IS_ERR(preg->pregulator[sensor_idx][type])){
		pr_err("regulator reinit fail %pK\n",preg->pregulator[sensor_idx][type]);
		return NULL;
	} else {
		pr_err("regulator reinit success");
		return preg->pregulator[sensor_idx][type];
	}
}
#endif

static enum IMGSENSOR_RETURN regulator_set(
	void *pinstance,
	enum IMGSENSOR_SENSOR_IDX   sensor_idx,
	enum IMGSENSOR_HW_PIN       pin,
	enum IMGSENSOR_HW_PIN_STATE pin_state)
{
	struct regulator     *pregulator;
	struct REGULATOR     *preg = (struct REGULATOR *)pinstance;
	int reg_type_offset;
	atomic_t             *enable_cnt;
#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	if (is_project(21101) || is_project(21102) || is_project(21235) || is_project(21236) || is_project(21041) || is_project(21042)
	|| is_project(0x216A0) || is_project(21831) || is_project(0x2163B) || is_project(0x2163C) || is_project(0x2163D)
	|| is_project(21639) || is_project(0x216CD) || is_project(0x216CE) || is_project(22603) || is_project(22604) || is_project(22609)
	|| is_project(0x2260A) || is_project(0x2260B) || is_project(22669) || is_project(0x2266A) || is_project(0x2266B) || is_project(0x2266C)) {
		PK_DBG("to set current regulator pin:%d", pin);
		regVCAMAF = regulator_get_regVCAMAF_ALICE();
		if (IS_ERR(regVCAMAF)) {
			PK_PR_ERR("get main af regulator fail");
			regVCAMAF = NULL;
		}
		if (pin == IMGSENSOR_HW_PIN_AFVDD)
		{
			if (pin_state == IMGSENSOR_HW_PIN_STATE_LEVEL_0)
			{
				if (regulator_set_voltage(regVCAMAF,regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator]fail to regulator_set_voltage, powerId:%d\n", regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_disable(regVCAMAF))
				{
					PK_PR_ERR("[regulator]fail to regulator_disable gVCamIO\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			else
			{
				if (regulator_set_voltage(regVCAMAF, regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator]fail to regulator_set_voltage, powerId:%d\n",regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_enable(regVCAMAF))
				{
					PK_PR_ERR("[regulator]fail to regulator_enable\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			return IMGSENSOR_RETURN_SUCCESS;
		}
	}
	if (is_project(20609) || is_project(0x2060A) || is_project(0x2060B) || is_project(0x2070C) || is_project(0x206FF) || is_project(20795) || is_project(20796) || is_project(0x2070C) || is_project(0x2070B) || is_project(0x2070E) || is_project(0x210A0) || is_project(21747) || is_project(21748))
	{
		regVCAMAF = regulator_get_regVCAMAF_20609();
		if (IS_ERR(regVCAMAF)) {
			PK_PR_ERR("get main af regulator fail");
			regVCAMAF = NULL;
		}
		if (pin == IMGSENSOR_HW_PIN_AFVDD)
		{
			if (pin_state == IMGSENSOR_HW_PIN_STATE_LEVEL_0)
			{
				if (regulator_set_voltage(regVCAMAF,regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator]fail to regulator_set_voltage, powerId:%d\n", regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_disable(regVCAMAF))
				{
					PK_PR_ERR("[regulator]fail to regulator_disable gVCamIO\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			else
			{
				if (regulator_set_voltage(regVCAMAF, regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator]fail to regulator_set_voltage, powerId:%d\n",regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_enable(regVCAMAF))
				{
					PK_PR_ERR("[regulator]fail to regulator_enable\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			return IMGSENSOR_RETURN_SUCCESS;
		}
	}
	if (is_project(21711) || is_project(21712))
	{
		regVCAMAF = regulator_get_regVCAMAF();
		if (IS_ERR(regVCAMAF)) {
			PK_PR_ERR("get main af regulator fail");
			regVCAMAF = NULL;
		}
		if (pin == IMGSENSOR_HW_PIN_AFVDD)
		{
			if (pin_state == IMGSENSOR_HW_PIN_STATE_LEVEL_0)
			{
				if (regulator_set_voltage(regVCAMAF,regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator] fail to regulator_set_voltage, powerId:%d\n", regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_disable(regVCAMAF))
				{
					PK_PR_ERR("[regulator] fail to regulator_disable gVCamIO\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			else
			{
				if (regulator_set_voltage(regVCAMAF, regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator] fail to regulator_set_voltage, powerId:%d\n",regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_enable(regVCAMAF))
				{
					PK_PR_ERR("[regulator] fail to regulator_enable\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			return IMGSENSOR_RETURN_SUCCESS;
		}
	}
	if (is_project(22609) || is_project(0x2266B) || is_project(0x2266C) || is_project(22669) || is_project(0x2266A) || is_project(0x2260B)) {
		regVCAMAF = regulator_get_regVCAMAF_bladeb();
		if (IS_ERR(regVCAMAF)) {
			PK_PR_ERR("get bladeb main af regulator fail");
			regVCAMAF = NULL;
		}
		if (pin == IMGSENSOR_HW_PIN_AFVDD)
		{
			if (pin_state == IMGSENSOR_HW_PIN_STATE_LEVEL_0)
			{
				if (regulator_set_voltage(regVCAMAF,regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator] fail to regulator_set_voltage, powerId:%d\n", regulator_voltage[IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_disable(regVCAMAF))
				{
					PK_PR_ERR("[regulator] fail to regulator_disable gVCamIO\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			else
			{
				if (regulator_set_voltage(regVCAMAF, regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]))
				{
					PK_PR_ERR("[regulator] fail to regulator_set_voltage, powerId:%d\n",regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				}
				if (regulator_enable(regVCAMAF))
				{
					PK_PR_ERR("[regulator] fail to regulator_enable\n");
					return IMGSENSOR_RETURN_ERROR;
				}
			}
			return IMGSENSOR_RETURN_SUCCESS;
		}
	}
#endif
	#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	if (pin > IMGSENSOR_HW_PIN_FAN53870_ENABLE   ||
	#else
	if (pin > IMGSENSOR_HW_PIN_DOVDD   ||
	#endif
	    pin < IMGSENSOR_HW_PIN_AVDD    ||
	    pin_state < IMGSENSOR_HW_PIN_STATE_LEVEL_0 ||
	    pin_state >= IMGSENSOR_HW_PIN_STATE_LEVEL_HIGH ||
	    sensor_idx < 0)
		return IMGSENSOR_RETURN_ERROR;

	reg_type_offset = REGULATOR_TYPE_VCAMA;

	#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	if (ghw_device_common)
		check_for_regulator_get(preg, &ghw_device_common->pplatform_device->dev, sensor_idx,(reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD));
	#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
	pregulator = preg->pregulator[(unsigned int)sensor_idx][
		reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD];

	#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
	if(IS_ERR(pregulator)) {
		pregulator = regulator_reinit(preg, sensor_idx, reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD);
		if (IS_ERR(pregulator)) {
			PK_PR_ERR(
				"[regulator]fail to regulator_reinit, sensor_idx:%d pin:%d reg_type_offset:%d\n",
				sensor_idx,
				pin,
				reg_type_offset);
			return IMGSENSOR_RETURN_ERROR;
		}
	}
	#endif
	enable_cnt = &preg->enable_cnt[(unsigned int)sensor_idx][
		reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD];

	if (pregulator) {
		if (pin_state != IMGSENSOR_HW_PIN_STATE_LEVEL_0) {
			if (regulator_set_voltage(pregulator,
				regulator_voltage[
				pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0],
				regulator_voltage[
				pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0])) {

				PK_PR_ERR(
				  "[regulator]fail to regulator_set_voltage, powertype:%d powerId:%d\n",
				  pin,
				  regulator_voltage[
				  pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
			}
			#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
			if (!regulator_set_load(pregulator, 10000)) {
				PK_DBG("set load success");
			}
			#endif

			if (regulator_enable(pregulator)) {
				PK_PR_ERR(
				"[regulator]fail to regulator_enable, powertype:%d powerId:%d\n",
				pin,
				regulator_voltage[
				  pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
				#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
				check_for_regulator_put(preg, sensor_idx, (reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD));
				#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
				return IMGSENSOR_RETURN_ERROR;
			}
			atomic_inc(enable_cnt);
		} else {
			if (regulator_is_enabled(pregulator))
				PK_DBG("[regulator]%d is enabled\n", pin);

			if (regulator_disable(pregulator)) {
				PK_PR_ERR(
					"[regulator]fail to regulator_disable, powertype: %d\n",
					pin);
				#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
				check_for_regulator_put(preg, sensor_idx,(reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD));
				#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
				return IMGSENSOR_RETURN_ERROR;
			}
			#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
			check_for_regulator_put(preg, sensor_idx,(reg_type_offset + pin - IMGSENSOR_HW_PIN_AVDD));
			#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
			atomic_dec(enable_cnt);
		}
	} else {
#ifdef CONFIG_TRAN_CAMERA_EXT_LDO_REGULATOR
		if (pin == IMGSENSOR_HW_PIN_AVDD || pin == IMGSENSOR_HW_PIN_DVDD) {
			if (pin_state != IMGSENSOR_HW_PIN_STATE_LEVEL_0)
				ext_ldo_regulator_enable(pin, regulator_voltage[pin_state - IMGSENSOR_HW_PIN_STATE_LEVEL_0]);
			else
				ext_ldo_regulator_disable(pin);
			return IMGSENSOR_RETURN_SUCCESS;
		}
#endif
		PK_PR_ERR("regulator == NULL %d %d %d\n",
				reg_type_offset,
				pin,
				IMGSENSOR_HW_PIN_AVDD);
	}

	return IMGSENSOR_RETURN_SUCCESS;
}

#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
static void check_for_regulator_get(struct REGULATOR *preg,
	struct device *pdevice, unsigned int sensor_index,
	unsigned int regulator_index)
{
	struct device_node *pof_node = NULL;
	char str_regulator_name[LENGTH_FOR_SNPRINTF];

	PK_PR_ERR("[%s]sensor_idx:%d regulator_idx: %d\n", __func__, sensor_index, regulator_index);

	if (!preg || !pdevice) {
		PK_PR_ERR("Fatal: Null ptr.preg:%pK,pdevice:%pK\n", preg, pdevice);
		return;
	}

	if (sensor_index >= IMGSENSOR_SENSOR_IDX_MAX_NUM ||
			regulator_index >= REGULATOR_TYPE_MAX_NUM ) {
		PK_PR_ERR("[%s]Invalid sensor_idx:%d regulator_idx: %d\n", __func__, sensor_index, regulator_index);
		return;
	}

	mutex_lock(&g_regulator_state_mutex);

	if (regulator_status[sensor_index][regulator_index] == false) {
		pof_node = pdevice->of_node;
		pdevice->of_node = of_node_record;

		snprintf(str_regulator_name, sizeof(str_regulator_name), "cam%d_%s", sensor_index, regulator_control[regulator_index].pregulator_type);
		preg->pregulator[sensor_index][regulator_index] = regulator_get(pdevice, str_regulator_name);

		if (preg != NULL)
			regulator_status[sensor_index][regulator_index] = true;
		else
			PK_PR_ERR("get regulator failed.\n");

		pdevice->of_node = pof_node;
	}

	mutex_unlock(&g_regulator_state_mutex);

	return;
}

static void check_for_regulator_put(struct REGULATOR *preg,
	unsigned int sensor_index, unsigned int regulator_index)
{
	if (!preg) {
		PK_PR_ERR("Fatal: Null ptr.\n");
		return;
	}

	PK_PR_ERR("[%s]sensor_idx:%d regulator_idx: %d\n", __func__, sensor_index, regulator_index);

	if (sensor_index >= IMGSENSOR_SENSOR_IDX_MAX_NUM || regulator_index >= REGULATOR_TYPE_MAX_NUM ) {
		PK_PR_ERR("[%s]Invalid sensor_idx:%d regulator_idx: %d\n", __func__, sensor_index, regulator_index);
		return;
	}

	mutex_lock(&g_regulator_state_mutex);

	if (regulator_status[sensor_index][regulator_index] == true) {
		regulator_put(preg->pregulator[sensor_index][regulator_index]);
		preg->pregulator[sensor_index][regulator_index]=NULL;
		regulator_status[sensor_index][regulator_index] = false;
	}

	mutex_unlock(&g_regulator_state_mutex);
	return;
}
#endif /*OPLUS_FEATURE_CAMERA_COMMON*/
static enum IMGSENSOR_RETURN regulator_dump(void *pinstance)
{
	struct REGULATOR *preg = (struct REGULATOR *)pinstance;
	int i, j;
	#if !defined(OPLUS_FEATURE_CAMERA_COMMON) || defined(CONFIG_CUSTOM_TRAN_PROJECT)
	int enable = 0;
	#endif

	for (j = IMGSENSOR_SENSOR_IDX_MIN_NUM;
		j < IMGSENSOR_SENSOR_IDX_MAX_NUM;
		j++) {

		for (i = REGULATOR_TYPE_VCAMA;
		i < REGULATOR_TYPE_MAX_NUM;
		i++) {
			#if defined(OPLUS_FEATURE_CAMERA_COMMON) && !defined(CONFIG_CUSTOM_TRAN_PROJECT)
			if (IS_ERR_OR_NULL(preg->pregulator[j][i])) {
				PK_DBG("regulator idx(%d, %d) alread release ", j, i);
				return IMGSENSOR_RETURN_SUCCESS;
			} else {
				if (regulator_is_enabled(preg->pregulator[j][i]) &&
					atomic_read(&preg->enable_cnt[j][i]) != 0)
					PK_DBG("index= %d %s = %d\n",
						j,
						regulator_control[i].pregulator_type,
						regulator_get_voltage(
						preg->pregulator[j][i]));
			}
			#else
			if (!preg->pregulator[j][i])
				continue;

			if (regulator_is_enabled(preg->pregulator[j][i]) &&
				atomic_read(&preg->enable_cnt[j][i]) != 0)
				enable = 1;
			else
				enable = 0;

			PK_DBG("[sensor_dump][regulator] index= %d, %s = %d, enable = %d\n",
				j,
				regulator_control[i].pregulator_type,
				regulator_get_voltage(preg->pregulator[j][i]),
				enable);
			#endif
		}
	}
	return IMGSENSOR_RETURN_SUCCESS;
}

static struct IMGSENSOR_HW_DEVICE device = {
	.id        = IMGSENSOR_HW_ID_REGULATOR,
	.pinstance = (void *)&reg_instance,
	.init      = regulator_init,
	.set       = regulator_set,
	.release   = regulator_release,
	.dump      = regulator_dump
};

enum IMGSENSOR_RETURN imgsensor_hw_regulator_open(
	struct IMGSENSOR_HW_DEVICE **pdevice)
{
	*pdevice = &device;
	return IMGSENSOR_RETURN_SUCCESS;
}


#ifdef CONFIG_TRAN_CAMERA_EXT_LDO_REGULATOR
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>

#define EXT_LDO_NAME         "cam_vdd_ext"
#define EXT_LDO_REG_CONFIG   0x02
#define EXT_LDO_REG_DVDD1    0x03
#define EXT_LDO_REG_DVDD2    0x04
#define EXT_LDO_REG_AVDD1    0x05
#define EXT_LDO_REG_AVDD2    0x06
#define EXT_LDO_REG_ENABLE   0x0E

struct cam_ext_ldo_data {
	struct i2c_client *client;
	struct mutex lock;
};

static struct i2c_client *g_ext_ldo_i2c_client;
static struct pinctrl *g_ext_ldo_pinctrl;
static struct pinctrl_state *g_ext_ldo_en_high;
static struct pinctrl_state *g_ext_ldo_en_low;

static int ext_ldo_pinctrl_set(int state)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(g_ext_ldo_pinctrl))
		return -EINVAL;

	if (state) {
		if (!IS_ERR_OR_NULL(g_ext_ldo_en_high))
			ret = pinctrl_select_state(g_ext_ldo_pinctrl, g_ext_ldo_en_high);
	} else {
		if (!IS_ERR_OR_NULL(g_ext_ldo_en_low))
			ret = pinctrl_select_state(g_ext_ldo_pinctrl, g_ext_ldo_en_low);
	}
	return ret;
}

static int ext_ldo_read_reg(struct i2c_client *client, u8 reg)
{
	struct cam_ext_ldo_data *chip = i2c_get_clientdata(client);
	int val;

	mutex_lock(&chip->lock);
	val = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);
	return val;
}

static int ext_ldo_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct cam_ext_ldo_data *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);
	return ret;
}

static int ext_ldo_regulator_enable(enum IMGSENSOR_HW_PIN pin, int voltage_uv)
{
	int reg_en, reg_cfg, vset;
	int ret = 0;

	if (!g_ext_ldo_i2c_client) {
		PK_PR_ERR("cam_vdd_ext i2c client is NULL\n");
		return -ENODEV;
	}

	ext_ldo_pinctrl_set(0);

	reg_en = ext_ldo_read_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE);
	if (reg_en < 0) {
		PK_PR_ERR("cam_vdd_ext failed reading 0x0E: %d\n", reg_en);
		reg_en = 0;
	}

	reg_cfg = ext_ldo_read_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_CONFIG);
	if (reg_cfg < 0)
		reg_cfg = 0;

	if (pin == IMGSENSOR_HW_PIN_AVDD) {
		vset = (voltage_uv >= 1200000) ? ((voltage_uv - 1200000) / 12500) : 0;
		if (vset > 0xff)
			vset = 0xff;
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_AVDD1, (u8)vset);
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_AVDD2, (u8)vset);
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_CONFIG, (u8)(reg_cfg | 0x8C));
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE, (u8)(reg_en | 0x0C));
	} else if (pin == IMGSENSOR_HW_PIN_DVDD) {
		vset = (voltage_uv >= 600000) ? ((voltage_uv - 600000) / 6000) : 0;
		if (vset > 0xff)
			vset = 0xff;
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_DVDD1, (u8)vset);
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_DVDD2, (u8)vset);
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_CONFIG, (u8)(reg_cfg | 0x83));
		ret |= ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE, (u8)(reg_en | 0x03));
	}

	if (ret)
		pr_err("cam_vdd_ext enable pin=%d uv=%d write err=%d\n", pin, voltage_uv, ret);
	return ret;
}

static int ext_ldo_regulator_disable(enum IMGSENSOR_HW_PIN pin)
{
	int reg_en;
	int ret = 0;

	if (!g_ext_ldo_i2c_client)
		return -ENODEV;

	reg_en = ext_ldo_read_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE);
	if (reg_en < 0)
		return reg_en;

	if (pin == IMGSENSOR_HW_PIN_AVDD)
		ret = ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE, (u8)(reg_en & 0x03));
	else if (pin == IMGSENSOR_HW_PIN_DVDD)
		ret = ext_ldo_write_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE, (u8)(reg_en & 0x0C));

	ext_ldo_read_reg(g_ext_ldo_i2c_client, EXT_LDO_REG_ENABLE);
	return ret;
}

static int ext_ldo_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct cam_ext_ldo_data *chip;
	int chip_id, wr_ret;

	PK_INFO("cam_vdd_ext i2c probe start\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		PK_PR_ERR("cam_vdd_ext i2c check functionality failed\n");
		return -ENODEV;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);
	g_ext_ldo_i2c_client = client;

	ext_ldo_pinctrl_set(0);
	chip_id = ext_ldo_read_reg(client, 0x00);
	wr_ret = ext_ldo_write_reg(client, EXT_LDO_REG_ENABLE, 0x00);
	pr_info("cam_vdd_ext i2c probe done (chip_id=0x%x wr_ret=%d)\n", chip_id, wr_ret);
	return 0;
}

static int ext_ldo_i2c_remove(struct i2c_client *client)
{
	struct cam_ext_ldo_data *chip = i2c_get_clientdata(client);

	if (chip)
		ext_ldo_write_reg(client, EXT_LDO_REG_ENABLE, 0x00);
	g_ext_ldo_i2c_client = NULL;
	return 0;
}

static const struct i2c_device_id ext_ldo_i2c_id[] = {
	{EXT_LDO_NAME, 0},
	{}
};

static const struct of_device_id ext_ldo_i2c_of_match[] = {
	{.compatible = "mediatek,cam_vdd_ext"},
	{}
};

static struct i2c_driver ext_ldo_i2c_driver = {
	.driver = {
		.name = EXT_LDO_NAME,
		.of_match_table = ext_ldo_i2c_of_match,
	},
	.probe = ext_ldo_i2c_probe,
	.remove = ext_ldo_i2c_remove,
	.id_table = ext_ldo_i2c_id,
};

static int ext_ldo_platform_probe(struct platform_device *pdev)
{
	int ret;

	PK_INFO("cam_vdd_ext platform probe start\n");
	g_ext_ldo_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(g_ext_ldo_pinctrl)) {
		PK_PR_ERR("cam_vdd_ext failed to get pinctrl\n");
		return PTR_ERR(g_ext_ldo_pinctrl);
	}

	g_ext_ldo_en_high = pinctrl_lookup_state(g_ext_ldo_pinctrl, "extldo_en_high");
	if (IS_ERR(g_ext_ldo_en_high))
		PK_PR_ERR("cam_vdd_ext missing extldo_en_high\n");

	g_ext_ldo_en_low = pinctrl_lookup_state(g_ext_ldo_pinctrl, "extldo_en_low");
	if (IS_ERR(g_ext_ldo_en_low))
		PK_PR_ERR("cam_vdd_ext missing extldo_en_low\n");

	ret = i2c_add_driver(&ext_ldo_i2c_driver);
	if (ret)
		PK_PR_ERR("cam_vdd_ext i2c_add_driver failed: %d\n", ret);
	return ret;
}

static int ext_ldo_platform_remove(struct platform_device *pdev)
{
	i2c_del_driver(&ext_ldo_i2c_driver);
	return 0;
}

static const struct of_device_id ext_ldo_of_match[] = {
	{.compatible = "mediatek,cam_vdd_ext"},
	{}
};

static struct platform_driver ext_ldo_platform_driver = {
	.probe = ext_ldo_platform_probe,
	.remove = ext_ldo_platform_remove,
	.driver = {
		.name = EXT_LDO_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ext_ldo_of_match,
	},
};

static int __init cam_vdd_ext_init(void)
{
	return platform_driver_register(&ext_ldo_platform_driver);
}

static void __exit cam_vdd_ext_exit(void)
{
	platform_driver_unregister(&ext_ldo_platform_driver);
}

module_init(cam_vdd_ext_init);
module_exit(cam_vdd_ext_exit);
#endif /* CONFIG_TRAN_CAMERA_EXT_LDO_REGULATOR */
