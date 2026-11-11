// SPDX-License-Identifier: GPL-2.0
/*
 * Transsion DV2 USB Switch & RFC Adapter Controller Driver
 *
 * Reconstructed authentically for Infinix Note 11 Pro / Note 11S (X698 / MT6781)
 * Hardware pin control:
 *   - dv2_low1 / dv2_high1: GPIO 173 (0xad)
 *   - dv2_low2 / dv2_high2: GPIO 171 (0xab)
 * Low = Standard USB (Host PC / MUSB Controller active)
 * High = Fast Charge / RFC Adapter mode
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/power_supply.h>
#include <linux/notifier.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_boot.h>
#include "mtk_charger_intf.h"

struct tran_usb_switch_data {
	struct device *dev;
	struct power_supply *usb_switch_psy;
	struct power_supply_desc psy_desc;
	struct power_supply *chg_psy;
	struct notifier_block nb;
	bool chg_in;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_low1;
	struct pinctrl_state *pins_high1;
	struct pinctrl_state *pins_low2;
	struct pinctrl_state *pins_high2;
	struct pinctrl_state *pins_sda8;
	struct pinctrl_state *pins_scl8;
	int scl8_gpio;
	int sda8_gpio;
	bool rfc_dcp_ta;
	int usb_switch_online;
	int usb_switch_init_flag;
};

static struct tran_usb_switch_data *g_switch_dev;

struct tran_usb_switch_data *get_switch_device(void)
{
	struct power_supply *psy = power_supply_get_by_name("usb_switch");

	if (!IS_ERR_OR_NULL(psy)) {
		struct tran_usb_switch_data *data = power_supply_get_drvdata(psy);
		power_supply_put(psy);
		if (data)
			return data;
	}
	return g_switch_dev;
}
EXPORT_SYMBOL(get_switch_device);

static void dv2_select_usb_path(struct tran_usb_switch_data *data)
{
	if (!data || IS_ERR_OR_NULL(data->pinctrl))
		return;
	if (!IS_ERR_OR_NULL(data->pins_low1))
		pinctrl_select_state(data->pinctrl, data->pins_low1);
	if (!IS_ERR_OR_NULL(data->pins_low2))
		pinctrl_select_state(data->pinctrl, data->pins_low2);
	data->rfc_dcp_ta = false;
}

static int dv2_is_rfc_adapter(struct tran_usb_switch_data *data)
{
	int sda_val0, sda_val1;
	int ret;

	if (!data || !gpio_is_valid(data->sda8_gpio) ||
	    !gpio_is_valid(data->scl8_gpio))
		return -ENODEV;

	ret = gpio_request(data->sda8_gpio, "dcp_detect");
	if (ret < 0) {
		pr_err("%s: gpio_request sda8(%d) failed: %d\n",
			__func__, data->sda8_gpio, ret);
		return -EFAULT;
	}
	ret = gpio_request(data->scl8_gpio, "dcp_detect");
	if (ret < 0) {
		pr_err("%s: gpio_request scl8(%d) failed: %d\n",
			__func__, data->scl8_gpio, ret);
		gpio_free(data->sda8_gpio);
		return -EFAULT;
	}

	ret = gpio_direction_input(data->sda8_gpio);
	if (ret < 0) {
		pr_err("%s: sda8 direction_input failed: %d\n", __func__, ret);
		goto out_err;
	}
	ret = gpio_direction_output(data->scl8_gpio, 0);
	if (ret < 0) {
		pr_err("%s: scl8 direction_output failed: %d\n", __func__, ret);
		goto out_err;
	}
	gpio_set_value(data->scl8_gpio, 0);
	mdelay(15);
	sda_val0 = gpio_get_value(data->sda8_gpio);
	pr_info("%s: sda_val0=%d\n", __func__, sda_val0);

	gpio_set_value(data->scl8_gpio, 1);
	mdelay(15);
	sda_val1 = gpio_get_value(data->sda8_gpio);
	pr_info("%s: sda_val1=%d\n", __func__, sda_val1);

	gpio_free(data->sda8_gpio);
	gpio_free(data->scl8_gpio);
	return (sda_val0 != 0) || (sda_val1 == 0);

out_err:
	gpio_free(data->sda8_gpio);
	gpio_free(data->scl8_gpio);
	return -EFAULT;
}

static void dv2_reset_rfc_gpio(struct tran_usb_switch_data *data)
{
	if (!gpio_is_valid(data->sda8_gpio) || !gpio_is_valid(data->scl8_gpio))
		return;
	if (gpio_request(data->sda8_gpio, "dcp_detect") < 0)
		return;
	if (gpio_request(data->scl8_gpio, "dcp_detect") < 0) {
		gpio_free(data->sda8_gpio);
		return;
	}
	gpio_direction_output(data->sda8_gpio, 0);
	gpio_direction_output(data->scl8_gpio, 0);
	mdelay(20);
	gpio_direction_output(data->sda8_gpio, 1);
	gpio_direction_output(data->scl8_gpio, 1);
	gpio_free(data->sda8_gpio);
	gpio_free(data->scl8_gpio);
	mdelay(20);
}

int tran_usb_switch_ctl(struct tran_usb_switch_data *data, int en)
{
	struct charger_manager *cm;
	int is_rfc = 0;
	int retry;

	pr_info("DV2_SWITCH:usb_switch_ctl=%d\n", en & 1);
	if (!data || IS_ERR_OR_NULL(data->pinctrl))
		return -ENODEV;

	if (!(en & 1)) {
		dv2_select_usb_path(data);
		return 0;
	}

	if (!IS_ERR_OR_NULL(data->pins_high1))
		pinctrl_select_state(data->pinctrl, data->pins_high1);
	if (!IS_ERR_OR_NULL(data->pins_high2))
		pinctrl_select_state(data->pinctrl, data->pins_high2);
	mdelay(20);

	is_rfc = dv2_is_rfc_adapter(data);
	for (retry = 1; is_rfc <= 0 && retry <= 1; retry++) {
		dv2_reset_rfc_gpio(data);
		is_rfc = dv2_is_rfc_adapter(data);
		pr_info("DV2_SWITCH: retry=%d is_rfc=%d\n", retry, is_rfc);
	}

	if (is_rfc > 0) {
		data->rfc_dcp_ta = true;
		if (!IS_ERR_OR_NULL(data->pins_sda8))
			pinctrl_select_state(data->pinctrl, data->pins_sda8);
		if (!IS_ERR_OR_NULL(data->pins_scl8))
			pinctrl_select_state(data->pinctrl, data->pins_scl8);
		pr_info("DV2_SWITCH: rfc_dcp_detect=%d\n", data->rfc_dcp_ta);
	} else {
		dv2_select_usb_path(data);
	}

	cm = tran_get_pinfo();
	if (cm) {
		cm->rfc_dcp_ta = true;
		_wake_up_charger(cm);
	}
	return 0;
}
EXPORT_SYMBOL(tran_usb_switch_ctl);

int tran_usb_switch_ctl_check(struct tran_usb_switch_data *data,
			      int chr_type, int en)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != NORMAL_BOOT &&
	    boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT &&
	    boot_mode != LOW_POWER_OFF_CHARGING_BOOT)
		return 0;

	if (!data)
		return -ENODEV;

	pr_info("DV2_SWITCH:%s:chr_type=%d en=%d rfc_dcp_ta=%d\n",
		__func__, chr_type, en, data->rfc_dcp_ta);

	if (chr_type == STANDARD_CHARGER) {
		tran_usb_switch_ctl(data, en != 0);
		if (data->rfc_dcp_ta)
			return 0;
	}

	dv2_select_usb_path(data);
	return 0;
}
EXPORT_SYMBOL(tran_usb_switch_ctl_check);

int dcp_get_ta_status(void)
{
	struct tran_usb_switch_data *data = get_switch_device();

	if (!data)
		return 0;
	pr_info("DV2_SWITCH:%s:rfc_dcp_ta=%d\n", __func__, data->rfc_dcp_ta);
	return data->rfc_dcp_ta ? 1 : 0;
}
EXPORT_SYMBOL(dcp_get_ta_status);

void dcp_reset_rfc(void)
{
	struct tran_usb_switch_data *data = get_switch_device();

	if (!data)
		return;
	pr_info("DV2_SWITCH:%s:rfc_dcp_ta=%d\n", __func__, data->rfc_dcp_ta);
	dv2_select_usb_path(data);
}
EXPORT_SYMBOL(dcp_reset_rfc);

static int usb_switch_psy_notifier_cb(struct notifier_block *nb,
				      unsigned long event, void *v)
{
	struct tran_usb_switch_data *data =
		container_of(nb, struct tran_usb_switch_data, nb);
	struct power_supply *psy = (struct power_supply *)v;
	union power_supply_propval val;
	int chr_type;
	int ret;

	if (event != PSY_EVENT_PROP_CHANGED || !data)
		return NOTIFY_DONE;

	if (!psy || !psy->desc || strcmp(psy->desc->name, "charger") != 0)
		return NOTIFY_DONE;

	data->chg_psy = power_supply_get_by_name("charger");
	if (IS_ERR_OR_NULL(data->chg_psy))
		return NOTIFY_DONE;

	ret = power_supply_get_property(data->chg_psy,
		POWER_SUPPLY_PROP_CHARGE_TYPE, &val);
	if (ret < 0) {
		power_supply_put(data->chg_psy);
		return NOTIFY_DONE;
	}

	chr_type = val.intval;
	pr_info("DV2_SWITCH:%s:chrtype=%d chg_in=%d rfc_dcp_ta=%d\n",
		__func__, chr_type, data->chg_in, data->rfc_dcp_ta);

	if (chr_type != CHARGER_UNKNOWN) {
		if (!data->chg_in) {
			data->chg_in = true;
			tran_usb_switch_ctl_check(data, chr_type, 1);
			if (data->rfc_dcp_ta && !IS_ERR_OR_NULL(data->usb_switch_psy)) {
				val.intval = 1;
				power_supply_set_property(data->usb_switch_psy,
					POWER_SUPPLY_PROP_ONLINE, &val);
			}
		}
		if (chr_type == NONSTANDARD_CHARGER)
			data->chg_in = false;
	} else if (data->chg_in) {
		data->chg_in = false;
		tran_usb_switch_ctl_check(data, 0, 0);
		if (!data->rfc_dcp_ta && !IS_ERR_OR_NULL(data->usb_switch_psy)) {
			val.intval = 0;
			power_supply_set_property(data->usb_switch_psy,
				POWER_SUPPLY_PROP_ONLINE, &val);
		}
	}

	power_supply_put(data->chg_psy);
	return NOTIFY_OK;
}

static enum power_supply_property usb_switch_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int usb_switch_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct tran_usb_switch_data *data = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = data ? data->usb_switch_online : 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int usb_switch_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct tran_usb_switch_data *data = power_supply_get_drvdata(psy);

	if (!data)
		return -EINVAL;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		data->usb_switch_online = val->intval;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tran_usb_switch_parse_dt(struct tran_usb_switch_data *data)
{
	int boot_mode = get_boot_mode();
	struct pinctrl *p;
	struct device_node *np;

	if (boot_mode != NORMAL_BOOT &&
	    boot_mode != KERNEL_POWER_OFF_CHARGING_BOOT &&
	    boot_mode != LOW_POWER_OFF_CHARGING_BOOT)
		return 0;

	p = devm_pinctrl_get(data->dev);
	data->pinctrl = p;
	if (IS_ERR_OR_NULL(p)) {
		pr_err("DV2_SWITCH:%s: devm_pinctrl_get failed\n", __func__);
		return -EINVAL;
	}

	data->pins_low1  = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_low1");
	data->pins_high1 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_high1");
	data->pins_low2  = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_low2");
	data->pins_high2 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_high2");
	data->pins_sda8  = pinctrl_lookup_state(p, "dv2_rfc_sda8");
	data->pins_scl8  = pinctrl_lookup_state(p, "dv2_rfc_scl8");

	if (IS_ERR(data->pins_low1) || IS_ERR(data->pins_high1) ||
	    IS_ERR(data->pins_low2) || IS_ERR(data->pins_high2) ||
	    IS_ERR(data->pins_sda8) || IS_ERR(data->pins_scl8)) {
		pr_err("DV2_SWITCH:%s: pinctrl lookup failed\n", __func__);
		return -EINVAL;
	}

	np = data->dev->of_node;
	data->scl8_gpio = of_get_named_gpio(np, "rfc,scl8_gpio", 0);
	data->sda8_gpio = of_get_named_gpio(np, "rfc,sda8_gpio", 0);
	pr_info("DV2_SWITCH:%s: scl8_gpio=%d sda8_gpio=%d\n",
		__func__, data->scl8_gpio, data->sda8_gpio);

	dv2_select_usb_path(data);
	return 0;
}

static int tran_usb_switch_probe(struct platform_device *pdev)
{
	struct tran_usb_switch_data *data;
	struct power_supply_config psy_cfg = {};
	int ret;

	pr_info("DV2_SWITCH:tran_usb_switch_probe\n");

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	data->chg_in = false;
	data->rfc_dcp_ta = false;
	data->usb_switch_online = 0;
	data->usb_switch_init_flag = 0;
	data->scl8_gpio = -EINVAL;
	data->sda8_gpio = -EINVAL;

	ret = tran_usb_switch_parse_dt(data);
	if (ret < 0)
		pr_warn("DV2_SWITCH: tran_usb_switch_parse_dt failed: %d\n", ret);

	data->psy_desc.name = "usb_switch";
	data->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	data->psy_desc.properties = usb_switch_props;
	data->psy_desc.num_properties = ARRAY_SIZE(usb_switch_props);
	data->psy_desc.get_property = usb_switch_get_property;
	data->psy_desc.set_property = usb_switch_set_property;

	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.drv_data = data;
	data->usb_switch_psy = power_supply_register(&pdev->dev, &data->psy_desc, &psy_cfg);
	if (IS_ERR(data->usb_switch_psy)) {
		pr_warn("DV2_SWITCH: usb_switch_psy_register fail: %ld\n",
			PTR_ERR(data->usb_switch_psy));
		data->usb_switch_psy = NULL;
	} else {
		pr_info("DV2_SWITCH:usb_switch_psy_register: successfully\n");
	}

	g_switch_dev = data;

	data->nb.notifier_call = usb_switch_psy_notifier_cb;
	power_supply_reg_notifier(&data->nb);

	data->chg_psy = power_supply_get_by_name("charger");
	if (IS_ERR_OR_NULL(data->chg_psy))
		pr_info("DV2_SWITCH: usb_switch_get_chg_psy pending\n");
	else
		power_supply_put(data->chg_psy);

	data->usb_switch_init_flag = 1;
	platform_set_drvdata(pdev, data);
	pr_info("DV2_SWITCH:tran_usb_switch_probe done\n");
	return 0;
}

static int tran_usb_switch_remove(struct platform_device *pdev)
{
	struct tran_usb_switch_data *data = platform_get_drvdata(pdev);

	if (data) {
		power_supply_unreg_notifier(&data->nb);
		if (!IS_ERR_OR_NULL(data->usb_switch_psy))
			power_supply_unregister(data->usb_switch_psy);
	}
	g_switch_dev = NULL;
	return 0;
}

static const struct of_device_id tran_usb_switch_of_match[] = {
	{ .compatible = "tran,usb_switch", },
	{ }
};
MODULE_DEVICE_TABLE(of, tran_usb_switch_of_match);

static struct platform_driver tran_usb_switch_driver = {
	.probe  = tran_usb_switch_probe,
	.remove = tran_usb_switch_remove,
	.driver = {
		.name = "tran_usb_switch",
		.of_match_table = tran_usb_switch_of_match,
	},
};

static int __init tran_usb_switch_driver_init(void)
{
	int ret = platform_driver_register(&tran_usb_switch_driver);
	pr_info("DV2_SWITCH:[tran_usb_switch_driver_init] done\n");
	return ret;
}

static void __exit tran_usb_switch_driver_exit(void)
{
	platform_driver_unregister(&tran_usb_switch_driver);
}

module_init(tran_usb_switch_driver_init);
module_exit(tran_usb_switch_driver_exit);

MODULE_AUTHOR("Transsion");
MODULE_DESCRIPTION("Transsion DV2 USB Switch Controller Driver");
MODULE_LICENSE("GPL v2");
