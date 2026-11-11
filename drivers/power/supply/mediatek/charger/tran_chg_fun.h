/*
 * Copyright (C) 2021 TRANSSION Holdings
 *
 * Transsion charging helper functions and aging interface header.
 */

#ifndef _TRAN_CHG_FUN_H_
#define _TRAN_CHG_FUN_H_

#include <linux/types.h>
#include <linux/power_supply.h>

struct charger_manager;
struct charger_data;
struct charger_device;

/* Exported globals from tran_chg_fun.c */
extern int tran_charge_type;
extern int tran_game_mode;
extern int monkey_flag;
extern int g_tran_qmax;
extern int g_tran_bat_id;
extern int g_tran_jeita_ichg_before;
extern int g_tran_jeita_ichg_later;
extern int g_tran_jeita_iinput_before;
extern int g_tran_jeita_iiput_later;
extern int tran_pe50_running;
extern int tran_pe50_full;
extern int lcd_bl_level;

/* Exported globals from tran_chg_aging.c */
extern int g_tran_aging_rate;
extern int g_tran_addtion_time;

#ifdef CONFIG_TRAN_CHARGER_FUNCS_ADD
extern int set_jeita_charging_current(struct charger_manager *info,
				      struct charger_data *pdata);
extern int tran_ctl_aging_capacity(struct charger_device *chg_dev, int en);
extern int tran_get_cv_comp(struct charger_manager *info, u32 cv);
extern void tran_clear_notifycode_func(struct charger_manager *info);
extern int saletool_fg_accumulative(int coulomb);
extern int saletool_fault_analysis(struct charger_manager *info, int vbus);
extern bool tran_sw_close_otg(void);
extern bool tran_usb_5per_close_otg(void);
extern int tran_get_otg_state(void);
extern struct power_supply *get_switch_device(void);
#else
static inline int set_jeita_charging_current(struct charger_manager *info,
					     struct charger_data *pdata)
{
	return 0;
}
static inline int tran_ctl_aging_capacity(struct charger_device *chg_dev, int en)
{
	return 0;
}
static inline int tran_get_cv_comp(struct charger_manager *info, u32 cv)
{
	return cv;
}
static inline void tran_clear_notifycode_func(struct charger_manager *info) {}
static inline int saletool_fg_accumulative(int coulomb) { return 0; }
static inline int saletool_fault_analysis(struct charger_manager *info, int vbus)
{
	return 0;
}
static inline bool tran_sw_close_otg(void) { return false; }
static inline bool tran_usb_5per_close_otg(void) { return false; }
static inline int tran_get_otg_state(void) { return 0; }
static inline struct power_supply *get_switch_device(void) { return NULL; }
#endif /* CONFIG_TRAN_CHARGER_FUNCS_ADD */

#ifdef CONFIG_TRAN_CHARGER_AGING_SUPPORT
extern void battery_fg_aging(int temp, int soc, int coulomb);
#else
static inline void battery_fg_aging(int temp, int soc, int coulomb) {}
#endif /* CONFIG_TRAN_CHARGER_AGING_SUPPORT */

#endif /* _TRAN_CHG_FUN_H_ */
