// SPDX-License-Identifier: GPL-2.0
/*
 * Lynx Universal Kernel Power Controller
 * Native in-tree implementation of:
 *   1. Manual Hardware Bypass Charging (/proc/lynx/bypass_charging)
 *   2. Full-Charge Bypass (/proc/lynx/full_bypass & /proc/lynx/bypass_active)
 *   3. Hyper Charge Thermal Unthrottle (/proc/lynx/hyper_charge)
 *   4. CPU & GPU Thermal Silencer (/proc/lynx/thermal_silencer)
 *
 * Designed for Infinix Note 11s (X698 / MT6781) & MediaTek Universal HAL
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>

#include "lynx_power.h"

bool g_lynx_bypass_active = false;
EXPORT_SYMBOL(g_lynx_bypass_active);

bool g_lynx_full_bypass_enabled = true;
EXPORT_SYMBOL(g_lynx_full_bypass_enabled);

bool g_lynx_full_bypass_latched = false;
EXPORT_SYMBOL(g_lynx_full_bypass_latched);

bool g_lynx_hyper_charge = true;
EXPORT_SYMBOL(g_lynx_hyper_charge);

bool g_lynx_thermal_silencer = false;

void __weak thrm_aware_lynx_release(void)
{
}

void __weak fbt_lynx_sync_silencer(bool silencer)
{
}

EXPORT_SYMBOL(g_lynx_thermal_silencer);

extern int dcp_get_ta_status(void);

static DEFINE_MUTEX(lynx_proc_mutex);
static DEFINE_MUTEX(lynx_silencer_mutex);
static struct proc_dir_entry *lynx_proc_dir;

void lynx_bypass_on_plug_out(void)
{
	WRITE_ONCE(g_lynx_bypass_active, false);
	WRITE_ONCE(g_lynx_full_bypass_latched, false);
}
EXPORT_SYMBOL(lynx_bypass_on_plug_out);

void lynx_bypass_set_full_latch(bool latch)
{
	WRITE_ONCE(g_lynx_full_bypass_latched, latch);
	if (!latch && !lynx_is_manual_bypass_active()) {
		rt9471_set_bypass_mode(false);
		lynx_sync_bypass_charger(false);
	}
}
EXPORT_SYMBOL(lynx_bypass_set_full_latch);

void lynx_bypass_clear_manual_emergency(void)
{
	WRITE_ONCE(g_lynx_bypass_active, false);
}
EXPORT_SYMBOL(lynx_bypass_clear_manual_emergency);

/* 1. Manual Hardware Bypass Charging proc interface */
static int lynx_bypass_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", lynx_is_manual_bypass_active() ? 1 : 0);
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

	mutex_lock(&lynx_proc_mutex);
	if (val) {
		int chr_type = (int)mt_get_charger_type();
		int uisoc = battery_get_uisoc();
		int vbat = battery_get_bat_voltage();
		int vbus = battery_get_vbus();
		bool vbus_gd = false;

		rt9471_lynx_get_hw_info(NULL, NULL, NULL, &vbus_gd, NULL);
		if (chr_type == CHARGER_UNKNOWN) {
			mutex_unlock(&lynx_proc_mutex);
			pr_warn("LYNX_POWER: Refuse manual bypass - no charger attached (type=%d, vbus_gd=%d, vbus=%d)\n",
				chr_type, vbus_gd ? 1 : 0, vbus);
			return -ENODEV;
		}

		if (uisoc >= 0 && uisoc <= 3 && vbat > 0 && vbat < 3450) {
			mutex_unlock(&lynx_proc_mutex);
			pr_warn("LYNX_POWER: Refuse manual bypass - critical low battery (uisoc=%d%%, vbat=%dmV)\n",
				uisoc, vbat);
			return -EINVAL;
		}

		WRITE_ONCE(g_lynx_bypass_active, true);
		rt9471_set_bypass_mode(true);
		lynx_sync_bypass_charger(true);
		pr_info("LYNX_POWER: Manual Hardware Bypass ACTIVATED (CHG_EN=0, HZ=0, VSYS pass-through)\n");
	} else {
		WRITE_ONCE(g_lynx_bypass_active, false);
		/* User explicitly turned OFF bypass: unlatch full bypass so charging resumes immediately */
		WRITE_ONCE(g_lynx_full_bypass_latched, false);
		rt9471_set_bypass_mode(false);
		lynx_sync_bypass_charger(false);
		pr_info("LYNX_POWER: Hardware Bypass DEACTIVATED (normal charging resumed)\n");
	}
	mutex_unlock(&lynx_proc_mutex);

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

/* 2. Full-Charge Bypass proc interface */
static int lynx_full_bypass_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", lynx_is_full_bypass_enabled() ? 1 : 0);
	return 0;
}

static int lynx_full_bypass_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_full_bypass_show, NULL);
}

