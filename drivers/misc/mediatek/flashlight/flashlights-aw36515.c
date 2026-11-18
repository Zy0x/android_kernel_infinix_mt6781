/*
 * Copyright (C) 2015 MediaTek Inc.
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

#define AW36515_NAME "flashlights-aw36515"

#ifndef AW36515_DTNAME
#define AW36515_DTNAME "mediatek,flashlights_aw36515"
#endif
#ifndef AW36515_DTNAME_I2C
#define AW36515_DTNAME_I2C "mediatek,strobe_main"
#endif

#define AW36515_REG_ENABLE          (0x01)
#define AW36515_MASK_ENABLE_LED1    (0x01)
#define AW36515_MASK_ENABLE_LED2    (0x02)
#define AW36515_DISABLE             (0x00)
#define AW36515_ENABLE_LED1         (0x01)
#define AW36515_ENABLE_LED1_TORCH   (0x09)
#define AW36515_ENABLE_LED1_FLASH   (0x0D)
#define AW36515_ENABLE_LED2         (0x02)
#define AW36515_ENABLE_LED2_TORCH   (0x0A)
#define AW36515_ENABLE_LED2_FLASH   (0x0E)

#define AW36515_REG_TORCH_LEVEL_LED1 (0x05)
#define AW36515_REG_FLASH_LEVEL_LED1 (0x03)
#define AW36515_REG_TORCH_LEVEL_LED2 (0x06)
#define AW36515_REG_FLASH_LEVEL_LED2 (0x04)
#define AW36515_DEVICE_ID            (0x0C)
#define AW36515_CHIP_ID_VAL          (0x02)

#define AW36515_REG_TIMING_CONF      (0x08)
#define AW36515_TORCH_RAMP_TIME      (0x00)
#define AW36515_FLASH_TIMEOUT        (0x0F)

#define AW36515_CHANNEL_NUM 2
#define AW36515_CHANNEL_CH1 0
#define AW36515_CHANNEL_CH2 1

#define AW36515_LEVEL_NUM 30
#define AW36515_LEVEL_TORCH 3
#define AW36515_HW_TIMEOUT 400 /* ms */

#define AW36515_PINCTRL_PIN_HWEN 0
#define AW36515_PINCTRL_PINSTATE_LOW 0
#define AW36515_PINCTRL_PINSTATE_HIGH 1
#define AW36515_PINCTRL_STATE_HWEN_HIGH "aw36515hwen_high"
#define AW36515_PINCTRL_STATE_HWEN_LOW  "aw36515hwen_low"

static DEFINE_MUTEX(aw36515_mutex);

static struct pinctrl *aw36515_pinctrl;
static struct pinctrl_state *aw36515_hwen_high;
static struct pinctrl_state *aw36515_hwen_low;

static struct work_struct aw36515_work_ch1;
static struct work_struct aw36515_work_ch2;

static struct hrtimer aw36515_timer_ch1;
static struct hrtimer aw36515_timer_ch2;
static unsigned int aw36515_timeout_ms[AW36515_CHANNEL_NUM];

struct aw36515_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};

struct aw36515_chip_data {
	struct i2c_client *client;
	struct aw36515_platform_data *pdata;
	struct mutex lock;
};

static struct platform_device *aw36515_pdev;
static struct aw36515_platform_data *aw36515_pdata;
static struct i2c_client *aw36515_i2c_client;

static int use_count;

static int aw36515_pinctrl_init(struct platform_device *pdev)
{
	int ret = 0;

	aw36515_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(aw36515_pinctrl)) {
		pr_err("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(aw36515_pinctrl);
		aw36515_pinctrl = NULL;
		return ret;
	}

	aw36515_hwen_high = pinctrl_lookup_state(
			aw36515_pinctrl, AW36515_PINCTRL_STATE_HWEN_HIGH);
	if (IS_ERR(aw36515_hwen_high)) {
		pr_err("Failed to init (%s)\n", AW36515_PINCTRL_STATE_HWEN_HIGH);
		ret = PTR_ERR(aw36515_hwen_high);
		aw36515_hwen_high = NULL;
	}

	aw36515_hwen_low = pinctrl_lookup_state(
			aw36515_pinctrl, AW36515_PINCTRL_STATE_HWEN_LOW);
	if (IS_ERR(aw36515_hwen_low)) {
		pr_err("Failed to init (%s)\n", AW36515_PINCTRL_STATE_HWEN_LOW);
		ret = PTR_ERR(aw36515_hwen_low);
		aw36515_hwen_low = NULL;
	}

	return ret;
}

