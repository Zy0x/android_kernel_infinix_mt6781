/*
 * Copyright (C) 2021 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>

#include "flashlight-core.h"
#include "flashlight-dt.h"

#define OCP81375_NAME "flashlights-ocp81375"

#ifndef OCP81375_DTNAME
#define OCP81375_DTNAME "mediatek,flashlights_ocp81375"
#endif
#ifndef OCP81375_DTNAME_I2C
#define OCP81375_DTNAME_I2C "mediatek,strobe_main"
#endif

#define OCP81375_REG_ENABLE          (0x01)
#define OCP81375_MASK_ENABLE_LED1    (0x01)
#define OCP81375_MASK_ENABLE_LED2    (0x02)
#define OCP81375_DISABLE             (0x00)
#define OCP81375_ENABLE_LED1         (0x01)
#define OCP81375_ENABLE_LED1_TORCH   (0x09)
#define OCP81375_ENABLE_LED1_FLASH   (0x0D)
#define OCP81375_ENABLE_LED2         (0x02)
#define OCP81375_ENABLE_LED2_TORCH   (0x0A)
#define OCP81375_ENABLE_LED2_FLASH   (0x0E)

#define OCP81375_REG_FLASH_LEVEL_LED1 (0x03)
#define OCP81375_REG_FLASH_LEVEL_LED2 (0x04)
#define OCP81375_REG_TORCH_LEVEL_LED1 (0x05)
#define OCP81375_REG_TORCH_LEVEL_LED2 (0x06)
#define OCP81375_REG_IVFM            (0x07)
#define OCP81375_REG_TIMING_CONF     (0x08)
#define OCP81375_DEVICE_ID           (0x0C)
#define OCP81375_CHIP_ID_VAL         (0x3A)

#define OCP81375_CHANNEL_NUM 2
#define OCP81375_CHANNEL_CH1 0
#define OCP81375_CHANNEL_CH2 1

#define OCP81375_LEVEL_NUM 30
#define OCP81375_LEVEL_TORCH 3
#define OCP81375_HW_TIMEOUT 400 /* ms */

#define OCP81375_PINCTRL_PIN_HWEN 0
#define OCP81375_PINCTRL_PINSTATE_LOW 0
#define OCP81375_PINCTRL_PINSTATE_HIGH 1
#define OCP81375_PINCTRL_STATE_HWEN_HIGH "ocp81375hwen_high"
#define OCP81375_PINCTRL_STATE_HWEN_LOW  "ocp81375hwen_low"

static DEFINE_MUTEX(ocp81375_mutex);

static struct pinctrl *ocp81375_pinctrl;
static struct pinctrl_state *ocp81375_hwen_high;
static struct pinctrl_state *ocp81375_hwen_low;

static struct work_struct ocp81375_work_ch1;
static struct work_struct ocp81375_work_ch2;

static struct hrtimer ocp81375_timer_ch1;
static struct hrtimer ocp81375_timer_ch2;
static unsigned int ocp81375_timeout_ms[OCP81375_CHANNEL_NUM];

struct ocp81375_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};

struct ocp81375_chip_data {
	struct i2c_client *client;
	struct ocp81375_platform_data *pdata;
	struct mutex lock;
};

static struct platform_device *ocp81375_pdev;
static struct ocp81375_platform_data *ocp81375_pdata;
static struct i2c_client *ocp81375_i2c_client;

static int use_count;

