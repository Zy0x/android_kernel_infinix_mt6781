// SPDX-License-Identifier: GPL-2.0
/*
 * Proprietary Travel Adapter (TA) class framework
 * Reconstructed to match stock X698 behavior
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <mt-plat/prop_ta_class.h>

static struct class *prop_ta_class;

int prop_ta_device_get_power_limit(struct prop_ta_device *ta, u32 *pdp)
{
	if (ta && ta->ops && ta->ops->get_power_limit)
		return ta->ops->get_power_limit(ta, pdp);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_power_limit);

int prop_ta_device_get_verinfo(struct prop_ta_device *ta, u8 *buf, int len)
{
	if (ta && ta->ops && ta->ops->get_verinfo)
		return ta->ops->get_verinfo(ta, buf, len);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_verinfo);

int prop_ta_device_get_fwcode(struct prop_ta_device *ta, u8 *buf, int len)
{
	if (ta && ta->ops && ta->ops->get_fwcode)
		return ta->ops->get_fwcode(ta, buf, len);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_fwcode);

int prop_ta_device_get_mfinfo(struct prop_ta_device *ta, u8 *buf, int len)
{
	if (ta && ta->ops && ta->ops->get_mfinfo)
		return ta->ops->get_mfinfo(ta, buf, len);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_mfinfo);

int prop_ta_device_get_datecode(struct prop_ta_device *ta, u8 *buf, int len)
{
	if (ta && ta->ops && ta->ops->get_datecode)
		return ta->ops->get_datecode(ta, buf, len);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_datecode);

int prop_ta_device_get_min_voltage(struct prop_ta_device *ta, u32 *mV)
{
	if (ta && ta->ops && ta->ops->get_min_voltage)
		return ta->ops->get_min_voltage(ta, mV);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_min_voltage);

int prop_ta_device_get_max_voltage(struct prop_ta_device *ta, u32 *mV)
{
	if (ta && ta->ops && ta->ops->get_max_voltage)
		return ta->ops->get_max_voltage(ta, mV);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_max_voltage);

int prop_ta_device_get_min_current(struct prop_ta_device *ta, u32 *mA)
{
	if (ta && ta->ops && ta->ops->get_min_current)
		return ta->ops->get_min_current(ta, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_min_current);

int prop_ta_device_get_max_current(struct prop_ta_device *ta, u32 *mA)
{
	if (ta && ta->ops && ta->ops->get_max_current)
		return ta->ops->get_max_current(ta, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_max_current);

int prop_ta_device_get_output_voltage(struct prop_ta_device *ta, u32 *mV)
{
	if (ta && ta->ops && ta->ops->get_output_voltage)
		return ta->ops->get_output_voltage(ta, mV);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_output_voltage);

int prop_ta_device_get_output_current(struct prop_ta_device *ta, u32 *mA)
{
	if (ta && ta->ops && ta->ops->get_output_current)
		return ta->ops->get_output_current(ta, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_output_current);

int prop_ta_device_get_status(struct prop_ta_device *ta, u32 *status)
{
	if (ta && ta->ops && ta->ops->get_status)
		return ta->ops->get_status(ta, status);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_status);

int prop_ta_device_get_temp1(struct prop_ta_device *ta, int *temp)
{
	if (ta && ta->ops && ta->ops->get_temp1)
		return ta->ops->get_temp1(ta, temp);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_temp1);

int prop_ta_device_get_temp2(struct prop_ta_device *ta, int *temp)
{
	if (ta && ta->ops && ta->ops->get_temp2)
		return ta->ops->get_temp2(ta, temp);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_temp2);

int prop_ta_device_set_output_control(struct prop_ta_device *ta, u8 val)
{
	if (ta && ta->ops && ta->ops->set_output_control)
		return ta->ops->set_output_control(ta, val);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_output_control);

int prop_ta_device_set_mode(struct prop_ta_device *ta, u8 val)
{
	if (ta && ta->ops && ta->ops->set_mode)
		return ta->ops->set_mode(ta, val);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_mode);

int prop_ta_device_set_voltage(struct prop_ta_device *ta, u32 mV)
{
	if (ta && ta->ops && ta->ops->set_voltage)
		return ta->ops->set_voltage(ta, mV);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_voltage);

int prop_ta_device_set_current(struct prop_ta_device *ta, u32 mA)
{
	if (ta && ta->ops && ta->ops->set_current)
		return ta->ops->set_current(ta, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_current);

int prop_ta_device_set_voltage_current(struct prop_ta_device *ta, u32 mV,
				       u32 mA)
{
	if (ta && ta->ops && ta->ops->set_voltage_current)
		return ta->ops->set_voltage_current(ta, mV, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_voltage_current);

int prop_ta_device_get_output_voltage_current(struct prop_ta_device *ta,
					      u32 *mV, u32 *mA)
{
	if (ta && ta->ops && ta->ops->get_output_voltage_current)
		return ta->ops->get_output_voltage_current(ta, mV, mA);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_get_output_voltage_current);

int prop_ta_device_authentication(struct prop_ta_device *ta)
{
	if (ta && ta->ops && ta->ops->authentication)
		return ta->ops->authentication(ta);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_authentication);

int prop_ta_device_enable_wdt(struct prop_ta_device *ta, bool en)
{
	if (ta && ta->ops && ta->ops->enable_wdt)
		return ta->ops->enable_wdt(ta, en);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_enable_wdt);

int prop_ta_device_set_wdt(struct prop_ta_device *ta, u32 ms)
{
	if (ta && ta->ops && ta->ops->set_wdt)
		return ta->ops->set_wdt(ta, ms);
	return -ENOTSUPP;
}
EXPORT_SYMBOL(prop_ta_device_set_wdt);

static int prop_ta_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct prop_ta_device *prop_ta_device_get_by_name(const char *name)
{
	struct device *dev;

	if (!prop_ta_class || !name)
		return NULL;

	dev = class_find_device(prop_ta_class, NULL, name,
				prop_ta_match_device_by_name);
	return dev ? dev_get_drvdata(dev) : NULL;
}
EXPORT_SYMBOL(prop_ta_device_get_by_name);

int prop_ta_classdev_register(struct device *parent, struct prop_ta_device *ta)
{
	if (!prop_ta_class || !ta || !ta->name)
		return -EINVAL;

	ta->dev = device_create_with_groups(prop_ta_class, parent, MKDEV(0, 0),
					    ta, ta->groups, "%s", ta->name);
	if (IS_ERR(ta->dev))
		return PTR_ERR(ta->dev);

	return 0;
}
EXPORT_SYMBOL(prop_ta_classdev_register);

void prop_ta_classdev_unregister(struct prop_ta_device *ta)
{
	if (ta && !IS_ERR_OR_NULL(ta->dev)) {
		device_unregister(ta->dev);
		ta->dev = NULL;
	}
}
EXPORT_SYMBOL(prop_ta_classdev_unregister);

static void devm_prop_ta_classdev_release(struct device *dev, void *res)
{
	struct prop_ta_device **ta = res;

	prop_ta_classdev_unregister(*ta);
}

int devm_prop_ta_classdev_register(struct device *parent,
				   struct prop_ta_device *ta)
{
	struct prop_ta_device **dr;
	int ret;

	dr = devres_alloc(devm_prop_ta_classdev_release, sizeof(*dr),
			  GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = prop_ta_classdev_register(parent, ta);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	*dr = ta;
	devres_add(parent, dr);
	return 0;
}
EXPORT_SYMBOL(devm_prop_ta_classdev_register);

static int prop_ta_classdev_suspend(struct device *dev, pm_message_t state)
{
	struct prop_ta_device *ta = dev_get_drvdata(dev);

	if (ta && ta->ops && ta->ops->suspend)
		return ta->ops->suspend(ta);
	return 0;
}

static int prop_ta_classdev_resume(struct device *dev)
{
	struct prop_ta_device *ta = dev_get_drvdata(dev);

	if (ta && ta->ops && ta->ops->resume)
		return ta->ops->resume(ta);
	return 0;
}

static int __init prop_ta_init(void)
{
	prop_ta_class = class_create(THIS_MODULE, "prop_ta");
	if (IS_ERR(prop_ta_class)) {
		pr_err("Unable to create prop_ta class; errno = %ld\n",
		       PTR_ERR(prop_ta_class));
		return PTR_ERR(prop_ta_class);
	}
	prop_ta_class->suspend = prop_ta_classdev_suspend;
	prop_ta_class->resume = prop_ta_classdev_resume;
	return 0;
}
subsys_initcall(prop_ta_init);

static void __exit prop_ta_exit(void)
{
	if (!IS_ERR_OR_NULL(prop_ta_class))
		class_destroy(prop_ta_class);
}
module_exit(prop_ta_exit);

MODULE_DESCRIPTION("Proprietary TA Class Interface");
MODULE_LICENSE("GPL v2");