static int aw36515_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (!aw36515_pinctrl) {
		pr_err("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case AW36515_PINCTRL_PIN_HWEN:
		if (state == AW36515_PINCTRL_PINSTATE_LOW &&
				aw36515_hwen_low && !IS_ERR(aw36515_hwen_low))
			pinctrl_select_state(aw36515_pinctrl, aw36515_hwen_low);
		else if (state == AW36515_PINCTRL_PINSTATE_HIGH &&
				aw36515_hwen_high && !IS_ERR(aw36515_hwen_high))
			pinctrl_select_state(aw36515_pinctrl, aw36515_hwen_high);
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

/* Transsion X698 verified 30-level table */
static const int aw36515_current[AW36515_LEVEL_NUM] = {
	12, 74, 136, 198, 260, 322, 384, 446, 508, 570,
	632, 694, 756, 818, 880, 942, 1004, 1066, 1128, 1190,
	1252, 1314, 1376, 1438, 1500, 1562, 1624, 1686, 1748, 1900
};

static unsigned char aw36515_torch_code(int cur)
{
	int code = (cur * 100 - 98) / 196;

	if (code < 0)
		code = 0;
	else if (code > 0x7f)
		code = 0x7f;
	return (unsigned char)code;
}

static unsigned char aw36515_flash_code(int cur)
{
	int code = (cur * 100 - 391) / 783;

	if (code < 0)
		code = 0;
	else if (code > 0x7f)
		code = 0x7f;
	return (unsigned char)code;
}

static unsigned char aw36515_reg_enable;
static int aw36515_level_ch1 = -1;
static int aw36515_level_ch2 = -1;

static int aw36515_is_torch(int level)
{
	if (level >= AW36515_LEVEL_TORCH)
		return -1;
	return 0;
}

static int aw36515_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= AW36515_LEVEL_NUM)
		level = AW36515_LEVEL_NUM - 1;
	return level;
}

static int aw36515_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	struct aw36515_chip_data *chip;

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