static int ocp81375_pinctrl_init(struct platform_device *pdev)
{
	int ret = 0;

	ocp81375_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(ocp81375_pinctrl)) {
		pr_err("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(ocp81375_pinctrl);
		ocp81375_pinctrl = NULL;
		return ret;
	}

	ocp81375_hwen_high = pinctrl_lookup_state(
			ocp81375_pinctrl, OCP81375_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(ocp81375_hwen_high)) {
		pr_err("Failed to init (%s)\n", OCP81375_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(ocp81375_hwen_high);
		ocp81375_hwen_high = NULL;
	}

	ocp81375_hwen_low = pinctrl_lookup_state(
			ocp81375_pinctrl, OCP81375_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(ocp81375_hwen_low)) {
		pr_err("Failed to init (%s)\n", OCP81375_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(ocp81375_hwen_low);
		ocp81375_hwen_low = NULL;
	}

	return ret;
}

static int ocp81375_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (!ocp81375_pinctrl) {
		pr_err("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case OCP81375_PINCTRL_PIN_HWEN:
		if (state == OCP81375_PINCTRL_PINSTATE_LOW &&
				ocp81375_hwen_low && !IS_ERR(ocp81375_hwen_low))
			pinctrl_select_state(ocp81375_pinctrl, ocp81375_hwen_low);
		else if (state == OCP81375_PINCTRL_PINSTATE_HIGH &&
				ocp81375_hwen_high && !IS_ERR(ocp81375_hwen_high))
			pinctrl_select_state(ocp81375_pinctrl, ocp81375_hwen_high);
		else {
			pr_err("set err, pin(%d) state(%d)\n", pin, state);
			ret = -1;
		}
		break;
	default:
		pr_err("set err, pin(%d) state(%d)\n", pin, state);
		ret = -1;
		break;
	}

	return ret;
}

static const int ocp81375_current[OCP81375_LEVEL_NUM] = {
	12, 74, 136, 198, 260, 322, 384, 446, 508, 570,
	632, 694, 756, 818, 880, 942, 1004, 1066, 1128, 1190,
	1252, 1314, 1376, 1438, 1500, 1562, 1624, 1686, 1748, 1900
};

static unsigned char ocp81375_torch_code(int cur)
{
	int code = (cur * 100 - 390) / 390;

	if (code < 0)
		code = 0;
	else if (code > 0x7f)
		code = 0x7f;
	return (unsigned char)code;
}

static unsigned char ocp81375_flash_code(int cur)
{
	int code = (cur * 100 - 1563) / 1563;

	if (code < 0)
		code = 0;
	else if (code > 0x7f)
		code = 0x7f;
	return (unsigned char)code;
}

static unsigned char ocp81375_reg_enable;
static int ocp81375_level_ch1 = -1;
static int ocp81375_level_ch2 = -1;

static int ocp81375_is_torch(int level)
{
	if (level >= OCP81375_LEVEL_TORCH)
		return -1;
	return 0;
}

static int ocp81375_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= OCP81375_LEVEL_NUM)
		level = OCP81375_LEVEL_NUM - 1;
	return level;
}

static int ocp81375_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	struct ocp81375_chip_data *chip;

	if (!client)
		return -ENODEV;
	chip = i2c_get_clientdata(client);
	if (!chip)
		return -ENODEV;

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		pr_err("failed writing at 0x%02x\n", reg);

	return ret;
}

static int ocp81375_read_reg(struct i2c_client *client, u8 reg)
{
	int val;
	struct ocp81375_chip_data *chip;

	if (!client)
		return -ENODEV;
	chip = i2c_get_clientdata(client);
	if (!chip)
		return -ENODEV;

	mutex_lock(&chip->lock);
	val = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);

	return val;
}

static int ocp81375_enable_ch1(void)
{
	unsigned char reg = OCP81375_REG_ENABLE;
	unsigned char val;

	ocp81375_reg_enable &= (~OCP81375_ENABLE_LED1_FLASH);
	if (!ocp81375_is_torch(ocp81375_level_ch1))
		ocp81375_reg_enable |= OCP81375_ENABLE_LED1_TORCH;
	else
		ocp81375_reg_enable |= OCP81375_ENABLE_LED1_FLASH;

	val = ocp81375_reg_enable;
	return ocp81375_write_reg(ocp81375_i2c_client, reg, val);
}

static int ocp81375_enable_ch2(void)
{
	unsigned char reg = OCP81375_REG_ENABLE;
	unsigned char val;

	ocp81375_reg_enable &= (~OCP81375_ENABLE_LED2_FLASH);
	if (!ocp81375_is_torch(ocp81375_level_ch2))
		ocp81375_reg_enable |= OCP81375_ENABLE_LED2_TORCH;
	else
		ocp81375_reg_enable |= OCP81375_ENABLE_LED2_FLASH;

	val = ocp81375_reg_enable;
	return ocp81375_write_reg(ocp81375_i2c_client, reg, val);
}

static int ocp81375_enable(int channel)
{
	if (channel == OCP81375_CHANNEL_CH1)
		return ocp81375_enable_ch1();
	else if (channel == OCP81375_CHANNEL_CH2)
		return ocp81375_enable_ch2();
	pr_err("Error channel %d\n", channel);
	return -1;
}

static int ocp81375_disable_ch1(void)
{
	unsigned char reg = OCP81375_REG_ENABLE;
	unsigned char val;

	if (ocp81375_reg_enable & OCP81375_MASK_ENABLE_LED2)
		ocp81375_reg_enable &= (~OCP81375_ENABLE_LED1);
	else
		ocp81375_reg_enable &= (~OCP81375_ENABLE_LED1_FLASH);

	val = ocp81375_reg_enable;
	return ocp81375_write_reg(ocp81375_i2c_client, reg, val);
}

