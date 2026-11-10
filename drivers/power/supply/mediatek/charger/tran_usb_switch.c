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

struct tran_usb_switch_data {
	struct device *dev;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_init1;
	struct pinctrl_state *pins_high1;
	struct pinctrl_state *pins_low1;
	struct pinctrl_state *pins_init2;
	struct pinctrl_state *pins_high2;
	struct pinctrl_state *pins_low2;
	struct power_supply *usb_switch_psy;
	struct power_supply_desc psy_desc;
	struct notifier_block nb;
	int rfc_dcp_ta;
	int cur_ctl;
};

static struct tran_usb_switch_data *g_switch_dev;

struct tran_usb_switch_data *get_switch_device(void)
{
	return g_switch_dev;
}
EXPORT_SYMBOL(get_switch_device);

int tran_usb_switch_ctl(int en)
{
	if (!g_switch_dev || !g_switch_dev->pinctrl)
		return -ENODEV;

	if (g_switch_dev->cur_ctl == en)
		return 0;

	pr_info("DV2_SWITCH:usb_switch_ctl=%d\n", en);
	if (en) {
		if (!IS_ERR_OR_NULL(g_switch_dev->pins_high1))
			pinctrl_select_state(g_switch_dev->pinctrl, g_switch_dev->pins_high1);
		if (!IS_ERR_OR_NULL(g_switch_dev->pins_high2))
			pinctrl_select_state(g_switch_dev->pinctrl, g_switch_dev->pins_high2);
		g_switch_dev->cur_ctl = 1;
	} else {
		if (!IS_ERR_OR_NULL(g_switch_dev->pins_low1))
			pinctrl_select_state(g_switch_dev->pinctrl, g_switch_dev->pins_low1);
		if (!IS_ERR_OR_NULL(g_switch_dev->pins_low2))
			pinctrl_select_state(g_switch_dev->pinctrl, g_switch_dev->pins_low2);
		g_switch_dev->cur_ctl = 0;
	}
	return 0;
}
EXPORT_SYMBOL(tran_usb_switch_ctl);

int tran_usb_switch_ctl_check(int chr_type, int en, int rfc_dcp_ta)
{
	pr_info("DV2_SWITCH:tran_usb_switch_ctl_check:chr_type=%d en=%d rfc_dcp_ta=%d\n",
		chr_type, en, rfc_dcp_ta);
	if (rfc_dcp_ta)
		return tran_usb_switch_ctl(1);
	else
		return tran_usb_switch_ctl(0);
}
EXPORT_SYMBOL(tran_usb_switch_ctl_check);

int dcp_get_ta_status(void)
{
	return g_switch_dev ? g_switch_dev->rfc_dcp_ta : 0;
}
EXPORT_SYMBOL(dcp_get_ta_status);

void dcp_reset_rfc(void)
{
	if (g_switch_dev)
		g_switch_dev->rfc_dcp_ta = 0;
}
EXPORT_SYMBOL(dcp_reset_rfc);

static int usb_switch_psy_notifier_cb(struct notifier_block *nb, unsigned long event, void *v)
{
	struct power_supply *psy = (struct power_supply *)v;
	union power_supply_propval val;

	if (event != PSY_EVENT_PROP_CHANGED || !psy || !g_switch_dev)
		return NOTIFY_OK;

	/* ONLY handle "charger" power supply to prevent battery fuel-gauge events from resetting rfc_dcp_ta */
	if (strcmp(psy->desc->name, "charger") != 0)
		return NOTIFY_OK;

	{
		int chr_type = 1;
		int chg_in = 1;

		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val) == 0)
			chg_in = val.intval;
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CHARGE_TYPE, &val) == 0)
			chr_type = val.intval;

		if (chg_in && (chr_type == 4 || chr_type == 3 || chr_type == 5 || chr_type == 6 || chr_type == 2)) {
			g_switch_dev->rfc_dcp_ta = 1;
		} else if (!chg_in || chr_type == 1 || chr_type == 0) {
			g_switch_dev->rfc_dcp_ta = 0;
		}

		pr_info("DV2_SWITCH:usb_switch_psy_notifier_cb:chrtype=%d chg_in=%d rfc_dcp_ta=%d\n",
			chr_type, chg_in, g_switch_dev->rfc_dcp_ta);
		tran_usb_switch_ctl_check(chr_type, chg_in, g_switch_dev->rfc_dcp_ta);
	}
	return NOTIFY_OK;
}

static enum power_supply_property usb_switch_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int usb_switch_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = g_switch_dev ? g_switch_dev->cur_ctl : 0;
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
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		tran_usb_switch_ctl(val->intval);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tran_usb_switch_parse_dt(struct tran_usb_switch_data *data)
{
	struct pinctrl *p = data->pinctrl;

	if (IS_ERR_OR_NULL(p))
		return -EINVAL;

	data->pins_init1 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_init1");
	data->pins_high1 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_high1");
	data->pins_low1  = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_low1");
	data->pins_init2 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_init2");
	data->pins_high2 = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_high2");
	data->pins_low2  = pinctrl_lookup_state(p, "dv2_usb_switch_gpio_low2");

	/* Connect USB lines to MUSB controller by default */
	if (!IS_ERR_OR_NULL(data->pins_low1))
		pinctrl_select_state(p, data->pins_low1);
	if (!IS_ERR_OR_NULL(data->pins_low2))
		pinctrl_select_state(p, data->pins_low2);

	return 0;
}

static int tran_usb_switch_probe(struct platform_device *pdev)
{
	struct tran_usb_switch_data *data;
	struct power_supply_config psy_cfg = {};

	pr_info("DV2_SWITCH:tran_usb_switch_probe\n");
	pr_info("DV2_SWITCH:****tran_dv2_adaptor_gpio_init:end Init Drive dv2 adaptor!!!!!\n");

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	data->cur_ctl = 0;
	data->rfc_dcp_ta = 0;

	data->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(data->pinctrl)) {
		pr_warn("DV2_SWITCH: pinctrl get failed: %ld\n", PTR_ERR(data->pinctrl));
	} else {
		tran_usb_switch_parse_dt(data);
	}

	data->psy_desc.name = "usb_switch";
	data->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	data->psy_desc.properties = usb_switch_props;
	data->psy_desc.num_properties = ARRAY_SIZE(usb_switch_props);
	data->psy_desc.get_property = usb_switch_get_property;
	data->psy_desc.set_property = usb_switch_set_property;

	psy_cfg.drv_data = data;
	data->usb_switch_psy = power_supply_register(&pdev->dev, &data->psy_desc, &psy_cfg);
	if (IS_ERR(data->usb_switch_psy)) {
		pr_warn("DV2_SWITCH: usb_switch_psy_register fail: %ld\n", PTR_ERR(data->usb_switch_psy));
	} else {
		pr_info("DV2_SWITCH:usb_switch_psy_register: successfully\n");
	}

	g_switch_dev = data;

	data->nb.notifier_call = usb_switch_psy_notifier_cb;
	power_supply_reg_notifier(&data->nb);

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