static int aw36515_read_reg(struct i2c_client *client, u8 reg)
{
	int val;
	struct aw36515_chip_data *chip;

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

static int aw36515_enable_ch1(void)
{
	unsigned char reg = AW36515_REG_ENABLE;
	unsigned char val;

	aw36515_reg_enable &= (~AW36515_ENABLE_LED1_FLASH);
	if (!aw36515_is_torch(aw36515_level_ch1))
		aw36515_reg_enable |= AW36515_ENABLE_LED1_TORCH;
	else
		aw36515_reg_enable |= AW36515_ENABLE_LED1_FLASH;

	val = aw36515_reg_enable;
	return aw36515_write_reg(aw36515_i2c_client, reg, val);
}

static int aw36515_enable_ch2(void)
{
	unsigned char reg = AW36515_REG_ENABLE;
	unsigned char val;

	aw36515_reg_enable &= (~AW36515_ENABLE_LED2_FLASH);
	if (!aw36515_is_torch(aw36515_level_ch2))
		aw36515_reg_enable |= AW36515_ENABLE_LED2_TORCH;
	else
		aw36515_reg_enable |= AW36515_ENABLE_LED2_FLASH;

	val = aw36515_reg_enable;
	return aw36515_write_reg(aw36515_i2c_client, reg, val);
}

static int aw36515_enable(int channel)
{
	if (channel == AW36515_CHANNEL_CH1)
		return aw36515_enable_ch1();
	else if (channel == AW36515_CHANNEL_CH2)
		return aw36515_enable_ch2();
	pr_err("Error channel %d\n", channel);
	return -1;
}

static int aw36515_disable_ch1(void)
{
	unsigned char reg = AW36515_REG_ENABLE;
	unsigned char val;

	if (aw36515_reg_enable & AW36515_MASK_ENABLE_LED2)
		aw36515_reg_enable &= (~AW36515_ENABLE_LED1);
	else
		aw36515_reg_enable &= (~AW36515_ENABLE_LED1_FLASH);

	val = aw36515_reg_enable;
	return aw36515_write_reg(aw36515_i2c_client, reg, val);
}

static int aw36515_disable_ch2(void)
{
	unsigned char reg = AW36515_REG_ENABLE;
	unsigned char val;

	if (aw36515_reg_enable & AW36515_MASK_ENABLE_LED1)
		aw36515_reg_enable &= (~AW36515_ENABLE_LED2);
	else
		aw36515_reg_enable &= (~AW36515_ENABLE_LED2_FLASH);

	val = aw36515_reg_enable;
	return aw36515_write_reg(aw36515_i2c_client, reg, val);
}

static int aw36515_disable(int channel)
{
	if (channel == AW36515_CHANNEL_CH1)
		return aw36515_disable_ch1();
	else if (channel == AW36515_CHANNEL_CH2)
		return aw36515_disable_ch2();
	pr_err("Error channel %d\n", channel);
	return -1;
}

static int aw36515_set_level_ch1(int level)
{
	int cur, ret = 0;

	level = aw36515_verify_level(level);
	aw36515_level_ch1 = level;
	cur = aw36515_current[level];

	if (!aw36515_is_torch(level))
		ret = aw36515_write_reg(aw36515_i2c_client,
				AW36515_REG_TORCH_LEVEL_LED1,
				aw36515_torch_code(cur));
	ret |= aw36515_write_reg(aw36515_i2c_client,
			AW36515_REG_FLASH_LEVEL_LED1,
			aw36515_flash_code(cur));

	return ret;
}

static int aw36515_set_level_ch2(int level)
{
	int cur, ret = 0;

	level = aw36515_verify_level(level);
	aw36515_level_ch2 = level;
	cur = aw36515_current[level];

	if (!aw36515_is_torch(level))
		ret = aw36515_write_reg(aw36515_i2c_client,
				AW36515_REG_TORCH_LEVEL_LED2,
				aw36515_torch_code(cur));
	ret |= aw36515_write_reg(aw36515_i2c_client,
			AW36515_REG_FLASH_LEVEL_LED2,
			aw36515_flash_code(cur));

	return ret;
}

static int aw36515_set_level(int channel, int level)
{
	if (channel == AW36515_CHANNEL_CH1)
		return aw36515_set_level_ch1(level);
	else if (channel == AW36515_CHANNEL_CH2)
		return aw36515_set_level_ch2(level);
	pr_err("Error channel %d\n", channel);
	return -1;
}

int aw36515_init(void)
{
	int ret;

	aw36515_pinctrl_set(
			AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_HIGH);
	msleep(5);

	aw36515_reg_enable = AW36515_DISABLE;
	ret = aw36515_write_reg(aw36515_i2c_client,
			AW36515_REG_ENABLE, AW36515_DISABLE);
	ret |= aw36515_write_reg(aw36515_i2c_client,
			AW36515_REG_TIMING_CONF,
			AW36515_TORCH_RAMP_TIME | AW36515_FLASH_TIMEOUT);

	return ret;
}

int aw36515_uninit(void)
{
	aw36515_disable(AW36515_CHANNEL_CH1);
	aw36515_disable(AW36515_CHANNEL_CH2);
	aw36515_pinctrl_set(
			AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_LOW);
	return 0;
}

static void aw36515_work_disable_ch1(struct work_struct *data)
{
	aw36515_disable_ch1();
}

static void aw36515_work_disable_ch2(struct work_struct *data)
{
	aw36515_disable_ch2();
}

static enum hrtimer_restart aw36515_timer_func_ch1(struct hrtimer *timer)
{
	schedule_work(&aw36515_work_ch1);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart aw36515_timer_func_ch2(struct hrtimer *timer)
{
	schedule_work(&aw36515_work_ch2);
	return HRTIMER_NORESTART;
}

static int aw36515_timer_start(int channel, ktime_t ktime)
{
	if (channel == AW36515_CHANNEL_CH1)
		hrtimer_start(&aw36515_timer_ch1, ktime, HRTIMER_MODE_REL);
	else if (channel == AW36515_CHANNEL_CH2)
		hrtimer_start(&aw36515_timer_ch2, ktime, HRTIMER_MODE_REL);
	else
		return -1;
	return 0;
}

static int aw36515_timer_cancel(int channel)
{
	if (channel == AW36515_CHANNEL_CH1)
		hrtimer_cancel(&aw36515_timer_ch1);
	else if (channel == AW36515_CHANNEL_CH2)
		hrtimer_cancel(&aw36515_timer_ch2);
	else
		return -1;
	return 0;
}

static int aw36515_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;
	ktime_t ktime;
	unsigned int s, ns;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	if (channel >= AW36515_CHANNEL_NUM || channel < 0) {
		pr_err("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		aw36515_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		aw36515_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		if (fl_arg->arg == 1) {
			if (aw36515_timeout_ms[channel]) {
				s = aw36515_timeout_ms[channel] / 1000;
				ns = aw36515_timeout_ms[channel] % 1000 * 1000000;
				ktime = ktime_set(s, ns);
				aw36515_timer_start(channel, ktime);
			}
			aw36515_enable(channel);
		} else {
			aw36515_disable(channel);
			aw36515_timer_cancel(channel);
		}
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		fl_arg->arg = AW36515_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		fl_arg->arg = AW36515_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = aw36515_verify_level(fl_arg->arg);
		fl_arg->arg = aw36515_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		fl_arg->arg = AW36515_HW_TIMEOUT;
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int aw36515_open(void)
{
	return 0;
}

static int aw36515_release(void)
{
	aw36515_uninit();
	return 0;
}

static int aw36515_set_driver(int set)
{
	int ret = 0;

	mutex_lock(&aw36515_mutex);
	if (set) {
		if (!use_count)
			ret = aw36515_init();
		use_count++;
	} else {
		use_count--;
		if (!use_count)
			ret = aw36515_uninit();
		if (use_count < 0)
			use_count = 0;
	}
	mutex_unlock(&aw36515_mutex);

	return ret;
}

static ssize_t aw36515_strobe_store(struct flashlight_arg arg)
{
	aw36515_set_driver(1);
	aw36515_set_level(arg.channel, arg.level);
	aw36515_timeout_ms[arg.channel] = 0;
	aw36515_enable(arg.channel);
	msleep(arg.dur);
	aw36515_disable(arg.channel);
	aw36515_set_driver(0);

	return 0;
}

static struct flashlight_operations aw36515_ops = {
	aw36515_open,
	aw36515_release,
	aw36515_ioctl,
	aw36515_strobe_store,
	aw36515_set_driver
};

static int aw36515_i2c_probe(
		struct i2c_client *client, const struct i2c_device_id *id)
{
	struct aw36515_chip_data *chip;
	int dev_id;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	if (!aw36515_pdev)
		return -EPROBE_DEFER;

	if (aw36515_pinctrl_init(aw36515_pdev)) {
		pr_err("Failed to init pinctrl.\n");
		if (aw36515_pinctrl) {
			devm_pinctrl_put(aw36515_pinctrl);
			aw36515_pinctrl = NULL;
			aw36515_hwen_high = NULL;
			aw36515_hwen_low = NULL;
		}
		return -ENODEV;
	}

	chip = kzalloc(sizeof(struct aw36515_chip_data), GFP_KERNEL);
	if (!chip) {
		if (aw36515_pinctrl) {
			devm_pinctrl_put(aw36515_pinctrl);
			aw36515_pinctrl = NULL;
			aw36515_hwen_high = NULL;
			aw36515_hwen_low = NULL;
		}
		return -ENOMEM;
	}

	chip->client = client;
	chip->pdata = aw36515_pdata;
	i2c_set_clientdata(client, chip);
	aw36515_i2c_client = client;
	mutex_init(&chip->lock);

	INIT_WORK(&aw36515_work_ch1, aw36515_work_disable_ch1);
	INIT_WORK(&aw36515_work_ch2, aw36515_work_disable_ch2);
	hrtimer_init(&aw36515_timer_ch1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw36515_timer_ch1.function = aw36515_timer_func_ch1;
	hrtimer_init(&aw36515_timer_ch2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw36515_timer_ch2.function = aw36515_timer_func_ch2;
	aw36515_timeout_ms[AW36515_CHANNEL_CH1] = 100;
	aw36515_timeout_ms[AW36515_CHANNEL_CH2] = 100;

	aw36515_pinctrl_set(AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_HIGH);
	mdelay(5);
	dev_id = aw36515_read_reg(aw36515_i2c_client, AW36515_DEVICE_ID);
	aw36515_pinctrl_set(AW36515_PINCTRL_PIN_HWEN, AW36515_PINCTRL_PINSTATE_LOW);

	pr_info("aw36515 read reg:0x%x val:0x%x\n", AW36515_DEVICE_ID, dev_id);
	if (dev_id < 0 || (dev_id & 0xffff) != AW36515_CHIP_ID_VAL) {
		pr_info("aw36515 DEVICE_ID mismatch (0x%x), skip.\n", dev_id);
		if (aw36515_pinctrl) {
			devm_pinctrl_put(aw36515_pinctrl);
			aw36515_pinctrl = NULL;
			aw36515_hwen_high = NULL;
			aw36515_hwen_low = NULL;
		}
		err = -ENODEV;
		goto err_free;
	}

	if (flashlight_dev_register(AW36515_NAME, &aw36515_ops)) {
		pr_err("aw36515 flashlight_dev_register fail.\n");
		if (aw36515_pinctrl) {
			devm_pinctrl_put(aw36515_pinctrl);
			aw36515_pinctrl = NULL;
			aw36515_hwen_high = NULL;
			aw36515_hwen_low = NULL;
		}
		err = -EFAULT;
		goto err_free;
	}

	use_count = 0;
	pr_info("aw36515 i2c probe success (chip id 0x%02x)\n", dev_id);
	return 0;

err_free:
	aw36515_i2c_client = NULL;
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	return err;
}

static int aw36515_i2c_remove(struct i2c_client *client)
{
	struct aw36515_chip_data *chip = i2c_get_clientdata(client);

	flashlight_dev_unregister(AW36515_NAME);
	flush_work(&aw36515_work_ch1);
	flush_work(&aw36515_work_ch2);
	aw36515_i2c_client = NULL;
	if (aw36515_pinctrl) {
		devm_pinctrl_put(aw36515_pinctrl);
		aw36515_pinctrl = NULL;
		aw36515_hwen_high = NULL;
		aw36515_hwen_low = NULL;
	}
	if (chip) {
		client->dev.platform_data = NULL;
		kfree(chip);
	}
	return 0;
}

static const struct i2c_device_id aw36515_i2c_id[] = {
	{AW36515_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id aw36515_i2c_of_match[] = {
	{.compatible = AW36515_DTNAME_I2C},
	{},
};
#endif

static struct i2c_driver aw36515_i2c_driver = {
	.driver = {
		.name = AW36515_NAME,
#ifdef CONFIG_OF
		.of_match_table = aw36515_i2c_of_match,
#endif
	},
	.probe = aw36515_i2c_probe,
	.remove = aw36515_i2c_remove,
	.id_table = aw36515_i2c_id,
};

static int aw36515_probe(struct platform_device *pdev)
{
	struct aw36515_platform_data *pdata = dev_get_platdata(&pdev->dev);

	aw36515_pdev = pdev;
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		pdev->dev.platform_data = pdata;
	}
	aw36515_pdata = pdata;

	if (i2c_add_driver(&aw36515_i2c_driver)) {
		pr_err("Failed to add i2c driver.\n");
		return -1;
	}

	return 0;
}

static int aw36515_remove(struct platform_device *pdev)
{
	i2c_del_driver(&aw36515_i2c_driver);
	aw36515_pdev = NULL;
	pdev->dev.platform_data = NULL;
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id aw36515_of_match[] = {
	{.compatible = AW36515_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, aw36515_of_match);
#endif

static struct platform_driver aw36515_platform_driver = {
	.probe = aw36515_probe,
	.remove = aw36515_remove,
	.driver = {
		.name = AW36515_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = aw36515_of_match,
#endif
	},
};

static int __init flashlight_aw36515_init(void)
{
	return platform_driver_register(&aw36515_platform_driver);
}

static void __exit flashlight_aw36515_exit(void)
{
	platform_driver_unregister(&aw36515_platform_driver);
}

module_init(flashlight_aw36515_init);
module_exit(flashlight_aw36515_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Infinix / Transsion Stock Restore");
MODULE_DESCRIPTION("MTK Flashlight AW36515 Driver");
