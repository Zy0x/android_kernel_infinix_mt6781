/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <generated/autoconf.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
//#include <linux/pm_wakeup.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/power_supply.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/irq.h>
#include <linux/notifier.h>

#include <mt-plat/mtk_battery.h>
#include <mt-plat/upmu_common.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#include <mt-plat/mtk_boot.h>
#include <mt-plat/charger_type.h>
#include <mt-plat/charger_class.h>
#include <mt6358/mtk_pmic_api.h>

#define __SW_CHRDET_IN_PROBE_PHASE__

static enum charger_type g_chr_type;
static struct charger_device *primary_chg;
static bool first_flag;
#ifdef __SW_CHRDET_IN_PROBE_PHASE__
static struct work_struct chr_work;
#endif

static DEFINE_MUTEX(chrdet_lock);
static RAW_NOTIFIER_HEAD(chrdet_notifier_head);

#ifdef CONFIG_USB_MTK_OTG
extern int iddig_eint_num;
extern bool usb_to_otg;
#endif

int register_charger_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&chrdet_notifier_head, nb);
}
EXPORT_SYMBOL(register_charger_notifier);

int charger_notifier_call_chain(unsigned long val, void *v)
{
	return raw_notifier_call_chain(&chrdet_notifier_head, val, v);
}
EXPORT_SYMBOL(charger_notifier_call_chain);

