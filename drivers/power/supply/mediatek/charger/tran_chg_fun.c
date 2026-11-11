/*
 * Copyright (C) 2021 TRANSSION Holdings
 *
 * Transsion charging helper functions, thermal throttling, JEITA limits,
 * call state / game mode management, OTG control, and diagnostics.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/pinctrl/consumer.h>
#include <linux/power_supply.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_charger.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/mtk_boot.h>
#include <mt-plat/mtk_thermal_platform.h>
#include "mtk_charger_intf.h"
#include "tran_chg_fun.h"
#include "mtk_pe_intf.h"
#include "mtk_pe20_intf.h"
#include "mtk_pe40_intf.h"
#include "mtk_hvdcp_intf.h"
#include "mtk_tc30_intf.h"
#include "mtk_pdc_intf.h"
#include <mt-plat/mtk_thermal_monitor.h>

#define TRAN_CALL_DROP_CURRENT 1200000

/* Exported globals */
int tran_charge_type = 0;
EXPORT_SYMBOL(tran_charge_type);

int tran_game_mode = 0;
EXPORT_SYMBOL(tran_game_mode);

int monkey_flag = 0;
EXPORT_SYMBOL(monkey_flag);

int g_tran_qmax = 5000;
EXPORT_SYMBOL(g_tran_qmax);

int g_tran_bat_id = 0;
EXPORT_SYMBOL(g_tran_bat_id);

int g_tran_jeita_ichg_before = 0;
EXPORT_SYMBOL(g_tran_jeita_ichg_before);

int g_tran_jeita_ichg_later = 0;
EXPORT_SYMBOL(g_tran_jeita_ichg_later);

int g_tran_jeita_iinput_before = 0;
EXPORT_SYMBOL(g_tran_jeita_iinput_before);

int g_tran_jeita_iiput_later = 0;
EXPORT_SYMBOL(g_tran_jeita_iiput_later);

int tran_pe50_running = 0;
EXPORT_SYMBOL(tran_pe50_running);

int tran_pe50_full = 0;
EXPORT_SYMBOL(tran_pe50_full);

int lcd_bl_level = 0;
EXPORT_SYMBOL(lcd_bl_level);

/* Internal states */
static int memtest_input_current = 0;
static int tran_aging_stop_flag = 0;
static int g_max_temp_delta = 0;
static int g_ibat_temp_cur = 0;
static int flag_pe = 0;
static int flag_hvdcp = 0;
static int flag_tc30 = 0;
static int tran_otg_ctl = 0;
static int en_lcd_on_tml = 0;
static int enable_pcb_tml = 1;
static int pcb_temp_step_one = 43000;
static int pcb_cur_step_one = 200;
static int pcb_temp_step_two = 43000;
static int pcb_cur_step_two = 200;
static int pcb_min_chg_cur = 1000000;

static int Tpcb_store_ok = 0;
static int Tstep1 = 0, Tcur1 = 0, Tstep2 = 0, Tcur2 = 0, Tmin_curr = 0;

static int g_tran_test_total_fg = 0;
static int last_coulomb_saletool = 0;
static int fault_nonstandard_retry = 0;
static int fault_vbus_retry = 0;
static int fault_vdpm_retry = 0;
static int g_tran_last_current_value = 0;

static struct charger_device *primary_chg = NULL;
static struct pinctrl *tran_pinctrl = NULL;
static struct pinctrl_state *tran_otg_state_high = NULL;
static struct pinctrl_state *tran_otg_state_low = NULL;
static struct pinctrl_state *tran_meta_state_high = NULL;
static struct pinctrl_state *tran_meta_state_low = NULL;

/* Interface functions provided by mtk_*_intf.h */

static void change_current_by_screen_light(struct charger_manager *info)
{
	int delta;

	if (!en_lcd_on_tml || !info)
		return;

	if ((mtk_is_pep_series_connect(info) || mtk_pe_get_is_connect(info)) &&
	    lcd_bl_level >= 1) {
		delta = g_ibat_temp_cur - 2000000;
		if (delta < 0)
			delta = 0;
		if (delta > g_max_temp_delta)
			g_max_temp_delta = delta;
	}
}

