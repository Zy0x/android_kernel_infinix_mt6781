/*
 * Copyright (C) 2021 TRANSSION Holdings
 *
 * Transsion Battery Aging Monitoring and Health Estimation Driver.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "aging_tables.h"
#include "tran_chg_fun.h"

int g_tran_aging_rate = 1000;
EXPORT_SYMBOL(g_tran_aging_rate);

int g_tran_addtion_time = 0;
EXPORT_SYMBOL(g_tran_addtion_time);

static int g_tran_aging_debug = 1;
static int g_tran_estimate_total_fg = 0;
static int g_tran_last_coulomb = 0;
static int g_tran_add_soc = 0;
static int g_tran_start_soc = 0;
static int g_tran_last_soc = -1;
static int g_tran_upload_fg = 0;
static int g_tran_upload_calculate = 0;
static int g_tran_upload_reset = 0;
static int g_tran_fg_large = 0;
static int battery_aging_module = 0;
static struct platform_device *aging_pdev = NULL;

static int *aging_tbl[4] = {
	aging_tbl_0,
	aging_tbl_1,
	aging_tbl_2,
	aging_tbl_3,
};

static char *aging_envp_0[] = { "BAT_AGING=1", NULL };
static char *aging_envp_1[] = { "BAT_ESTIMATE=1", NULL };
static char *aging_envp_2[] = { "BAT_CALCULATE=1", NULL };
static char *aging_envp_3[] = { "BAT_PLUG_RESET=1", NULL };

static void aging_uevent(struct platform_device *pdev, int type)
{
	char **envp = NULL;
	int ret;

	if (!pdev) {
		if (g_tran_aging_debug >= 2)
			pr_info("aging_uevent pdev is null !!!\n");
		return;
	}

	switch (type) {
	case 0:
		envp = aging_envp_0;
		break;
	case 1:
		envp = aging_envp_1;
		break;
	case 2:
		envp = aging_envp_2;
		break;
	case 3:
		envp = aging_envp_3;
		break;
	default:
		if (g_tran_aging_debug >= 2)
			pr_info("phone master aging_uevent is default!!!\n");
		return;
	}

	if (g_tran_aging_debug >= 2)
		pr_info("aging_uevent !!!i=%d env = %s\n", type, envp[0]);

	ret = kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, envp);
	if (ret != 0 && g_tran_aging_debug >= 2)
		pr_info("aging_uevent: kobject_uevent_fail, ret=%d\n", ret);
}

void battery_fg_aging(int temp, int soc, int coulomb)
{
	int delta_fg;
	int expect_capacity;

	if (!aging_pdev) {
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master fg:pdev is null return\n");
		return;
	}

	delta_fg = g_tran_last_coulomb - coulomb;
	if (delta_fg >= 1) {
		g_tran_upload_fg = delta_fg;
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master fg:for test upload_fg = %d\n", g_tran_upload_fg);

		if (g_tran_upload_fg >= 5001) {
			g_tran_fg_large = 1;
			if (g_tran_aging_debug >= 2)
				pr_info("phone_master fg:upload_fg is too large!!!fg_large = 1\n");
		} else {
			aging_uevent(aging_pdev, 0);
		}
	}

	if (g_tran_aging_debug > 1)
		pr_info("phone_master testing ! upload_fg=%d\n", g_tran_upload_fg);

	g_tran_last_coulomb = coulomb;

	if (soc + g_tran_add_soc <= 70) {
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master soc is too low ! soc= %d,add_soc = %d TRAN_LOW_SOC = 10, TRAN_ESTIMATE_SOC = 60\n",
				soc, g_tran_add_soc);
		return;
	}

	if (temp < 20 || temp >= 56) {
		g_tran_estimate_total_fg = 0;
		g_tran_add_soc = 0;
		g_tran_start_soc = 0;
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master tmp is over value = %d !!! fg: g_tran_estimate_total_fg clear\n", temp);
		return;
	}

	if (tran_charge_type != 0) {
		g_tran_estimate_total_fg = 0;
		g_tran_add_soc = 0;
		g_tran_start_soc = 0;
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master charging type = %d !!! fg: g_tran_estimate_total_fg clear\n", tran_charge_type);
		return;
	}

	if (soc < 10 || soc >= 91) {
		g_tran_estimate_total_fg = 0;
		g_tran_add_soc = 0;
		g_tran_start_soc = 0;
		if (g_tran_aging_debug >= 2)
			pr_info("phone_master soc is %d not in range !!! fg: g_tran_estimate_total_fg clear\n", soc);
		return;
	}

	if (g_tran_start_soc == 0)
		g_tran_start_soc = soc;

	if (g_tran_aging_debug > 1)
		pr_info("phone_master start_soc = %d\n", g_tran_start_soc);

	if (g_tran_last_coulomb - coulomb >= 1 && g_tran_start_soc > soc)
		g_tran_estimate_total_fg += (g_tran_last_coulomb - coulomb);

	if (g_tran_aging_debug > 1)
		pr_info("phone_master fg:for test g_tran_estimate_total_fg = %d\n", g_tran_estimate_total_fg);

	g_tran_last_coulomb = coulomb;

	if (g_tran_last_soc != -1 && g_tran_last_soc - soc > 0 && g_tran_start_soc > soc)
		g_tran_add_soc += (g_tran_last_soc - soc);

	g_tran_last_soc = soc;

	if (g_tran_aging_debug >= 2)
		pr_info("phone_master fg: add_soc = %d\n", g_tran_add_soc);

	if (g_tran_add_soc >= 61) {
		expect_capacity = (g_tran_qmax * 980) / 1000;
		if (expect_capacity > 0)
			g_tran_aging_rate = ((g_tran_estimate_total_fg * 1000) / expect_capacity) / 6;

		if (g_tran_aging_debug >= 2)
			pr_info("phone_master ESTIMATE: g_tran_aging_rate = %d,tran_transition = %d,expect_capacity = %d\n",
				g_tran_aging_rate, g_tran_estimate_total_fg, expect_capacity);

		if (g_tran_aging_rate >= 1001) {
			g_tran_aging_rate = 1000;
		} else if (g_tran_aging_rate <= 950 && g_tran_fg_large == 1) {
			g_tran_aging_rate += 20;
			if (g_tran_aging_debug >= 2)
				pr_info("phone_master compensation: g_tran_aging_rate = %d\n", g_tran_aging_rate);
		}

		if (g_tran_aging_debug >= 2)
			pr_info("phone_master write_nvram: g_tran_estimate_total_fg= %d,g_tran_aging_rate = %d\n",
				g_tran_estimate_total_fg, g_tran_aging_rate);

		aging_uevent(aging_pdev, 1);
		g_tran_estimate_total_fg = 0;
		g_tran_add_soc = 0;
	}
}
EXPORT_SYMBOL(battery_fg_aging);

static ssize_t show_aging_fg(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	if (g_tran_aging_debug >= 2)
		pr_info("phone_master show_aging_fg = %d\n", g_tran_upload_fg);
	return sprintf(buf, "%d\n", g_tran_upload_fg);
}

static ssize_t store_aging_fg(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	if (g_tran_aging_debug >= 2)
		pr_info("store_aging_fg buf is %s\n", buf);
	return size;
}

static ssize_t show_estimate_rate(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (g_tran_aging_debug >= 2)
		pr_info("phone_master show_estimate_rate = %d\n", g_tran_aging_rate);
	return sprintf(buf, "%d\n", g_tran_aging_rate);
}

static ssize_t store_estimate_rate(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	if (g_tran_aging_debug >= 2)
		pr_info("store_estimate_rate buf is %s\n", buf);
	return size;
}

static ssize_t show_aging_calculate(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	if (g_tran_aging_debug >= 2)
		pr_info("phone_master show_aging_calculate = %d\n", g_tran_upload_calculate);
	return sprintf(buf, "%d\n", g_tran_upload_calculate);
}

static ssize_t store_aging_calculate(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	unsigned long long cycle;
	int ret;
	int midx = battery_aging_module & 3;

	g_tran_upload_calculate = 0;
	if (!buf || size == 0)
		return size;

	if (g_tran_aging_debug >= 2)
		pr_info("store_aging_calculate buf is %s\n", buf);

	if (kstrtoull(buf, 10, &cycle) < 0)
		return size;

	if (g_tran_aging_debug >= 2)
		pr_info("phone_master start aging_find_list\n");

	if (cycle <= 500) {
		ret = aging_tbl[midx][cycle];
	} else {
		ret = aging_tbl[midx][499] - (int)((cycle - 500) / 5);
	}

	if (g_tran_aging_debug >= 2)
		pr_info("phone_master aging_find_list FORWARD !!! ret = %d\n", ret);

	g_tran_upload_calculate = ret;
	aging_uevent(aging_pdev, 2);
	return size;
}

static ssize_t show_aging_reset(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (g_tran_aging_debug >= 2)
		pr_info("phone_master show_aging_reset = %d\n", g_tran_upload_reset);
	return sprintf(buf, "%d\n", g_tran_upload_reset);
}

static ssize_t store_aging_reset(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	unsigned long long target;
	int midx = battery_aging_module & 3;
	int ret = -1;

	g_tran_upload_reset = 0;
	if (!buf || size == 0)
		return size;

	if (g_tran_aging_debug >= 2)
		pr_info("store_aging_reset buf is %s\n", buf);

	if (kstrtoull(buf, 10, &target) < 0)
		return size;

	if (g_tran_aging_debug >= 2)
		pr_info("phone_master start aging_find_list\n");

	if (aging_tbl[midx][499] >= (int)target) {
		ret = 500 + (aging_tbl[midx][499] - (int)target) * 5;
	} else {
		int front = 0;
		int end = 499;
		int mid;
		int loop = 500;

		if (g_tran_aging_debug >= 2) {
			pr_info("phone_master aging_find_list REVERSE !!! 1111\n");
			pr_info("phone_master start find_index\n");
		}

		while (loop-- > 0 && front <= end) {
			mid = (front + end) / 2;
			if (g_tran_aging_debug >= 2)
				pr_info("phone_master start find_index front=%d,mid=%d,end=%d\n",
					front, mid, end);

			if (aging_tbl[midx][mid] <= (int)target) {
				ret = mid;
				break;
			}
			if (mid + 1 <= 499 && aging_tbl[midx][mid + 1] < (int)target) {
				ret = mid;
				break;
			}
			if (aging_tbl[midx][mid] > (int)target)
				front = mid + 1;
			else
				end = mid - 1;
		}

		if (g_tran_aging_debug >= 2)
			pr_info("phone_master aging_find_list REVERSE !!! 2222 %d\n", ret);
	}

	if (g_tran_aging_debug >= 2)
		pr_info("phone_master aging_find_list REVERSE !!! ret = %d\n", ret);

	g_tran_upload_reset = ret;
	aging_uevent(aging_pdev, 3);
	return size;
}

static ssize_t show_plug_times(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	if (g_tran_aging_debug >= 2)
		pr_info("phone_master g_tran_addtion_time = %d\n", g_tran_addtion_time);
	return sprintf(buf, "%d\n", g_tran_addtion_time);
}

static ssize_t store_plug_times(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	if (g_tran_aging_debug >= 2)
		pr_info("store_plug_times\n");
	return size;
}

static DEVICE_ATTR(AGING_FG, 0664, show_aging_fg, store_aging_fg);
static DEVICE_ATTR(ESTIMATE_RATE, 0664, show_estimate_rate, store_estimate_rate);
static DEVICE_ATTR(AGING_CALCULATE, 0664, show_aging_calculate, store_aging_calculate);
static DEVICE_ATTR(AGING_RESET, 0664, show_aging_reset, store_aging_reset);
static DEVICE_ATTR(PLUG_TIMES, 0664, show_plug_times, store_plug_times);

static struct attribute *tran_aging_attrs[] = {
	&dev_attr_AGING_FG.attr,
	&dev_attr_ESTIMATE_RATE.attr,
	&dev_attr_AGING_CALCULATE.attr,
	&dev_attr_AGING_RESET.attr,
	&dev_attr_PLUG_TIMES.attr,
	NULL,
};

static const struct attribute_group tran_aging_attr_group = {
	.attrs = tran_aging_attrs,
};

static int tran_aging_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	u32 val = 0;
	int ret;

	aging_pdev = pdev;

	if (np) {
		if (!of_property_read_u32(np, "battery_aging_module0", &val))
			battery_aging_module = val;
		else if (!of_property_read_u32(np, "battery_aging_module1", &val))
			battery_aging_module = val;
		else if (!of_property_read_u32(np, "battery_aging_module2", &val))
			battery_aging_module = val;
		else if (!of_property_read_u32(np, "battery_aging_module3", &val))
			battery_aging_module = val;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &tran_aging_attr_group);
	if (ret) {
		pr_err("%s: failed to create sysfs group (%d)\n", __func__, ret);
		return ret;
	}

	pr_info("%s: Transsion battery aging initialized (module=%d)\n",
		__func__, battery_aging_module);
	return 0;
}

static int tran_aging_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &tran_aging_attr_group);
	aging_pdev = NULL;
	return 0;
}

static const struct of_device_id tran_aging_of_match[] = {
	{ .compatible = "tran,chg_aging", },
	{},
};
MODULE_DEVICE_TABLE(of, tran_aging_of_match);

static struct platform_driver tran_aging_driver = {
	.probe = tran_aging_probe,
	.remove = tran_aging_remove,
	.driver = {
		.name = "tran_battery_aging",
		.of_match_table = tran_aging_of_match,
	},
};

static int __init tran_aging_init(void)
{
	return platform_driver_register(&tran_aging_driver);
}

static void __exit tran_aging_exit(void)
{
	platform_driver_unregister(&tran_aging_driver);
}

module_init(tran_aging_init);
module_exit(tran_aging_exit);

MODULE_AUTHOR("Transsion Holdings");
MODULE_DESCRIPTION("Transsion Battery Aging Driver");
MODULE_LICENSE("GPL");
