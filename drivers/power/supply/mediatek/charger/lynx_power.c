// SPDX-License-Identifier: GPL-2.0
/*
 * Lynx Universal Kernel Power Controller
 * Native in-tree implementation of:
 *   1. Hardware Bypass Charging (/proc/lynx/bypass_charging)
 *   2. Hyper Charge Thermal Unthrottle (/proc/lynx/hyper_charge)
 *   3. CPU & GPU Thermal Silencer (/proc/lynx/thermal_silencer)
 *
 * Designed for Infinix Note 11s (X698 / MT6781) & MediaTek Universal HAL
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <mt-plat/charger_type.h>

#include "lynx_power.h"

bool g_lynx_bypass_active = false;
EXPORT_SYMBOL(g_lynx_bypass_active);

bool g_lynx_hyper_charge = true;
EXPORT_SYMBOL(g_lynx_hyper_charge);

bool g_lynx_thermal_silencer = true;
EXPORT_SYMBOL(g_lynx_thermal_silencer);

extern int dcp_get_ta_status(void);

static struct proc_dir_entry *lynx_proc_dir;

/* 1. Bypass Charging proc interface */
static int lynx_bypass_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_lynx_bypass_active ? 1 : 0);
	return 0;
}

static int lynx_bypass_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_bypass_show, NULL);
}

static ssize_t lynx_bypass_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *pos)
{
	char kbuf[8];
	int val = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buffer, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strim(kbuf), 10, &val) < 0)
		return -EINVAL;

	if (val) {
		g_lynx_bypass_active = true;
		rt9471_set_bypass_mode(true);
		pr_info("LYNX_POWER: Hardware Bypass Charging ACTIVATED (current to 0mA, VSYS buck on)\n");
	} else {
		g_lynx_bypass_active = false;
		rt9471_set_bypass_mode(false);
		pr_info("LYNX_POWER: Hardware Bypass Charging DEACTIVATED (normal charging resumed)\n");
	}

	return count;
}

static const struct file_operations lynx_bypass_fops = {
	.owner = THIS_MODULE,
	.open = lynx_bypass_open,
	.read = seq_read,
	.write = lynx_bypass_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 2. Hyper Charge proc interface */
static int lynx_hyper_charge_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_lynx_hyper_charge ? 1 : 0);
	return 0;
}

static int lynx_hyper_charge_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_hyper_charge_show, NULL);
}

static ssize_t lynx_hyper_charge_write(struct file *file, const char __user *buffer,
				       size_t count, loff_t *pos)
{
	char kbuf[8];
	int val = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buffer, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strim(kbuf), 10, &val) < 0)
		return -EINVAL;

	g_lynx_hyper_charge = (val != 0);
	pr_info("LYNX_POWER: Hyper Charge %s\n", g_lynx_hyper_charge ? "ENABLED" : "DISABLED");

	return count;
}

static const struct file_operations lynx_hyper_charge_fops = {
	.owner = THIS_MODULE,
	.open = lynx_hyper_charge_open,
	.read = seq_read,
	.write = lynx_hyper_charge_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 3. Thermal Silencer proc interface */
static int lynx_thermal_silencer_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", g_lynx_thermal_silencer ? 1 : 0);
	return 0;
}

static int lynx_thermal_silencer_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_thermal_silencer_show, NULL);
}

static ssize_t lynx_thermal_silencer_write(struct file *file, const char __user *buffer,
					   size_t count, loff_t *pos)
{
	char kbuf[8];
	int val = 0;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buffer, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(strim(kbuf), 10, &val) < 0)
		return -EINVAL;

	g_lynx_thermal_silencer = (val != 0);
	pr_info("LYNX_POWER: Thermal Silencer %s\n", g_lynx_thermal_silencer ? "ENABLED" : "DISABLED");

	return count;
}

static const struct file_operations lynx_thermal_silencer_fops = {
	.owner = THIS_MODULE,
	.open = lynx_thermal_silencer_open,
	.read = seq_read,
	.write = lynx_thermal_silencer_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 4. Status proc interface */
static int lynx_status_show(struct seq_file *m, void *v)
{
	seq_printf(m, "=== LYNX UNIVERSAL POWER SUBSYSTEM ===\n");
	seq_printf(m, "Hardware Bypass Charging : %s\n", g_lynx_bypass_active ? "ACTIVE" : "INACTIVE");
	seq_printf(m, "Hyper Charge (Unthrottle): %s\n", g_lynx_hyper_charge ? "ENABLED" : "DISABLED");
	seq_printf(m, "Thermal Silencer (CPU/GPU): %s\n", g_lynx_thermal_silencer ? "ENABLED" : "DISABLED");
	seq_printf(m, "Charger Type Detected    : %d\n", (int)mt_get_charger_type());
	seq_printf(m, "Transsion RFC Fast TA    : %d\n", dcp_get_ta_status());
	return 0;
}

static int lynx_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_status_show, NULL);
}

static const struct file_operations lynx_status_fops = {
	.owner = THIS_MODULE,
	.open = lynx_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init lynx_power_init(void)
{
	lynx_proc_dir = proc_mkdir("lynx", NULL);
	if (!lynx_proc_dir) {
		pr_err("LYNX_POWER: Failed to create /proc/lynx\n");
		return -ENOMEM;
	}

	proc_create("bypass_charging", 0666, lynx_proc_dir, &lynx_bypass_fops);
	proc_create("hyper_charge", 0666, lynx_proc_dir, &lynx_hyper_charge_fops);
	proc_create("thermal_silencer", 0666, lynx_proc_dir, &lynx_thermal_silencer_fops);
	proc_create("status", 0444, lynx_proc_dir, &lynx_status_fops);

	pr_info("LYNX_POWER: Native kernel power management initialized successfully (/proc/lynx/*)\n");
	return 0;
}

late_initcall(lynx_power_init);

MODULE_AUTHOR("NOIR");
MODULE_DESCRIPTION("Lynx Universal Power Subsystem");
MODULE_LICENSE("GPL");