static void change_current_by_pcb_temp(void)
{
	int Tpcb = mtk_thermal_get_temp(MTK_THERMAL_SENSOR_AP);
	int step1 = Tpcb_store_ok ? Tstep1 : pcb_temp_step_one;
	int cur1  = Tpcb_store_ok ? Tcur1  : pcb_cur_step_one;
	int step2 = Tpcb_store_ok ? Tstep2 : pcb_temp_step_two;
	int cur2  = Tpcb_store_ok ? Tcur2  : pcb_cur_step_two;
	int delta = 0;

	if (Tpcb == -127000)
		return;

	if (Tpcb > step1 && Tpcb <= step2) {
		delta = cur1 * (Tpcb - step1);
	} else if (Tpcb > step2) {
		delta = cur1 * (step2 - step1) + cur2 * (Tpcb - step2);
	}

	if (delta < 0)
		delta = 0;

	if (delta > g_max_temp_delta)
		g_max_temp_delta = delta;
}

static void change_current_by_call_stats(struct charger_data *pdata)
{
	if (!pdata)
		return;

	if (tran_game_mode == 2 && pdata->charging_current_limit > TRAN_CALL_DROP_CURRENT) {
		pdata->charging_current_limit = TRAN_CALL_DROP_CURRENT;
	}
}

static void change_current_by_temp(struct charger_manager *info)
{
	int min_cur;
	int Ibat;

	if (!enable_pcb_tml || !info)
		return;

	change_current_by_pcb_temp();

	min_cur = Tpcb_store_ok ? Tmin_curr : pcb_min_chg_cur;
	if (g_max_temp_delta > g_ibat_temp_cur - min_cur)
		g_max_temp_delta = g_ibat_temp_cur - min_cur;

	Ibat = g_ibat_temp_cur - g_max_temp_delta;
	if (Ibat < min_cur)
		Ibat = min_cur;

	if (!(flag_pe | flag_hvdcp | flag_tc30 | mtk_pdc_check_charger(info)))
		info->chg1_data.input_current_limit = Ibat;

	info->chg1_data.charging_current_limit = Ibat;
}

int set_jeita_charging_current(struct charger_manager *info,
			       struct charger_data *pdata)
{
	int boot_mode;

	if (!info || !pdata)
		return 0;

	flag_pe = 0;
	flag_hvdcp = 0;
	flag_tc30 = 0;

	if (info->chr_type != STANDARD_CHARGER || !info->enable_sw_jeita)
		return 0;

	g_ibat_temp_cur = info->data.ac_charger_current;

	if (mtk_pe_get_is_connect(info) || mtk_pe20_get_is_connect(info)) {
		flag_pe = 1;
		g_ibat_temp_cur = info->data.ta_ac_charger_current;
	} else if (mtk_hvdcp20_get_is_connect(info)) {
		flag_hvdcp = 1;
		g_ibat_temp_cur = info->data.ta_ac_charger_current;
	} else if (mtk_tc30_get_is_connect(info)) {
		flag_tc30 = 1;
		g_ibat_temp_cur = info->data.ta_ac_charger_current;
	} else if (mtk_pdc_check_charger(info)) {
		g_ibat_temp_cur = info->data.ta_ac_charger_current;
	}

	switch (info->sw_jeita.sm) {
	case TEMP_BELOW_T0:
		pdata->charging_current_limit = 0;
		break;
	case TEMP_T0_TO_T1:
		pdata->charging_current_limit = info->data.jeita_temp_pos0_to_pos1_current;
		break;
	case TEMP_T1_TO_T2:
		pdata->charging_current_limit = info->data.jeita_temp_pos1_to_pos2_current;
		break;
	case TEMP_T2_TO_T3:
		pdata->charging_current_limit = g_ibat_temp_cur;
		break;
	case TEMP_T3_TO_T4:
		pdata->charging_current_limit = info->data.jeita_temp_pos3_to_pos4_current;
		break;
	case TEMP_ABOVE_T4:
	default:
		pdata->charging_current_limit = 0;
		break;
	}

	g_max_temp_delta = g_ibat_temp_cur - pdata->charging_current_limit;
	boot_mode = get_boot_mode();

	if (boot_mode != 8 && boot_mode != 9 &&
	    info->sw_jeita.sm != TEMP_BELOW_T0 &&
	    info->sw_jeita.sm != TEMP_ABOVE_T4) {
		change_current_by_temp(info);
		change_current_by_screen_light(info);

		change_current_by_call_stats(pdata);
		info->chg1_data.charging_current_limit = pdata->charging_current_limit;
	}

	if (memtest_input_current != 0) {
		pdata->input_current_limit = memtest_input_current * 1000;
		pdata->charging_current_limit = memtest_input_current * 1000;
	}