static ssize_t lynx_full_bypass_write(struct file *file, const char __user *buffer,
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

	mutex_lock(&lynx_proc_mutex);
	if (val) {
		WRITE_ONCE(g_lynx_full_bypass_enabled, true);
		lynx_sync_bypass_charger(lynx_is_bypass_effective());
		pr_info("LYNX_POWER: Full-Charge Bypass ENABLED\n");
	} else {
		bool was_latched = READ_ONCE(g_lynx_full_bypass_latched);

		WRITE_ONCE(g_lynx_full_bypass_enabled, false);
		WRITE_ONCE(g_lynx_full_bypass_latched, false);
		if (was_latched && !lynx_is_bypass_effective()) {
			rt9471_set_bypass_mode(false);
			lynx_sync_bypass_charger(false);
		}
		pr_info("LYNX_POWER: Full-Charge Bypass DISABLED (was_latched=%d)\n",
			was_latched ? 1 : 0);
	}
	mutex_unlock(&lynx_proc_mutex);

	return count;
}

static const struct file_operations lynx_full_bypass_fops = {
	.owner = THIS_MODULE,
	.open = lynx_full_bypass_open,
	.read = seq_read,
	.write = lynx_full_bypass_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 3. Read-only Effective Bypass Active interface */
static int lynx_bypass_active_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", lynx_is_bypass_effective() ? 1 : 0);
	return 0;
}

static int lynx_bypass_active_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_bypass_active_show, NULL);
}

static const struct file_operations lynx_bypass_active_fops = {
	.owner = THIS_MODULE,
	.open = lynx_bypass_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 4. Hyper Charge proc interface */
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
	if (g_lynx_hyper_charge) {
		/* Explicit command to enter Hyper Charge: clear any bypass latch! */
		WRITE_ONCE(g_lynx_bypass_active, false);
		WRITE_ONCE(g_lynx_full_bypass_latched, false);
		rt9471_set_bypass_mode(false);
		lynx_sync_bypass_charger(false);
	}
	lynx_sync_hyper_hv(g_lynx_hyper_charge);
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

/* 5. Thermal Silencer proc interface */
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

	mutex_lock(&lynx_silencer_mutex);
	WRITE_ONCE(g_lynx_thermal_silencer, val != 0);
	smp_wmb();
	if (g_lynx_thermal_silencer) {
		mt_ppm_cpu_thermal_protect(0);
		mt_ppm_pwrthro_lynx_release();
		apthermolmt_lynx_release_limits();
		mt_gpufreq_lynx_release_limits();
		cpuhvfs_update_imax_state(0);
		cpuhvfs_update_imax_thermal_state(0);
		cfp_lynx_sync_silencer(true);
		fbt_lynx_sync_silencer(true);
		thrm_aware_lynx_release();
		lynx_sync_sspm_thermal_throttle(true);
	} else {
		cfp_lynx_sync_silencer(false);
		lynx_sync_sspm_thermal_throttle(false);
	}
	pr_info("LYNX_POWER: Thermal Silencer %s\n", g_lynx_thermal_silencer ? "ENABLED" : "DISABLED");
	mutex_unlock(&lynx_silencer_mutex);

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

__weak void lynx_sync_sspm_thermal_throttle(bool disable)
{
}

__weak int mt_gpufreq_lynx_probe_pll(unsigned int khz, int mode, char *buf, int sz)
{
	return scnprintf(buf, sz, "gpu_probe_weak\n");
}

__weak int mt_cpufreq_lynx_probe_pll(unsigned int khz, int mode, char *buf, int sz)
{
	return scnprintf(buf, sz, "cpu_probe_weak\n");
}

__weak int mt_cpufreq_lynx_probe_cci(unsigned int khz, int mode, char *buf, int sz)
{
	return scnprintf(buf, sz, "cci_probe_weak\n");
}

static char lynx_pll_probe_buf[512] = "ready\n";

static int lynx_pll_probe_show(struct seq_file *m, void *v)
{
	seq_puts(m, lynx_pll_probe_buf);
	return 0;
}

static int lynx_pll_probe_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_pll_probe_show, NULL);
}

static ssize_t lynx_pll_probe_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char kbuf[64], target[16];
	unsigned int khz = 0;
	int mode = 0;
	size_t len = min(count, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	if (sscanf(kbuf, "%15s %u %d", target, &khz, &mode) == 3) {
		if (!strcmp(target, "gpu"))
			mt_gpufreq_lynx_probe_pll(khz, mode, lynx_pll_probe_buf,
						  sizeof(lynx_pll_probe_buf));
		else if (!strcmp(target, "cpu"))
			mt_cpufreq_lynx_probe_pll(khz, mode, lynx_pll_probe_buf,
						  sizeof(lynx_pll_probe_buf));
		else if (!strcmp(target, "cci"))
			mt_cpufreq_lynx_probe_cci(khz, mode, lynx_pll_probe_buf,
						  sizeof(lynx_pll_probe_buf));
		else
			scnprintf(lynx_pll_probe_buf, sizeof(lynx_pll_probe_buf),
				  "usage: gpu|cpu|cci <khz> <mode>\n");
	} else {
		scnprintf(lynx_pll_probe_buf, sizeof(lynx_pll_probe_buf),
			  "usage: gpu|cpu|cci <khz> <mode>\n");
	}
	return count;
}

