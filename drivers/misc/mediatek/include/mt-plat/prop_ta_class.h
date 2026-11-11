/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2026 Lynx Universal
 */

#ifndef __LINUX_PROP_TA_CLASS_H
#define __LINUX_PROP_TA_CLASS_H

#include <linux/device.h>
#include <linux/types.h>

struct prop_ta_device;

struct prop_ta_ops {
	int (*get_verinfo)(struct prop_ta_device *ta, u8 *buf, int len);
	int (*get_fwcode)(struct prop_ta_device *ta, u8 *buf, int len);
	int (*get_mfinfo)(struct prop_ta_device *ta, u8 *buf, int len);
	int (*get_datecode)(struct prop_ta_device *ta, u8 *buf, int len);
	int (*get_min_voltage)(struct prop_ta_device *ta, u32 *mV);
	int (*get_max_voltage)(struct prop_ta_device *ta, u32 *mV);
	int (*get_min_current)(struct prop_ta_device *ta, u32 *mA);
	int (*get_max_current)(struct prop_ta_device *ta, u32 *mA);
	int (*get_output_voltage)(struct prop_ta_device *ta, u32 *mV);
	int (*get_output_current)(struct prop_ta_device *ta, u32 *mA);
	int (*get_status)(struct prop_ta_device *ta, u32 *status);
	int (*get_temp1)(struct prop_ta_device *ta, int *temp);
	int (*get_temp2)(struct prop_ta_device *ta, int *temp);
	int (*set_output_control)(struct prop_ta_device *ta, u8 val);
	int (*set_mode)(struct prop_ta_device *ta, u8 val);
	int (*set_voltage)(struct prop_ta_device *ta, u32 mV);
	int (*set_current)(struct prop_ta_device *ta, u32 mA);
	int (*suspend)(struct prop_ta_device *ta);
	int (*resume)(struct prop_ta_device *ta);
	int (*set_voltage_current)(struct prop_ta_device *ta, u32 mV, u32 mA);
	int (*get_output_voltage_current)(struct prop_ta_device *ta,
					  u32 *mV, u32 *mA);
	int (*authentication)(struct prop_ta_device *ta);
	int (*enable_wdt)(struct prop_ta_device *ta, bool en);
	int (*set_wdt)(struct prop_ta_device *ta, u32 ms);
	int (*get_power_limit)(struct prop_ta_device *ta, u32 *pdp);
};

struct prop_ta_device {
	const char *name;
	struct device *dev;
	const struct prop_ta_ops *ops;
	const struct attribute_group **groups;
	void *drv_data;
};

static inline void *prop_ta_get_drvdata(const struct prop_ta_device *ta)
{
	return ta->drv_data;
}

static inline void prop_ta_set_drvdata(struct prop_ta_device *ta, void *data)
{
	ta->drv_data = data;
}

extern int prop_ta_classdev_register(struct device *parent,
				     struct prop_ta_device *ta);
extern void prop_ta_classdev_unregister(struct prop_ta_device *ta);
extern int devm_prop_ta_classdev_register(struct device *parent,
					  struct prop_ta_device *ta);
extern struct prop_ta_device *prop_ta_device_get_by_name(const char *name);

extern int prop_ta_device_get_power_limit(struct prop_ta_device *ta, u32 *pdp);
extern int prop_ta_device_get_verinfo(struct prop_ta_device *ta, u8 *buf,
				      int len);
extern int prop_ta_device_get_fwcode(struct prop_ta_device *ta, u8 *buf,
				     int len);
extern int prop_ta_device_get_mfinfo(struct prop_ta_device *ta, u8 *buf,
				     int len);
extern int prop_ta_device_get_datecode(struct prop_ta_device *ta, u8 *buf,
				       int len);
extern int prop_ta_device_get_min_voltage(struct prop_ta_device *ta, u32 *mV);
extern int prop_ta_device_get_max_voltage(struct prop_ta_device *ta, u32 *mV);
extern int prop_ta_device_get_min_current(struct prop_ta_device *ta, u32 *mA);
extern int prop_ta_device_get_max_current(struct prop_ta_device *ta, u32 *mA);
extern int prop_ta_device_get_output_voltage(struct prop_ta_device *ta,
					     u32 *mV);
extern int prop_ta_device_get_output_current(struct prop_ta_device *ta,
					     u32 *mA);
extern int prop_ta_device_get_status(struct prop_ta_device *ta, u32 *status);
extern int prop_ta_device_get_temp1(struct prop_ta_device *ta, int *temp);
extern int prop_ta_device_get_temp2(struct prop_ta_device *ta, int *temp);
extern int prop_ta_device_set_output_control(struct prop_ta_device *ta, u8 val);
extern int prop_ta_device_set_mode(struct prop_ta_device *ta, u8 val);
extern int prop_ta_device_set_voltage(struct prop_ta_device *ta, u32 mV);
extern int prop_ta_device_set_current(struct prop_ta_device *ta, u32 mA);
extern int prop_ta_device_set_voltage_current(struct prop_ta_device *ta,
					      u32 mV, u32 mA);
extern int prop_ta_device_get_output_voltage_current(struct prop_ta_device *ta,
						     u32 *mV, u32 *mA);
extern int prop_ta_device_authentication(struct prop_ta_device *ta);
extern int prop_ta_device_enable_wdt(struct prop_ta_device *ta, bool en);
extern int prop_ta_device_set_wdt(struct prop_ta_device *ta, u32 ms);

#endif /* __LINUX_PROP_TA_CLASS_H */