	return 0;
}
EXPORT_SYMBOL(set_jeita_charging_current);

int tran_ctl_aging_capacity(struct charger_device *chg_dev, int en)
{
	int ui_soc = battery_get_soc();
	int boot_mode = get_boot_mode();

	switch (monkey_flag) {
	case 2: /* Monkey test */
		if (ui_soc >= 75) {
			tran_aging_stop_flag = 1;
			charger_dev_enable(chg_dev, false);
			charger_dev_enable_powerpath(chg_dev, false);
		} else if (ui_soc <= 70) {
			tran_aging_stop_flag = 0;
			charger_dev_enable(chg_dev, true);
			charger_dev_enable_powerpath(chg_dev, true);
		}
		break;
	case 3: /* Force disable charging */
		tran_aging_stop_flag = 1;
		charger_dev_enable(chg_dev, false);
		charger_dev_enable_powerpath(chg_dev, false);
		break;
	case 4: /* Force enable charging */
		tran_aging_stop_flag = 0;
		charger_dev_enable(chg_dev, true);
		charger_dev_enable_powerpath(chg_dev, true);
		break;
	case 5: /* Exhibition test */
		if ((boot_mode & 0x72) && ui_soc >= 100) {
			tran_aging_stop_flag = 1;
			charger_dev_enable(chg_dev, false);
			charger_dev_enable_powerpath(chg_dev, false);
		} else if (ui_soc <= 60) {
			tran_aging_stop_flag = 0;
			charger_dev_enable(chg_dev, true);
			charger_dev_enable_powerpath(chg_dev, true);
		}
		break;
	default:
		tran_aging_stop_flag = 0;
		charger_dev_enable(chg_dev, !!en);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(tran_ctl_aging_capacity);

int tran_get_cv_comp(struct charger_manager *info, u32 cv)
{
	int ichg_ma;
	int comp;

	if (!info || info->chr_type != STANDARD_CHARGER)
		return cv;

	ichg_ma = battery_get_bat_current() / 10;
	if (ichg_ma < 0)
		ichg_ma = 0;

	comp = ichg_ma * info->data.r_bat_comp;
	if (comp > (int)info->data.v_comp_max)
		comp = info->data.v_comp_max;

	return cv + comp;
}
EXPORT_SYMBOL(tran_get_cv_comp);

void tran_clear_notifycode_func(struct charger_manager *info)
{
	if (!info)
		return;
	info->notify_code = 0;
}
EXPORT_SYMBOL(tran_clear_notifycode_func);

int saletool_fg_accumulative(int coulomb)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != 0 && boot_mode != 7)
		return 0;

	if (g_tran_addtion_time == 0x7fffffff)
		g_tran_addtion_time = 0x7ffffffe;

	if (last_coulomb_saletool - coulomb >= 1) {
		g_tran_test_total_fg += (last_coulomb_saletool - coulomb);
	}
	last_coulomb_saletool = coulomb;

	return 0;
}
EXPORT_SYMBOL(saletool_fg_accumulative);