static const struct file_operations lynx_pll_probe_fops = {
	.open = lynx_pll_probe_open,
	.read = seq_read,
	.write = lynx_pll_probe_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/* 6. Status proc interface */
static const char *lynx_chr_type_str(int type)
{
	switch (type) {
	case 0: return "UNKNOWN (Disconnected)";
	case 1: return "STANDARD_HOST (PC USB Port)";
	case 2: return "CHARGING_HOST (PC CDP)";
	case 3: return "NONSTANDARD_CHARGER";
	case 4: return "STANDARD_CHARGER (Wall Fast Charger)";
	case 5: return "APPLE_2_1A_CHARGER";
	case 6: return "APPLE_1_0A_CHARGER";
	default: return "OTHER";
	}
}

static int lynx_status_show(struct seq_file *m, void *v)
{
	int type = (int)mt_get_charger_type();
	int vbat = battery_get_bat_voltage();
	int vbus = battery_get_vbus();
	int ibat_ma = battery_get_bat_current() / 10;
	int uisoc = battery_get_uisoc();
	bool hz = false, chg_en = false, chg_done = false, vbus_gd = false;
	u32 aicr_ua = 0;
	int hw_ret;
	const char *full_state;

	if (!lynx_is_full_bypass_enabled())
		full_state = "DISABLED";
	else if (lynx_is_full_bypass_active())
		full_state = "ENABLED [LATCHED]";
	else
		full_state = "ENABLED [STANDBY]";

	hw_ret = rt9471_lynx_get_hw_info(&hz, &chg_en, &chg_done, &vbus_gd, &aicr_ua);

	seq_printf(m, "=== LYNX UNIVERSAL POWER SUBSYSTEM ===\n");
	seq_printf(m, "Manual Bypass Charging   : %s\n",
		   lynx_is_manual_bypass_active() ? "ACTIVE" : "INACTIVE");
	seq_printf(m, "Full-Charge Bypass       : %s\n", full_state);
	seq_printf(m, "Effective Bypass State   : %s\n",
		   lynx_is_bypass_effective() ? "ACTIVE (VSYS PASS-THROUGH)" : "INACTIVE");
	if (hw_ret == 0) {
		seq_printf(m, "RT9471 IC Registers      : HZ=%d CHG_EN=%d CHGDONE=%d VBUS_GD=%d AICR=%u mA\n",
			   hz ? 1 : 0, chg_en ? 1 : 0, chg_done ? 1 : 0,
			   vbus_gd ? 1 : 0, aicr_ua / 1000U);
	} else {
		seq_printf(m, "RT9471 IC Registers      : UNAVAILABLE (%d)\n", hw_ret);
	}
	seq_printf(m, "Battery Telemetry        : VBAT=%d mV VBUS=%d mV IBAT=%d mA UISOC=%d%%\n",
		   vbat, vbus, ibat_ma, uisoc);
	seq_printf(m, "Hyper Charge (Unthrottle): %s\n", g_lynx_hyper_charge ? "ENABLED" : "DISABLED");
	seq_printf(m, "Thermal Silencer (CPU/GPU): %s\n", g_lynx_thermal_silencer ? "ENABLED" : "DISABLED");
	seq_printf(m, "GPU Max Cap (kHz): %u\n", mt_gpufreq_lynx_get_oc_khz());
	seq_printf(m, "Charger Type Detected    : %d [%s]\n", type, lynx_chr_type_str(type));
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


static int lynx_gpu_max_khz_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", mt_gpufreq_lynx_get_oc_khz());
	return 0;
}
static int lynx_gpu_max_khz_open(struct inode *inode, struct file *file)
{
	return single_open(file, lynx_gpu_max_khz_show, NULL);
}
static ssize_t lynx_gpu_max_khz_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned int val = 0;
	char kbuf[32];
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	if (kstrtouint(strim(kbuf), 10, &val))
		return -EINVAL;
	ret = mt_gpufreq_lynx_set_oc_khz(val);
	return ret ? ret : count;
}
static const struct file_operations lynx_gpu_max_khz_fops = {
	.open = lynx_gpu_max_khz_open,
	.read = seq_read,
	.write = lynx_gpu_max_khz_write,
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
	proc_create("full_bypass", 0666, lynx_proc_dir, &lynx_full_bypass_fops);
	proc_create("bypass_active", 0444, lynx_proc_dir, &lynx_bypass_active_fops);
	proc_create("hyper_charge", 0666, lynx_proc_dir, &lynx_hyper_charge_fops);
	proc_create("thermal_silencer", 0666, lynx_proc_dir, &lynx_thermal_silencer_fops);
	proc_create("gpu_max_khz", 0664, lynx_proc_dir, &lynx_gpu_max_khz_fops);
	proc_create("status", 0444, lynx_proc_dir, &lynx_status_fops);
	proc_create("pll_probe", 0664, lynx_proc_dir, &lynx_pll_probe_fops);

	pr_info("LYNX_POWER: Native kernel power management initialized successfully (/proc/lynx/*)\n");
	return 0;
}

late_initcall(lynx_power_init);

MODULE_AUTHOR("NOIR");
MODULE_DESCRIPTION("Lynx Universal Power Subsystem");
MODULE_LICENSE("GPL");