static int ocp81375_disable_ch2(void)
{
	unsigned char reg = OCP81375_REG_ENABLE;
	unsigned char val;

	if (ocp81375_reg_enable & OCP81375_MASK_ENABLE_LED1)
		ocp81375_reg_enable &= (~OCP81375_ENABLE_LED2);
	else
		ocp81375_reg_enable &= (~OCP81375_ENABLE_LED2_FLASH);

	val = ocp81375_reg_enable;
	return ocp81375_write_reg(ocp81375_i2c_client, reg, val);
}

static int ocp81375_disable(int channel)
{
	if (channel == OCP81375_CHANNEL_CH1)
		return ocp81375_disable_ch1();
	else if (channel == OCP81375_CHANNEL_CH2)
		return ocp81375_disable_ch2();
	pr_err("Error channel %d\n", channel);
	return -1;
}

static int ocp81375_set_level_ch1(int level)
{
	int cur, ret = 0;

	level = ocp81375_verify_level(level);
	ocp81375_level_ch1 = level;
	cur = ocp81375_current[level];

	if (!ocp81375_is_torch(level))
		ret = ocp81375_write_reg(ocp81375_i2c_client,
				OCP81375_REG_TORCH_LEVEL_LED1,
				ocp81375_torch_code(cur));
	ret |= ocp81375_write_reg(ocp81375_i2c_client,
			OCP81375_REG_FLASH_LEVEL_LED1,
			ocp81375_flash_code(cur));

	return ret;
}

static int ocp81375_set_level_ch2(int level)
{
	int cur, ret = 0;

	level = ocp81375_verify_level(level);
	ocp81375_level_ch2 = level;
	cur = ocp81375_current[level];

	if (!ocp81375_is_torch(level))
		ret = ocp81375_write_reg(ocp81375_i2c_client,
				OCP81375_REG_TORCH_LEVEL_LED2,
				ocp81375_torch_code(cur));
	ret |= ocp81375_write_reg(ocp81375_i2c_client,
			OCP81375_REG_FLASH_LEVEL_LED2,
			ocp81375_flash_code(cur));

	return ret;
}

static int ocp81375_set_level(int channel, int level)
{
	if (channel == OCP81375_CHANNEL_CH1)
		return ocp81375_set_level_ch1(level);
	else if (channel == OCP81375_CHANNEL_CH2)
		return ocp81375_set_level_ch2(level);
	pr_err("Error channel %d\n", channel);
	return -1;
}

int ocp81375_init(void)
{
	int ret;

	ocp81375_pinctrl_set(
			OCP81375_PINCTRL_PIN_HWEN, OCP81375_PINCTRL_PINSTATE_HIGH);
	msleep(5);

	ocp81375_reg_enable = OCP81375_DISABLE;
	ret = ocp81375_write_reg(ocp81375_i2c_client, OCP81375_REG_ENABLE, 0x00);
	ret |= ocp81375_write_reg(ocp81375_i2c_client, OCP81375_REG_IVFM, 0x09);
	ret |= ocp81375_write_reg(ocp81375_i2c_client, OCP81375_REG_TIMING_CONF, 0x1F);

	return ret;
}

int ocp81375_uninit(void)
{
	ocp81375_disable(OCP81375_CHANNEL_CH1);
	ocp81375_disable(OCP81375_CHANNEL_CH2);
	ocp81375_pinctrl_set(
			OCP81375_PINCTRL_PIN_HWEN, OCP81375_PINCTRL_PINSTATE_LOW);
	return 0;
}

static void ocp81375_work_disable_ch1(struct work_struct *data)
{
	ocp81375_disable_ch1();
}

static void ocp81375_work_disable_ch2(struct work_struct *data)
{
	ocp81375_disable_ch2();
}