int saletool_fault_analysis(struct charger_manager *info, int vbus)
{
	int boot_mode = get_boot_mode();
	int vdpm_status;
	int ret_ichg;
	int ret_input;

	if (boot_mode != 0 && boot_mode != 7)
		return 0;

	if (!info || !primary_chg)
		return 0;

	if (info->chr_type == CHARGER_UNKNOWN) {
		fault_nonstandard_retry++;
		if (fault_nonstandard_retry >= 10) {
			fault_nonstandard_retry = 0;
			pr_notice("saletool: nonstandard charger detected\n");
			return 1;
		}
	} else {
		fault_nonstandard_retry = 0;
	}

	if (info->chr_type == STANDARD_CHARGER) {
		if (vbus <= 4199) {
			fault_vbus_retry++;
			if (fault_vbus_retry >= 10) {
				fault_vbus_retry = 0;
				pr_notice("saletool: vbus < 4200mv on standard charger\n");
				return 2;
			}
		} else {
			fault_vbus_retry = 0;
		}

		{
			bool _vdpm_en = false;
			charger_dev_get_vdpm_status(primary_chg, &_vdpm_en);
			vdpm_status = _vdpm_en ? 1 : 0;
		}
		if (vdpm_status == 1) {
			fault_vdpm_retry++;
			if (fault_vdpm_retry >= 10) {
				fault_vdpm_retry = 0;
				pr_notice("saletool: vdpm triggered 10 times\n");
				return 3;
			}
		} else {
			fault_vdpm_retry = 0;
		}

		if (g_tran_jeita_ichg_later != 0) {
			ret_ichg = charger_dev_charging_current_analyze(primary_chg);
			if (ret_ichg != 1 && ret_ichg != -ENOTSUPP) {
				pr_notice("saletool: ichg analyze error\n");
				return 4;
			}
		}

		if (g_tran_jeita_iiput_later != 0) {
			ret_input = charger_dev_input_current_analyze(primary_chg);
			if (ret_input != 1 && ret_input != -ENOTSUPP) {
				pr_notice("saletool: iinput analyze error\n");
				return 4;
			}
		}

		if (g_tran_jeita_ichg_later != 0 && g_tran_jeita_iiput_later != 0 &&
		    g_tran_jeita_iinput_before != 0 && g_tran_jeita_ichg_before != 0) {
			if (g_tran_jeita_ichg_later < g_tran_jeita_ichg_before ||
			    g_tran_jeita_iiput_later < g_tran_jeita_iinput_before) {
				if (g_tran_last_current_value != g_tran_jeita_iiput_later) {
					g_tran_last_current_value = g_tran_jeita_iiput_later;
					pr_notice("saletool: thermal throttling active (ichg=%d, aicr=%d)\n",
						  g_tran_jeita_ichg_later, g_tran_jeita_iiput_later);
				}
				return 5;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(saletool_fault_analysis);

bool tran_sw_close_otg(void)
{
	return (tran_otg_ctl == 2);
}
EXPORT_SYMBOL(tran_sw_close_otg);

bool tran_usb_5per_close_otg(void)
{
	int ui_soc = battery_get_soc();
	int boot_mode = get_boot_mode();

	if (boot_mode == 0 && ui_soc <= 5)
		return true;

	return false;
}
EXPORT_SYMBOL(tran_usb_5per_close_otg);

int tran_get_otg_state(void)
{
	if (!primary_chg)
		return 0;
	return charger_dev_get_boost_status(primary_chg);
}
EXPORT_SYMBOL(tran_get_otg_state);

/* get_switch_device is implemented in tran_usb_switch.c */

/* Sysfs Attributes for tran_battery */
static ssize_t show_battery_qmax(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_tran_qmax);
}

static ssize_t store_battery_qmax(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0)
		g_tran_qmax = val;
	return size;
}

static ssize_t show_charging_callstate(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", tran_game_mode);
}

static ssize_t store_charging_callstate(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0)
		tran_game_mode = val;
	return size;
}

static ssize_t show_chg_capacity_test(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", monkey_flag);
}

static ssize_t store_chg_capacity_test(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0) {
		monkey_flag = val;
		if (monkey_flag == 6) {
			struct charger_manager *pinfo = tran_get_pinfo();
			if (pinfo) {
				mtk_pe_set_to_check_chr_type(pinfo, 1);
				mtk_pe20_set_to_check_chr_type(pinfo, 1);
				mtk_hvdcp20_set_to_check_chr_type(pinfo, 1);
				mtk_tc30_set_to_check_chr_type(pinfo, 1);
			}
		}
	}
	return size;
}

static ssize_t show_fg_coulomb(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_tran_test_total_fg);
}

static ssize_t store_fg_coulomb(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0)
		g_tran_test_total_fg = val;
	return size;
}

static ssize_t show_pump_express_vcharger(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	int vbus = battery_get_vbus();
	return sprintf(buf, "%d\n", vbus);
}

static ssize_t show_pump_express_icharger(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	int ibat = battery_get_bat_current();
	return sprintf(buf, "%d\n", ibat);
}

static ssize_t show_tran_cam(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", en_lcd_on_tml);
}

static ssize_t store_tran_cam(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0)
		en_lcd_on_tml = val;
	return size;
}

static ssize_t show_tran_bat_temp(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", tran_game_mode);
}

static ssize_t store_tran_bat_temp(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0)
		tran_game_mode = val;
	return size;
}