static struct power_supply *chrdet_psy;
static int chrdet_inform_psy_changed(enum charger_type chg_type,
				bool chg_online)
{
	int ret = 0;
	union power_supply_propval propval;

	pr_debug("charger type: %s: online = %d, type = %d\n",
		__func__, chg_online, chg_type);

	/* Inform chg det power supply */
	if (chg_online) {
		propval.intval = chg_online;
		ret = power_supply_set_property(chrdet_psy,
			POWER_SUPPLY_PROP_ONLINE, &propval);
		if (ret < 0)
			pr_debug("%s: psy online failed, ret = %d\n",
				__func__, ret);

		propval.intval = chg_type;
		ret = power_supply_set_property(chrdet_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
		if (ret < 0)
			pr_debug("%s: psy type failed, ret = %d\n",
				__func__, ret);

		power_supply_changed(chrdet_psy);
		return ret;
	}

	propval.intval = chg_type;
	ret = power_supply_set_property(chrdet_psy,
		POWER_SUPPLY_PROP_CHARGE_TYPE, &propval);
	if (ret < 0)
		pr_debug("%s: psy type failed, ret(%d)\n",
			__func__, ret);

	propval.intval = chg_online;
	ret = power_supply_set_property(chrdet_psy,
		POWER_SUPPLY_PROP_ONLINE, &propval);
	if (ret < 0)
		pr_debug("%s: psy online failed, ret(%d)\n",
			__func__, ret);

	power_supply_changed(chrdet_psy);
	return ret;
}


int hw_charging_get_charger_type(void)
{
	int boot_mode = 0;
	int chg_type = CHARGER_UNKNOWN;
	int wait_cnt = 0;

	boot_mode = get_boot_mode();
	pr_info("hw_bc11_init boot_mode=%d\n", boot_mode);
	msleep(200);

	if (boot_mode != RECOVERY_BOOT && !first_flag) {
		while (!is_usb_rdy() && wait_cnt < 200) {
			pr_info("CDP, block\n");
			msleep(100);
			wait_cnt++;
		}
		if (wait_cnt >= 200)
			pr_info("CDP, timeout\n");
		else
			pr_info("CDP, free\n");
		first_flag = true;
	} else {
		pr_info("CDP, PASS\n");
	}

	if (primary_chg) {
		chg_type = charger_dev_get_ext_chgtyp(primary_chg);
		if (chg_type < 0)
			chg_type = CHARGER_UNKNOWN;
	}

	pr_info("hw_charging_get_charger_type: chg_type = %d\n", chg_type);
	return chg_type;
}

/*****************************************************************************
 * Charger Detection
 ******************************************************************************/
void mtk_pmic_enable_chr_type_det(bool en)
{
	if (!mt_usb_is_device() && !pmic_get_register_value(PMIC_RGS_CHRDET)) {
		g_chr_type = CHARGER_UNKNOWN;
		pr_info("charger type: UNKNOWN, Now is usb host mode. Skip detection\n");
		return;
	}

	mutex_lock(&chrdet_lock);

	if (en) {
		if (is_meta_mode()) {
			pr_notice("force Standard USB Host in meta\n");
			g_chr_type = STANDARD_HOST;
			chrdet_inform_psy_changed(g_chr_type, 1);
		} else {
			pr_info("charger type: charger IN\n");
			g_chr_type = hw_charging_get_charger_type();
			chrdet_inform_psy_changed(g_chr_type, 1);
		}
	} else {
		pr_info("charger type: charger OUT\n");
		g_chr_type = CHARGER_UNKNOWN;
		chrdet_inform_psy_changed(g_chr_type, 0);
	}

	mutex_unlock(&chrdet_lock);
	charger_notifier_call_chain(en ? 1 : 0, NULL);
}

static void chrdet_handle_unplug_hw(void)
{
	if (primary_chg)
		charger_dev_set_dp_dm_recover(primary_chg);
#ifdef CONFIG_USB_MTK_OTG
	if (usb_to_otg) {
		enable_irq(iddig_eint_num);
		usb_to_otg = false;
	}
#endif
}

void do_charger_detect(void)
{
	if (!mt_usb_is_device() && !pmic_get_register_value(PMIC_RGS_CHRDET)) {
		g_chr_type = CHARGER_UNKNOWN;
		pr_debug("charger type: UNKNOWN, Now is usb host mode. Skip detection!!!\n");
		return;
	}

	if (pmic_get_register_value(PMIC_RGS_CHRDET)) {
		mtk_pmic_enable_chr_type_det(true);
	} else {
		chrdet_handle_unplug_hw();
		mtk_pmic_enable_chr_type_det(false);
	}
}



/*****************************************************************************
 * PMIC Int Handler
 ******************************************************************************/
void chrdet_int_handler(void)
{
	/*
	 * pr_info("[chrdet_int_handler]CHRDET status = %d....\n",
	 *	pmic_get_register_value(PMIC_RGS_CHRDET));
	 */
#ifdef CONFIG_MTK_KERNEL_POWER_OFF_CHARGING
	if (!pmic_get_register_value(PMIC_RGS_CHRDET)) {
		int boot_mode = 0;

		boot_mode = get_boot_mode();

		if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT ||
		    boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
			pr_info("[%s] Unplug Charger/USB\n", __func__);
#ifndef CONFIG_TCPC_CLASS
			orderly_poweroff(true);
#else
			return;
#endif
		}
	}
#endif
	do_charger_detect();
}


/************************************************
 * Charger Probe Related
 ************************************************/
#ifdef __SW_CHRDET_IN_PROBE_PHASE__
static void do_charger_detection_work(struct work_struct *data)
{
	do_charger_detect();
}
#endif

static int __init pmic_chrdet_init(void)
{
	mutex_init(&chrdet_lock);
	chrdet_psy = power_supply_get_by_name("charger");
	if (!chrdet_psy) {
		pr_info("%s: get power supply failed\n", __func__);
		return -EINVAL;
	}

	primary_chg = get_charger_by_name("primary_chg");
	if (!primary_chg) {
		pr_info("%s: get primary_chg failed\n", __func__);
		return -EINVAL;
	}

#ifdef __SW_CHRDET_IN_PROBE_PHASE__
	/* do charger detect here to prevent HW miss interrupt*/
	INIT_WORK(&chr_work, do_charger_detection_work);
	schedule_work(&chr_work);
#endif

	mt6358_upmu_set_rg_vcdt_hv_vth(0xd);
	pmic_register_interrupt_callback(INT_CHRDET_EDGE, chrdet_int_handler);
	pmic_enable_interrupt(INT_CHRDET_EDGE, 1, "PMIC");

	return 0;
}

late_initcall(pmic_chrdet_init);
