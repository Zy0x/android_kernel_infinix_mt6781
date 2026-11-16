/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AWINIC_HAPTIC_H_
#define _AWINIC_HAPTIC_H_

#include <linux/i2c.h>
#include <linux/device.h>

#define AW8624_STRUCT_MAGIC		0x41572400U
#define AW8622X_STRUCT_MAGIC		0x41572200U

enum awinic_chip_name {
	AW_CHIP_NULL = 0,
	AW8624_CHIP = 1,
	AW86223_CHIP = 2,
	AW86224_5_CHIP = 3,
	AW86214_CHIP = 4,
};

int aw8624_probe_dev(struct i2c_client *i2c, const struct i2c_device_id *id,
		     int reset_gpio, int irq_gpio);
int aw8624_remove_dev(struct i2c_client *i2c);
int aw8624_suspend_dev(struct device *dev);
int aw8624_resume_dev(struct device *dev);

int aw8622x_probe_dev(struct i2c_client *i2c, int reset_gpio, int irq_gpio,
		      unsigned char chip_name);
int aw8622x_remove_dev(struct i2c_client *i2c);
int aw8622x_suspend_dev(struct device *dev);
int aw8622x_resume_dev(struct device *dev);

#endif