static ssize_t show_pcb_thermal_debug(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	int Tpcb = mtk_thermal_get_temp(MTK_THERMAL_SENSOR_AP);
	return sprintf(buf, "Tpcb=%d step1=%d cur1=%d step2=%d cur2=%d min=%d\n",
		       Tpcb,
		       Tpcb_store_ok ? Tstep1 : pcb_temp_step_one,
		       Tpcb_store_ok ? Tcur1  : pcb_cur_step_one,
		       Tpcb_store_ok ? Tstep2 : pcb_temp_step_two,
		       Tpcb_store_ok ? Tcur2  : pcb_cur_step_two,
		       Tpcb_store_ok ? Tmin_curr : pcb_min_chg_cur);
}

static ssize_t store_pcb_thermal_debug(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	int s1, c1, s2, c2, mc;
	if (sscanf(buf, "%d %d %d %d %d", &s1, &c1, &s2, &c2, &mc) == 5) {
		Tstep1 = s1;
		Tcur1 = c1;
		Tstep2 = s2;
		Tcur2 = c2;
		Tmin_curr = mc;
		Tpcb_store_ok = 1;
	}
	return size;
}

static ssize_t show_otg_ctl(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", tran_otg_ctl);
}

static ssize_t store_otg_ctl(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int val;
	if (kstrtoint(buf, 10, &val) == 0) {
		tran_otg_ctl = val;
		if (tran_otg_ctl == 1 && tran_otg_state_high && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_otg_state_high);
		else if (tran_otg_ctl == 0 && tran_otg_state_low && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_otg_state_low);
	}
	return size;
}

static DEVICE_ATTR(BATTERY_QMAX, 0664, show_battery_qmax, store_battery_qmax);
static DEVICE_ATTR(Charging_CallState, 0664, show_charging_callstate, store_charging_callstate);
static DEVICE_ATTR(CHG_CAPACITY_TEST, 0664, show_chg_capacity_test, store_chg_capacity_test);
static DEVICE_ATTR(fg_coulomb, 0664, show_fg_coulomb, store_fg_coulomb);
static DEVICE_ATTR(Pump_Express_VCharger, 0444, show_pump_express_vcharger, NULL);
static DEVICE_ATTR(Pump_Express_ICharger, 0444, show_pump_express_icharger, NULL);
static DEVICE_ATTR(tran_cam, 0664, show_tran_cam, store_tran_cam);
static DEVICE_ATTR(tran_bat_temp, 0664, show_tran_bat_temp, store_tran_bat_temp);
static DEVICE_ATTR(pcb_thermal_debug, 0664, show_pcb_thermal_debug, store_pcb_thermal_debug);
static DEVICE_ATTR(OTG_CTL, 0664, show_otg_ctl, store_otg_ctl);

static struct attribute *tran_battery_attrs[] = {
	&dev_attr_BATTERY_QMAX.attr,
	&dev_attr_Charging_CallState.attr,
	&dev_attr_CHG_CAPACITY_TEST.attr,
	&dev_attr_fg_coulomb.attr,
	&dev_attr_Pump_Express_VCharger.attr,
	&dev_attr_Pump_Express_ICharger.attr,
	&dev_attr_tran_cam.attr,
	&dev_attr_tran_bat_temp.attr,
	&dev_attr_pcb_thermal_debug.attr,
	&dev_attr_OTG_CTL.attr,
	NULL,
};

static const struct attribute_group tran_battery_attr_group = {
	.attrs = tran_battery_attrs,
};

/* Proc limit_current */
static ssize_t limit_current_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	char kbuf[32];
	int len = snprintf(kbuf, sizeof(kbuf), "%d\n", memtest_input_current);
	return simple_read_from_buffer(buf, count, ppos, kbuf, len);
}

static ssize_t limit_current_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	char kbuf[32];
	int val;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';

	if (kstrtoint(kbuf, 10, &val) == 0)
		memtest_input_current = val;

	return count;
}

static const struct file_operations limit_current_fops = {
	.owner = THIS_MODULE,
	.read = limit_current_read,
	.write = limit_current_write,
};

/* OTG FTM misc device */
static long otg_ftm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == 0x40046b0f) { /* OTG_CTL */
		if (arg == 1 && tran_otg_state_high && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_otg_state_high);
		else if (arg == 0 && tran_otg_state_low && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_otg_state_low);
		return 0;
	} else if (cmd == 0x40046b10) { /* META_CTL */
		if (arg == 1 && tran_meta_state_high && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_meta_state_high);
		else if (arg == 0 && tran_meta_state_low && tran_pinctrl)
			pinctrl_select_state(tran_pinctrl, tran_meta_state_low);
		return 0;
	}
	return -ENOIOCTLCMD;
}