static enum hrtimer_restart ocp81375_timer_func_ch1(struct hrtimer *timer)
{
	schedule_work(&ocp81375_work_ch1);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart ocp81375_timer_func_ch2(struct hrtimer *timer)
{
	schedule_work(&ocp81375_work_ch2);
	return HRTIMER_NORESTART;
}

static int ocp81375_timer_start(int channel, ktime_t ktime)
{
	if (channel == OCP81375_CHANNEL_CH1)
		hrtimer_start(&ocp81375_timer_ch1, ktime, HRTIMER_MODE_REL);
	else if (channel == OCP81375_CHANNEL_CH2)
		hrtimer_start(&ocp81375_timer_ch2, ktime, HRTIMER_MODE_REL);
	else
		return -1;
	return 0;
}

static int ocp81375_timer_cancel(int channel)
{
	if (channel == OCP81375_CHANNEL_CH1)
		hrtimer_cancel(&ocp81375_timer_ch1);
	else if (channel == OCP81375_CHANNEL_CH2)
		hrtimer_cancel(&ocp81375_timer_ch2);
	else
		return -1;
	return 0;
}

static int ocp81375_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;
	unsigned int s, ns;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	if (channel >= OCP81375_CHANNEL_NUM || channel < 0) {
		pr_err("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		ocp81375_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		ocp81375_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		if (fl_arg->arg == 1) {
			if (ocp81375_timeout_ms[channel]) {
				s = ocp81375_timeout_ms[channel] / 1000;
				ns = ocp81375_timeout_ms[channel] % 1000 * 1000000;
				ktime = ktime_set(s, ns);
				ocp81375_timer_start(channel, ktime);
			}
			ocp81375_enable(channel);
		} else {
			ocp81375_disable(channel);
			ocp81375_timer_cancel(channel);
		}
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		fl_arg->arg = OCP81375_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		fl_arg->arg = OCP81375_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = ocp81375_verify_level(fl_arg->arg);
		fl_arg->arg = ocp81375_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		fl_arg->arg = OCP81375_HW_TIMEOUT;
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int ocp81375_open(void)
{
	return 0;
}

static int ocp81375_release(void)
{
	ocp81375_uninit();
	return 0;
}

static int ocp81375_set_driver(int set)
{
	int ret = 0;

	mutex_lock(&ocp81375_mutex);
	if (set) {
		if (!use_count)
			ret = ocp81375_init();
		use_count++;
	} else {
		use_count--;
		if (!use_count)
			ret = ocp81375_uninit();
		if (use_count < 0)
			use_count = 0;
	}
	mutex_unlock(&ocp81375_mutex);

	return ret;
}

static ssize_t ocp81375_strobe_store(struct flashlight_arg arg)
{
	ocp81375_set_driver(1);
	ocp81375_set_level(arg.channel, arg.level);
	ocp81375_timeout_ms[arg.channel] = 0;
	ocp81375_enable(arg.channel);
	msleep(arg.dur);
	ocp81375_disable(arg.channel);
	ocp81375_set_driver(0);

	return 0;
}

static struct flashlight_operations ocp81375_ops = {
	ocp81375_open,
	ocp81375_release,
	ocp81375_ioctl,
	ocp81375_strobe_store,
	ocp81375_set_driver
};

static int ocp81375_i2c_probe(
		struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ocp81375_chip_data *chip;
	int dev_id;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	if (!ocp81375_pdev)
		return -EPROBE_DEFER;

	if (ocp81375_pinctrl_init(ocp81375_pdev)) {
		pr_err("Failed to init pinctrl.\n");
		if (ocp81375_pinctrl) {
			devm_pinctrl_put(ocp81375_pinctrl);
			ocp81375_pinctrl = NULL;
			ocp81375_hwen_high = NULL;
			ocp81375_hwen_low = NULL;
		}
		return -ENODEV;
	}

	chip = kzalloc(sizeof(struct ocp81375_chip_data), GFP_KERNEL);
	if (!chip) {
		if (ocp81375_pinctrl) {
			devm_pinctrl_put(ocp81375_pinctrl);
			ocp81375_pinctrl = NULL;
			ocp81375_hwen_high = NULL;
			ocp81375_hwen_low = NULL;
		}
		return -ENOMEM;
	}

	chip->client = client;
	chip->pdata = ocp81375_pdata;
	i2c_set_clientdata(client, chip);
	ocp81375_i2c_client = client;
	mutex_init(&chip->lock);

	INIT_WORK(&ocp81375_work_ch1, ocp81375_work_disable_ch1);
	INIT_WORK(&ocp81375_work_ch2, ocp81375_work_disable_ch2);
	hrtimer_init(&ocp81375_timer_ch1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ocp81375_timer_ch1.function = ocp81375_timer_func_ch1;
	hrtimer_init(&ocp81375_timer_ch2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ocp81375_timer_ch2.function = ocp81375_timer_func_ch2;
	ocp81375_timeout_ms[OCP81375_CHANNEL_CH1] = 100;
	ocp81375_timeout_ms[OCP81375_CHANNEL_CH2] = 100;

	ocp81375_pinctrl_set(OCP81375_PINCTRL_PIN_HWEN, OCP81375_PINCTRL_PINSTATE_HIGH);
	mdelay(5);
	dev_id = ocp81375_read_reg(ocp81375_i2c_client, OCP81375_DEVICE_ID);
	ocp81375_pinctrl_set(OCP81375_PINCTRL_PIN_HWEN, OCP81375_PINCTRL_PINSTATE_LOW);

	pr_info("ocp81375_flash_read reg:0x%x val:0x%x\n", OCP81375_DEVICE_ID, dev_id);
	if (dev_id < 0 || (dev_id & 0xffff) != OCP81375_CHIP_ID_VAL) {
		pr_info("ocp81375 DEVICE_ID mismatch (0x%x), skip.\n", dev_id);
		if (ocp81375_pinctrl) {
			devm_pinctrl_put(ocp81375_pinctrl);
			ocp81375_pinctrl = NULL;
			ocp81375_hwen_high = NULL;
			ocp81375_hwen_low = NULL;
		}
		err = -ENODEV;
		goto err_free;
	}

	if (flashlight_dev_register(OCP81375_NAME, &ocp81375_ops)) {
		pr_err("ocp81375 flashlight_dev_register fail.\n");
		if (ocp81375_pinctrl) {
			devm_pinctrl_put(ocp81375_pinctrl);
			ocp81375_pinctrl = NULL;
			ocp81375_hwen_high = NULL;
			ocp81375_hwen_low = NULL;
		}
		err = -EFAULT;
		goto err_free;
	}

	use_count = 0;
	pr_info("ocp81375 i2c probe success (chip id 0x%02x)\n", dev_id);
	return 0;

err_free:
	ocp81375_i2c_client = NULL;
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	return err;
}

static int ocp81375_i2c_remove(struct i2c_client *client)
{
	struct ocp81375_chip_data *chip = i2c_get_clientdata(client);

	flashlight_dev_unregister(OCP81375_NAME);
	flush_work(&ocp81375_work_ch1);
	flush_work(&ocp81375_work_ch2);
	ocp81375_i2c_client = NULL;
	if (ocp81375_pinctrl) {
		devm_pinctrl_put(ocp81375_pinctrl);
		ocp81375_pinctrl = NULL;
		ocp81375_hwen_high = NULL;
		ocp81375_hwen_low = NULL;
	}
	if (chip) {
		client->dev.platform_data = NULL;
		kfree(chip);
	}
	return 0;
}

static const struct i2c_device_id ocp81375_i2c_id[] = {
	{OCP81375_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id ocp81375_i2c_of_match[] = {
	{.compatible = OCP81375_DTNAME_I2C},
	{},
};
#endif

static struct i2c_driver ocp81375_i2c_driver = {
	.driver = {
		.name = OCP81375_NAME,
#ifdef CONFIG_OF
		.of_match_table = ocp81375_i2c_of_match,
#endif
	},
	.probe = ocp81375_i2c_probe,
	.remove = ocp81375_i2c_remove,
	.id_table = ocp81375_i2c_id,
};

static int ocp81375_probe(struct platform_device *pdev)
{
	struct ocp81375_platform_data *pdata = dev_get_platdata(&pdev->dev);

	ocp81375_pdev = pdev;
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		pdev->dev.platform_data = pdata;
	}
	ocp81375_pdata = pdata;

	if (i2c_add_driver(&ocp81375_i2c_driver)) {
		pr_err("Failed to add i2c driver.\n");
		return -1;
	}

	return 0;
}

static int ocp81375_remove(struct platform_device *pdev)
{
	i2c_del_driver(&ocp81375_i2c_driver);
	ocp81375_pdev = NULL;
	pdev->dev.platform_data = NULL;
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ocp81375_of_match[] = {
	{.compatible = OCP81375_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, ocp81375_of_match);
#endif

static struct platform_driver ocp81375_platform_driver = {
	.probe = ocp81375_probe,
	.remove = ocp81375_remove,
	.driver = {
		.name = OCP81375_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ocp81375_of_match,
#endif
	},
};

static int __init flashlight_ocp81375_init(void)
{
	return platform_driver_register(&ocp81375_platform_driver);
}

static void __exit flashlight_ocp81375_exit(void)
{
	platform_driver_unregister(&ocp81375_platform_driver);
}

module_init(flashlight_ocp81375_init);
module_exit(flashlight_ocp81375_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Infinix / Transsion Stock Restore");
MODULE_DESCRIPTION("MTK Flashlight OCP81375 Driver");
