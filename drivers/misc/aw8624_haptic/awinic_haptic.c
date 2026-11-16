// SPDX-License-Identifier: GPL-2.0
/*
 * Unified Awinic Haptic Frontend (AW8624 / AW86223 / AW86224 / AW86225)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>

#include "awinic_haptic.h"

#define AWINIC_I2C_NAME			"awinic_haptic"
#define AWINIC_READ_CHIPID_RETRIES	5
#define AWINIC_REG_ID			0x00
#define AWINIC_AW8622X_EFRD9		0x64

static void awinic_hw_reset(int reset_gpio)
{
	if (!gpio_is_valid(reset_gpio))
		return;

	gpio_set_value_cansleep(reset_gpio, 0);
	usleep_range(1000, 2000);
	gpio_set_value_cansleep(reset_gpio, 1);
	usleep_range(3500, 4000);
}

static void awinic_haptic_softreset(struct i2c_client *i2c)
{
	i2c_smbus_write_byte_data(i2c, AWINIC_REG_ID, 0xAA);
	usleep_range(2000, 2500);
}

static int awinic_read_chipid(struct i2c_client *i2c, int reset_gpio,
			      unsigned char *chip_name)
{
	int cnt, ret;
	unsigned char reg = 0, ef = 0;

	for (cnt = 0; cnt < AWINIC_READ_CHIPID_RETRIES; cnt++) {
		awinic_hw_reset(reset_gpio);

		ret = i2c_smbus_read_byte_data(i2c, AWINIC_REG_ID);
		if (ret < 0) {
			dev_err(&i2c->dev, "awinic: read ID err=%d\n", ret);
			usleep_range(2000, 2500);
			continue;
		}
		reg = (unsigned char)ret;

		if (reg == 0x24) {
			*chip_name = AW8624_CHIP;
			dev_info(&i2c->dev, "awinic: detected AW8624 (0x24)\n");
			awinic_haptic_softreset(i2c);
			return 0;
		}

		if (reg == 0x00) {
			ret = i2c_smbus_read_byte_data(i2c, AWINIC_AW8622X_EFRD9);
			if (ret >= 0) {
				ef = (unsigned char)ret;
				if ((ef & 0x41) == 0x00) {
					*chip_name = AW86224_5_CHIP;
					dev_info(&i2c->dev,
						 "awinic: detected AW86224_5 (ef=0x%02x)\n",
						 ef);
					awinic_haptic_softreset(i2c);
					return 0;
				}
				if ((ef & 0x41) == 0x01) {
					*chip_name = AW86223_CHIP;
					dev_info(&i2c->dev,
						 "awinic: detected AW86223 (ef=0x%02x)\n",
						 ef);
					awinic_haptic_softreset(i2c);
					return 0;
				}
			}
		}

		usleep_range(2000, 2500);
	}

	dev_err(&i2c->dev, "awinic: unknown haptic chip (id=0x%02x)\n", reg);
	return -ENODEV;
}

static int awinic_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device_node *np = i2c->dev.of_node;
	int reset_gpio = -1;
	int irq_gpio = -1;
	unsigned int real_addr = 0;
	unsigned char chip_name = AW_CHIP_NULL;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(&i2c->dev, "awinic: check_functionality failed\n");
		return -EIO;
	}

	if (np) {
		reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
		irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
		if (of_property_read_u32(np, "reg-real-addr", &real_addr) == 0)
			i2c->addr = (u16)real_addr;
		else if (of_property_read_u32(np, "aw8622x_i2c_addr", &real_addr) == 0)
			i2c->addr = (u16)real_addr;
	}

	if (gpio_is_valid(reset_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, reset_gpio,
					    GPIOF_OUT_INIT_LOW, "awinic_rst");
		if (ret) {
			dev_err(&i2c->dev, "awinic: rst gpio request failed (%d)\n",
				ret);
			return ret;
		}
	} else {
		dev_err(&i2c->dev, "awinic: no valid reset-gpio\n");
		return -EINVAL;
	}

	ret = awinic_read_chipid(i2c, reset_gpio, &chip_name);
	if (ret < 0)
		return ret;

	if (chip_name == AW86223_CHIP || chip_name == AW86224_5_CHIP)
		return aw8622x_probe_dev(i2c, reset_gpio, irq_gpio, chip_name);

	if (chip_name == AW8624_CHIP)
		return aw8624_probe_dev(i2c, id, reset_gpio, irq_gpio);

	return -ENODEV;
}

static int awinic_i2c_remove(struct i2c_client *i2c)
{
	void *drv = i2c_get_clientdata(i2c);

	if (!drv)
		return 0;

	if (*(u32 *)drv == AW8622X_STRUCT_MAGIC)
		return aw8622x_remove_dev(i2c);

	return aw8624_remove_dev(i2c);
}

#ifdef CONFIG_PM_SLEEP
static int awinic_suspend(struct device *dev)
{
	void *drv = dev_get_drvdata(dev);

	if (!drv)
		return 0;

	if (*(u32 *)drv == AW8622X_STRUCT_MAGIC)
		return aw8622x_suspend_dev(dev);

	return aw8624_suspend_dev(dev);
}

static int awinic_resume(struct device *dev)
{
	void *drv = dev_get_drvdata(dev);

	if (!drv)
		return 0;

	if (*(u32 *)drv == AW8622X_STRUCT_MAGIC)
		return aw8622x_resume_dev(dev);

	return aw8624_resume_dev(dev);
}

static const struct dev_pm_ops awinic_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(awinic_suspend, awinic_resume)
};
#endif

static const struct i2c_device_id awinic_i2c_id[] = {
	{ AWINIC_I2C_NAME, 0 },
	{ "aw8624_haptic", 0 },
	{ "aw8622x_haptic", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, awinic_i2c_id);

static const struct of_device_id awinic_dt_match[] = {
	{ .compatible = "awinic,awinic_haptic" },
	{ .compatible = "awinic,aw8624_haptic" },
	{ .compatible = "awinic,aw8622x_haptic" },
	{ }
};

static struct i2c_driver awinic_i2c_driver = {
	.driver = {
		.name = AWINIC_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(awinic_dt_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &awinic_pm_ops,
#endif
	},
	.probe = awinic_i2c_probe,
	.remove = awinic_i2c_remove,
	.id_table = awinic_i2c_id,
};

static int __init awinic_i2c_init(void)
{
	int ret;

	pr_info("awinic_haptic: unified driver init\n");
	ret = i2c_add_driver(&awinic_i2c_driver);
	if (ret)
		pr_err("awinic_haptic: fail to add i2c driver (%d)\n", ret);
	return ret;
}
late_initcall(awinic_i2c_init);

static void __exit awinic_i2c_exit(void)
{
	i2c_del_driver(&awinic_i2c_driver);
}
module_exit(awinic_i2c_exit);

MODULE_DESCRIPTION("Unified Awinic Haptic Driver (AW8624 / AW8622x)");
MODULE_LICENSE("GPL v2");