static const struct file_operations otg_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = otg_ftm_ioctl,
	.compat_ioctl = otg_ftm_ioctl,
};

static struct miscdevice otg_ftm_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "otg_ftm",
	.fops = &otg_ftm_fops,
};

static int tran_5per_close_otg_callback(struct notifier_block *nb,
					unsigned long val, void *v)
{
	struct power_supply *psy = (struct power_supply *)v;
	union power_supply_propval pval;

	if (val == PSY_EVENT_PROP_CHANGED && psy &&
	    !strcmp(psy->desc->name, "battery")) {
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &pval) == 0) {
			if (pval.intval <= 5 && get_boot_mode() == 0) {
				if (primary_chg && charger_dev_get_boost_status(primary_chg)) {
					charger_dev_enable_otg(primary_chg, false);
					pr_notice("tran_battery: battery <= 5%%, OTG boost disabled\n");
				}
			}
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block tran_5per_close_otg_nb = {
	.notifier_call = tran_5per_close_otg_callback,
};

static int tran_battery_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct proc_dir_entry *proc_dir;
	u32 val;
	int ret;

	primary_chg = get_charger_by_name("primary_chg");
	if (!primary_chg)
		pr_notice("tran_battery: primary_chg not ready yet, will retry\n");

	if (np) {
		enable_pcb_tml = of_property_read_bool(np, "enable_pcb_tml");
		if (!of_property_read_u32(np, "pcb_temp_step_one", &val))
			pcb_temp_step_one = val;
		if (!of_property_read_u32(np, "pcb_cur_step_one", &val))
			pcb_cur_step_one = val;
		if (!of_property_read_u32(np, "pcb_temp_step_two", &val))
			pcb_temp_step_two = val;
		if (!of_property_read_u32(np, "pcb_cur_step_two", &val))
			pcb_cur_step_two = val;
		if (!of_property_read_u32(np, "pcb_min_chg_cur", &val))
			pcb_min_chg_cur = val;
	}

	tran_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR_OR_NULL(tran_pinctrl)) {
		tran_otg_state_high = pinctrl_lookup_state(tran_pinctrl, "otg_pin_high");
		tran_otg_state_low = pinctrl_lookup_state(tran_pinctrl, "otg_pin_low");
		tran_meta_state_high = pinctrl_lookup_state(tran_pinctrl, "meta_pin_high");
		tran_meta_state_low = pinctrl_lookup_state(tran_pinctrl, "meta_pin_low");
	} else {
		tran_pinctrl = NULL;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &tran_battery_attr_group);
	if (ret)
		pr_err("tran_battery: failed to create sysfs group (%d)\n", ret);

	proc_dir = proc_mkdir("tran_chg_limit_current", NULL);
	if (proc_dir)
		proc_create("limit_current", 0664, proc_dir, &limit_current_fops);

	misc_register(&otg_ftm_dev);

	tran_5per_close_otg_nb.notifier_call = tran_5per_close_otg_callback;
	power_supply_reg_notifier(&tran_5per_close_otg_nb);

	pr_info("tran_battery: Transsion battery & thermal management probe completed\n");
	return 0;
}

static int tran_battery_remove(struct platform_device *pdev)
{
	power_supply_unreg_notifier(&tran_5per_close_otg_nb);
	misc_deregister(&otg_ftm_dev);
	remove_proc_subtree("tran_chg_limit_current", NULL);
	sysfs_remove_group(&pdev->dev.kobj, &tran_battery_attr_group);
	return 0;
}

static const struct of_device_id tran_battery_of_match[] = {
	{ .compatible = "tran,chg_fun", },
	{},
};
MODULE_DEVICE_TABLE(of, tran_battery_of_match);

static struct platform_driver tran_battery_driver = {
	.probe = tran_battery_probe,
	.remove = tran_battery_remove,
	.driver = {
		.name = "tran_battery",
		.of_match_table = tran_battery_of_match,
	},
};

static int __init tran_battery_init(void)
{
	return platform_driver_register(&tran_battery_driver);
}

static void __exit tran_battery_exit(void)
{
	platform_driver_unregister(&tran_battery_driver);
}

module_init(tran_battery_init);
module_exit(tran_battery_exit);

MODULE_AUTHOR("Transsion Holdings");
MODULE_DESCRIPTION("Transsion Battery Charging & Thermal Controller");
MODULE_LICENSE("GPL");
