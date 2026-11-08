// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 */

#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/leds_pwm.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "leds-mtk-pwm.h"
#include "../../../gpu/drm/mediatek/mtk_disp_aal.h"


#define CONFIG_LEDS_BRIGHTNESS_CHANGED
/****************************************************************************
 * variables
 ***************************************************************************/
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :" fmt, __func__, __LINE__

struct mtk_leds_info;

static int led_level_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness);

struct led_debug_info {
	unsigned long long current_t;
	unsigned long long last_t;
	char buffer[4096];
	int count;
};

struct led_desp {
	int index;
	char name[16];
};

struct leds_desp_info {
	int lens;
	struct led_desp *leds[0];
};

struct led_pwm_info {
	struct pwm_device *pwm;
	struct led_pwm config;
	unsigned long long duty;
};

struct mtk_led_data {
	struct led_desp desp;
	struct led_conf_info	conf;
	int last_level;
	int brightness;
	struct led_pwm_info info;
	struct mtk_leds_info	*parent;
	struct led_debug_info debug;
	struct work_struct work;
};

struct mtk_leds_info {
	struct device *dev;
	int			nums;
	struct mtk_led_data leds[0];
};

static DEFINE_MUTEX(leds_mutex);
struct leds_desp_info *leds_info;
static BLOCKING_NOTIFIER_HEAD(mtk_leds_chain_head);

int mtk_leds_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_register_notifier);

int mtk_leds_unregister_notifier(struct notifier_block *nb)

{
	return blocking_notifier_chain_unregister(&mtk_leds_chain_head, nb);
}
EXPORT_SYMBOL_GPL(mtk_leds_unregister_notifier);

int mtk_leds_call_notifier(unsigned long action, void *data)
{
	return blocking_notifier_call_chain(&mtk_leds_chain_head, action, data);
}
EXPORT_SYMBOL_GPL(mtk_leds_call_notifier);


static int call_notifier(int event, struct mtk_led_data *led_dat)
{
	int err;

	err = mtk_leds_call_notifier(event, &led_dat->conf);
	if (err)
		pr_info("notifier_call_chain error\n");
	return err;
}

/****************************************************************************
 * DEBUG MACROS
 ***************************************************************************/

static void led_debug_log(struct mtk_led_data *s_led,
		int level, int mappingLevel)
{
	unsigned long cur_time_mod = 0;
	unsigned long long cur_time_display = 0;
	int ret = 0;

	s_led->debug.current_t = sched_clock();
	cur_time_display = s_led->debug.current_t;
	do_div(cur_time_display, 1000000);
	cur_time_mod = do_div(cur_time_display, 1000);

	ret = snprintf(s_led->debug.buffer + strlen(s_led->debug.buffer),
		4095 - strlen(s_led->debug.buffer),
		"T:%lld.%ld,L:%d L:%d map:%d    ",
		cur_time_display, cur_time_mod,
		s_led->conf.cdev.brightness, level, mappingLevel);

	s_led->debug.count++;

	if (ret < 0 || ret >= 4096) {
		pr_info("print log error!");
		s_led->debug.count = 5;
	}

	if (level == 0 || s_led->debug.count >= 5 ||
		(s_led->debug.current_t - s_led->debug.last_t) > 1000000000) {
		pr_info("%s", s_led->debug.buffer);
		s_led->debug.count = 0;
		s_led->debug.buffer[strlen("[Light] Set directly ") +
			strlen(s_led->conf.cdev.name)] = '\0';
	}

	s_led->debug.last_t = sched_clock();
}


static int getLedDespIndex(char *name)
{
	int i = 0;

	while (i < leds_info->lens) {
		if (!strcmp(name, leds_info->leds[i]->name))
			return i;
		i++;
	}
	return -1;
}


/****************************************************************************
 * driver functions
 ***************************************************************************/

static void __led_pwm_set(struct led_pwm_info *led_info)
{
	int new_duty = led_info->duty;
	int off_duty = led_info->config.active_low ?
		led_info->config.pwm_period_ns : 0;

	mutex_lock(&leds_mutex);

	pwm_config(led_info->pwm, new_duty, led_info->config.pwm_period_ns);
	if (new_duty == off_duty)
		pwm_disable(led_info->pwm);
	else
		pwm_enable(led_info->pwm);

	mutex_unlock(&leds_mutex);
}

/*
 * BLK-19 & P2 PWM Level-0 Audit: Low-level hardware PWM register helper.
 * The legacy static wrapper led_level_pwm_set() has been removed completely.
 * For the primary display backlight ("lcd-backlight"), this function is
 * ONLY invoked while holding g_lynx_arbiter.arbiter_lock (or during final
 * platform teardown after lynx_arbiter_deinit()).
 * Explicitly clamps negative brightness to 0 and ensures the PWM channel is
 * disabled whenever brightness == 0 (even if active_low inverts duty).
 */
int mtk_led_pwm_set_hardware(struct mtk_led_data *led_dat, int brightness)
{
	unsigned int max;
	unsigned long long duty;

	if (!led_dat)
		return -EINVAL;

	/* BLK-44: Immediate glitch-free shutdown for zero brightness (period_ns on active_low) */
	if (brightness <= 0) {
		int off_duty = led_dat->info.config.active_low ?
			led_dat->info.config.pwm_period_ns : 0;

		led_dat->conf.level = 0;
		led_dat->info.duty = off_duty;
		mutex_lock(&leds_mutex);
		pwm_config(led_dat->info.pwm, off_duty,
			   led_dat->info.config.pwm_period_ns);
		pwm_disable(led_dat->info.pwm);
		mutex_unlock(&leds_mutex);
		return 0;
	}

	brightness = min(brightness, led_dat->conf.max_level);
	led_dat->conf.level = brightness;
	max = (1 << led_dat->conf.trans_bits) - 1;
	if (max <= 0)
		max = 4095;
	duty = led_dat->info.config.pwm_period_ns;
	duty *= (unsigned int)brightness;
	do_div(duty, max);

	if (led_dat->info.config.active_low)
		duty = led_dat->info.config.pwm_period_ns - duty;

	led_dat->info.duty = duty;

	__led_pwm_set(&led_dat->info);
	return 0;
}
EXPORT_SYMBOL(mtk_led_pwm_set_hardware);

/*
 * BLK-22: Opaque accessor used by the Lynx Arbiter (mtk_disp_aal.c), which
 * cannot dereference struct mtk_led_data. Programs hardware PWM and updates
 * last_level (and optionally brightness). Caller must hold
 * g_lynx_arbiter.arbiter_lock for the "lcd-backlight" device.
 */
void mtk_led_pwm_commit_level(struct mtk_led_data *led_dat, int level,
			      bool update_brightness)
{
	if (!led_dat)
		return;

	mtk_led_pwm_set_hardware(led_dat, level);
	led_dat->last_level = level;
	if (update_brightness)
		led_dat->brightness = level;
}
EXPORT_SYMBOL(mtk_led_pwm_commit_level);

/*
 * BLK-19, BLK-26, BLK-30, BLK-33: Route mt_leds_brightness_set() through the
 * dedicated CCORR Silky Brightness handoff validator lynx_arbiter_route_ccorr_silky().
 * Unverified callers attempting to mutate "lcd-backlight" without an active,
 * unexpired AAL Silky handoff token are rejected by lynx_arbiter_route_ccorr_silky().
 */
int mt_leds_brightness_set(char *name, int level)
{
	struct mtk_led_data *led_dat;
	u64 cb_open_id = 0, cb_tx_id = 0, cb_seq = 0;
	u32 cb_epoch = 0;
	pid_t cb_tid = 0;
	int cb_backlight = -1;
	int index;
	bool routed;

	/*
	 * BLK-38: Acquire arbiter_lock BEFORE touching shutting_down or dereferencing
	 * leds_info to guarantee mutual exclusion against driver teardown / UAF.
	 */
	mutex_lock(&g_lynx_arbiter.arbiter_lock);
	if (unlikely(g_lynx_arbiter.shutting_down || !leds_info)) {
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		return 0;
	}

	index = getLedDespIndex(name);
	if (index < 0) {
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		pr_notice("can not find leds by led_desp %s", name);
		return -1;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mtk_led_data, desp);

	lynx_arbiter_get_current_ccorr_context(&cb_open_id, &cb_epoch, &cb_tid,
					       &cb_tx_id, &cb_seq, &cb_backlight);
	/*
	 * BLK-19, BLK-38, BLK-39: Verify Silky Brightness handoff token
	 * against full FIFO-popped provenance tuple (open_id + epoch + tid + tx_id + seq + backlight).
	 */
	routed = lynx_arbiter_route_ccorr_silky_locked(led_dat, level, cb_open_id, cb_epoch,
						       cb_tid, cb_tx_id, cb_seq, cb_backlight);
	if (!routed) {
		/* BLK-43: Secondary LED (not lcd-backlight) updated under arbiter_lock with thermal clamp */
		int effective_level = min(level, led_dat->conf.max_level);
		mtk_led_pwm_set_hardware(led_dat, effective_level);
		led_dat->conf.level = effective_level;
		led_dat->brightness = effective_level;
	}
	mutex_unlock(&g_lynx_arbiter.arbiter_lock);

	return 0;
}
EXPORT_SYMBOL(mt_leds_brightness_set);

/*
 * BLK-19, BLK-26, BLK-34, BLK-38: Route mtk_led_work() deferred workqueue path through
 * lynx_arbiter_route_legacy_locked() with arbiter_lock held throughout.
 */
void mtk_led_work(struct work_struct *work)
{
	struct mtk_led_data *led_data =
	    container_of(work, struct mtk_led_data, work);

	mutex_lock(&g_lynx_arbiter.arbiter_lock);
	if (unlikely(g_lynx_arbiter.shutting_down || !leds_info)) {
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		return;
	}

	if (!lynx_arbiter_route_legacy_locked(led_data, led_data->conf.level)) {
		mtk_led_pwm_set_hardware(led_data, led_data->conf.level);
	}
	mutex_unlock(&g_lynx_arbiter.arbiter_lock);
}


static int led_pwm_disable(struct led_pwm_info *led_info)
{
	int off_duty;

	if (unlikely(!led_info || !led_info->pwm))
		return 0;

	/* BLK-44: Active-low panels require period_ns duty for zero light emission */
	off_duty = led_info->config.active_low ?
		led_info->config.pwm_period_ns : 0;
	led_info->duty = off_duty;

	mutex_lock(&leds_mutex);
	pwm_config(led_info->pwm, off_duty, led_info->config.pwm_period_ns);
	pwm_disable(led_info->pwm);
	mutex_unlock(&leds_mutex);

	return 0;
}

int mtk_led_pwm_disable_hardware(struct mtk_led_data *led_dat)
{
	if (!led_dat)
		return -EINVAL;
	return led_pwm_disable(&(led_dat->info));
}
EXPORT_SYMBOL(mtk_led_pwm_disable_hardware);

static int led_level_set(struct led_classdev *led_cdev,
					  enum led_brightness brightness)
{
	int trans_level = 0;

	struct led_conf_info *led_conf =
		container_of(led_cdev, struct led_conf_info, cdev);
	struct mtk_led_data *led_dat =
		container_of(led_conf, struct mtk_led_data, conf);

	if (unlikely(READ_ONCE(g_lynx_arbiter.shutting_down)))
		return 0;

	led_dat->brightness = brightness;

	trans_level = (
		(((1 << led_dat->conf.trans_bits) - 1) * brightness
		+ (((1 << led_dat->conf.led_bits) - 1) / 2))
		/ ((1 << led_dat->conf.led_bits) - 1));

	if (brightness != 0 && trans_level == 0)
		trans_level = 1;

	led_debug_log(led_dat, brightness, trans_level);

#ifdef MET_USER_EVENT_SUPPORT
	if (enable_met_backlight_tag())
		output_met_backlight_tag(brightness);
#endif

	/*
	 * BLK-07, BLK-19, BLK-43: Evaluate lifecycle and pointer identity strictly inside
	 * arbiter_lock via lynx_arbiter_route_slider(). For secondary LEDs, serialize
	 * under arbiter_lock and respect thermal max_level clamp.
	 */
	if (!lynx_arbiter_route_slider(led_dat, trans_level)) {
		mutex_lock(&g_lynx_arbiter.arbiter_lock);
		if (READ_ONCE(g_lynx_arbiter.initialized) &&
		    !READ_ONCE(g_lynx_arbiter.shutting_down)) {
			int effective_level = min(trans_level, led_dat->conf.max_level);
			mtk_led_pwm_set_hardware(led_dat, effective_level);
			led_dat->last_level = effective_level;
		}
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
	}

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
	call_notifier(1, led_dat);
#endif
#ifdef CONFIG_MTK_AAL_SUPPORT
	disp_pq_notify_backlight_changed(trans_level);
#endif
	return 0;

}


/****************************************************************************
 * add API for temperature control
 ***************************************************************************/

int setMaxBrightness(char *name, int percent, bool enable)
{
	struct mtk_led_data *led_dat;
	int max_l = 0, index = -1, limit_l = 0, cur_l = 0;

	/*
	 * BLK-38: Acquire arbiter_lock BEFORE accessing leds_info or shutting_down
	 * to eliminate lifecycle race conditions and UAF.
	 */
	mutex_lock(&g_lynx_arbiter.arbiter_lock);
	if (unlikely(!g_lynx_arbiter.initialized ||
		     g_lynx_arbiter.shutting_down ||
		     !leds_info)) {
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		return 0;
	}

	index = getLedDespIndex(name);
	if (index < 0) {
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		pr_notice("can not find leds by led_desp %s\n", name);
		return -1;
	}
	led_dat = container_of(leds_info->leds[index],
		struct mtk_led_data, desp);

	max_l = (1 << led_dat->conf.trans_bits) - 1;
	limit_l = (percent * max_l) / 100;
	limit_l = clamp(limit_l, 0, max_l);
	pr_info("before: name: %s, percent : %d, limit_l : %d, enable: %d\n",
		leds_info->leds[index]->name, percent, limit_l, enable);

	/*
	 * BLK-35 & BLK-43: Validate that led_dat strictly matches g_lynx_arbiter.led_dat.
	 * If it is a secondary LED (e.g. button, flash, notification), execute local
	 * secondary LED thermal throttling without modifying master arbiter state,
	 * keeping arbiter_lock held throughout hardware write (BLK-43).
	 */
	if (unlikely(!g_lynx_arbiter.led_dat ||
		     g_lynx_arbiter.led_dat != led_dat)) {
		if (enable) {
			led_dat->conf.max_level = limit_l;
			cur_l = min(led_dat->last_level, limit_l);
		} else {
			led_dat->conf.max_level = max_l;
			cur_l = led_dat->last_level;
		}
#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
		call_notifier(3, led_dat);
#endif
		if (led_dat->conf.cdev.brightness != 0)
			mtk_led_pwm_set_hardware(led_dat, cur_l);
		mutex_unlock(&g_lynx_arbiter.arbiter_lock);
		return 0;
	}

	if (enable) {
		g_lynx_arbiter.thermal_limited = true;
		g_lynx_arbiter.thermal_limit_level = limit_l;
		led_dat->conf.max_level = limit_l;
	} else {
		g_lynx_arbiter.thermal_limited = false;
		g_lynx_arbiter.thermal_limit_level = max_l;
		led_dat->conf.max_level = max_l;
	}

	/*
	 * BLK-33: Explicitly invalidate any pending Silky Brightness handoff
	 * via encapsulated arbiter helper whenever thermal limits change.
	 */
	lynx_arbiter_cancel_silky_pending_locked();

	/*
	 * BLK-21, BLK-28, BLK-31: Select the active owner's UNCLAMPED target
	 * brightness (aal_target_level, ccorr_target_level, legacy_target_level,
	 * or slider_target_level).
	 */
	switch (g_lynx_arbiter.active_owner) {
	case LYNX_OWNER_AAL:
		cur_l = g_lynx_arbiter.aal_target_level;
		break;
	case LYNX_OWNER_CCORR:
		cur_l = g_lynx_arbiter.ccorr_target_level;
		break;
	case LYNX_OWNER_LEGACY:
		cur_l = g_lynx_arbiter.legacy_target_level;
		break;
	case LYNX_OWNER_SLIDER:
	case LYNX_OWNER_WATCHDOG:
	case LYNX_OWNER_THERMAL:
	default:
		cur_l = g_lynx_arbiter.slider_target_level;
		break;
	}

	if (g_lynx_arbiter.thermal_limited && cur_l > g_lynx_arbiter.thermal_limit_level)
		cur_l = g_lynx_arbiter.thermal_limit_level;

	if (g_lynx_arbiter.active_owner == LYNX_OWNER_AAL)
		g_lynx_arbiter.aal_level = cur_l;
	else if (g_lynx_arbiter.active_owner == LYNX_OWNER_SLIDER ||
		 g_lynx_arbiter.active_owner == LYNX_OWNER_WATCHDOG)
		g_lynx_arbiter.slider_level = cur_l;

	g_lynx_arbiter.diag_thermal_commits++;

	if (cur_l > 0 ||
	    g_lynx_arbiter.last_applied_level != cur_l ||
	    led_dat->conf.level != cur_l) {
		g_lynx_arbiter.last_applied_level = cur_l;
		mtk_led_pwm_set_hardware(led_dat, cur_l);
		led_dat->last_level = cur_l;
	}
	mutex_unlock(&g_lynx_arbiter.arbiter_lock);

#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
	call_notifier(3, led_dat);
#endif

	pr_info("after: name: %s, cur_l : %d, max_level : %d",
		led_dat->conf.cdev.name, cur_l, led_dat->conf.max_level);
	return 0;

}
EXPORT_SYMBOL(setMaxBrightness);

static int led_data_init(struct device *dev, struct mtk_led_data *s_led)
{
	int ret;

	s_led->conf.cdev.default_trigger = s_led->info.config.default_trigger;
	s_led->conf.cdev.max_brightness = s_led->info.config.max_brightness;
	s_led->conf.cdev.flags = 0;
	s_led->conf.cdev.brightness_set_blocking = led_level_set;
	s_led->brightness = 0;
	s_led->conf.level = s_led->brightness;
	s_led->last_level = s_led->brightness;
	ret = devm_led_classdev_register(dev, &(s_led->conf.cdev));
	if (ret < 0) {
		pr_notice("led class register fail!");
		return ret;
	}
	pr_info("%s devm_led_classdev_register ok! ", s_led->conf.cdev.name);

	INIT_WORK(&s_led->work, mtk_led_work);
	ret = snprintf(s_led->debug.buffer + strlen(s_led->debug.buffer),
		4095 - strlen(s_led->debug.buffer),
		"[Light] Set %s directly ", s_led->conf.cdev.name);

	if (ret < 0 || ret >= 4096)
		pr_info("print log init error!");

	return 0;
}

static int led_pwm_config_add(struct device *dev,
		struct mtk_led_data *s_led, struct device_node *leds_np)
{
	struct pwm_args pargs;
	int ret = 0;

	if (leds_np != NULL)
		s_led->info.pwm = devm_of_pwm_get(dev, leds_np,
			s_led->info.config.name);
	else
		s_led->info.pwm = devm_pwm_get(dev, s_led->info.config.name);
	if (IS_ERR(s_led->info.pwm)) {
		ret = PTR_ERR(s_led->info.pwm);
		if (ret != -EPROBE_DEFER) {
			pr_notice("unable to request PWM for %s, err_code: %d\n",
				s_led->info.config.name, ret);
			goto err;
		}
	}

	pwm_apply_args(s_led->info.pwm);
	pwm_get_args(s_led->info.pwm, &pargs);

	s_led->info.config.pwm_period_ns = pargs.period;
	if (!s_led->info.config.pwm_period_ns && (pargs.period > 0))
		s_led->info.config.pwm_period_ns = pargs.period;

	pr_info("set led pwm OK! info.config.pwm_period_ns = %d!",
		s_led->info.config.pwm_period_ns);
	return ret;

 err:
	pr_notice("add pwm failed!\n");
	ret = -ENOMEM;
	return ret;

}

static int mtk_leds_parse_dt(struct device *dev,
		struct mtk_leds_info *m_leds)
{
	struct device_node *child;
	struct mtk_led_data *s_led;
	int ret = 0, num = 0, level = 0;
	const char *state;

	if (!dev->of_node) {
		pr_notice("Error load dts: node not exist!\n");
		return ret;
	}

	for_each_available_child_of_node(dev->of_node, child) {

		s_led = &(m_leds->leds[num]);
		ret = of_property_read_string(child, "label",
			&(s_led->conf.cdev.name));
		if (ret) {
			pr_info("Fail to read label property");
			goto out_led_dt;
		}
		ret = of_property_read_string(child, "pwm-names",
			&(s_led->info.config.name));
		if (ret) {
			pr_info("Fail to read pwm-names property");
			goto out_led_dt;
		}
		ret = of_property_read_string(child, "default-trigger",
			&(s_led->info.config.default_trigger));
		if (ret) {
			pr_info("Fail to read default-trigger property");
			s_led->info.config.default_trigger = NULL;
		}
		ret = of_property_read_u8(child, "active-low",
			&(s_led->info.config.active_low));
		if (ret)
			pr_info("Fail to read active-low property\n");
		ret = of_property_read_u32(child,
			"led-bits", &(s_led->conf.led_bits));
		if (ret) {
			pr_info("No led-bits, use default value 8");
			s_led->conf.led_bits = 8;
		}
		ret = of_property_read_u32(child,
			"max-brightness", &(s_led->info.config.max_brightness));
		if (ret) {
			pr_info("No max-brightness, use default value 255");
			s_led->info.config.max_brightness =
				(1 << s_led->conf.led_bits) - 1;
		}
		ret = of_property_read_u32(child,
			"trans-bits", &(s_led->conf.trans_bits));
		if (ret) {
			pr_info("No trans-bits, use default value 10");
			s_led->conf.trans_bits = 10;
		}
		s_led->conf.max_level = (1 << s_led->conf.trans_bits) - 1;
		ret = of_property_read_string(child, "default-state", &state);
		if (!ret) {
			if (!strcmp(state, "half"))
				level = s_led->info.config.max_brightness / 2;
			else if (!strcmp(state, "on"))
				level = s_led->info.config.max_brightness;
			else
				level = s_led->conf.level = 0;
		} else {
			level = s_led->info.config.max_brightness;
		}
		pr_info("parse %s(%d) leds dt: %s, %s, %d, %d, %d\n",
			s_led->conf.cdev.name, num, s_led->info.config.name,
			s_led->info.config.default_trigger,
			s_led->info.config.active_low,
			s_led->info.config.max_brightness,
			s_led->conf.led_bits);
		s_led->desp.index = num;
		strncpy(s_led->desp.name, s_led->conf.cdev.name,
			strlen(s_led->conf.cdev.name));
		s_led->desp.index = num;
		leds_info->leds[num] = &s_led->desp;
		s_led->conf.cdev.brightness = level;
		ret = led_data_init(dev, s_led);
		if (ret)
			goto out_led_dt;
		ret = led_pwm_config_add(dev, s_led, child);
		if (ret)
			goto out_led_dt;

		/*
		 * BLK-09 & BLK-22: Match strictly by the canonical "lcd-backlight"
		 * device name (do not use num == 0) and mark s_led->parent = m_leds
		 * ONLY after led_data_init, led_pwm_config_add, and
		 * lynx_arbiter_init have all succeeded.
		 */
		if (!strcmp(s_led->conf.cdev.name, "lcd-backlight")) {
			ret = lynx_arbiter_init(s_led,
				(1 << s_led->conf.trans_bits) - 1);
			if (ret < 0) {
				pr_notice("Failed to init Lynx Arbiter: %d\n", ret);
				goto out_led_dt;
			}
			/*
			 * BLK-45: Set initial boot hardware level directly without
			 * invoking led_level_set(), preserving LYNX_OWNER_LEGACY
			 * until actual user slider interaction.
			 */
			s_led->parent = m_leds;
			s_led->conf.level = level;
			mtk_led_pwm_set_hardware(s_led, level);
			mtk_led_pwm_commit_level(s_led, level, true);
			g_lynx_arbiter.legacy_target_level = level;
			g_lynx_arbiter.last_applied_level = level;
		} else {
			s_led->parent = m_leds;
			led_level_set(&s_led->conf.cdev, level);
		}
		num++;
	}
	m_leds->nums = num;
	pr_info("load dts ok!");
	return ret;
out_led_dt:
	pr_notice("Error load dts node!\n");
	of_node_put(child);
	return ret;
}


/****************************************************************************
 * driver functions
 ***************************************************************************/

static int mtk_leds_probe(struct platform_device *pdev)
{

	struct device *dev = &pdev->dev;
	struct mtk_leds_info *m_leds;
	int ret, nums;

	pr_info("probe begain +++");

	nums = of_get_child_count(dev->of_node);
	pr_info("Load dts node nums: %d", nums);
	m_leds = devm_kzalloc(dev, (sizeof(struct mtk_leds_info) +
		(sizeof(struct mtk_led_data) * (nums))), GFP_KERNEL);
	if (!m_leds)
		goto err;

	leds_info = devm_kzalloc(dev, (sizeof(struct leds_desp_info) +
		sizeof(struct led_desp *) * (nums)),
		GFP_KERNEL);
	leds_info->lens = nums;
	if (!leds_info) {
		ret = -ENOMEM;
		goto err;
	}

	platform_set_drvdata(pdev, m_leds);
	m_leds->dev = dev;
	ret = mtk_leds_parse_dt(&(pdev->dev), m_leds);
	if (ret) {
		pr_notice("Failed to parse devicetree!\n");
		lynx_arbiter_deinit();
		leds_info = NULL;
		platform_set_drvdata(pdev, NULL);
		goto err;
	}

	pr_info("probe end ---");
	return 0;
 err:
	pr_notice("Failed to probe!\n");
	leds_info = NULL;
	platform_set_drvdata(pdev, NULL);
	ret = -ENOMEM;
	return ret;
}

static int mtk_leds_remove(struct platform_device *pdev)
{
	int i;
	struct mtk_leds_info *m_leds = platform_get_drvdata(pdev);

	/*
	 * BLK-38: Deinitialize Lynx Arbiter first under lock; cancels watchdog
	 * synchronously while initialized flag is still recorded as true.
	 */
	lynx_arbiter_deinit();

	/* Atomically nullify leds_info and ensure no in-flight brightness setters proceed */
	mutex_lock(&g_lynx_arbiter.arbiter_lock);
	leds_info = NULL;
	mutex_unlock(&g_lynx_arbiter.arbiter_lock);

	if (!m_leds)
		return 0;

	/* Step 2: Unregister LED class devices that completed full initialization */
	for (i = 0; i < m_leds->nums; i++) {
		if (!m_leds->leds[i].parent)
			continue;
		led_classdev_unregister(&m_leds->leds[i].conf.cdev);
		cancel_work_sync(&m_leds->leds[i].work);
		mtk_led_pwm_disable_hardware(&(m_leds->leds[i]));
		m_leds->leds[i].parent = NULL;
	}
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, m_leds);

	return 0;
}

static void mtk_leds_shutdown(struct platform_device *pdev)
{
	int i;
	struct mtk_leds_info *m_leds = platform_get_drvdata(pdev);

	pr_info("Turn off backlight\n");

	/*
	 * BLK-38: Deinitialize Lynx Arbiter first under lock; cancels watchdog
	 * synchronously while initialized flag is still recorded as true.
	 */
	lynx_arbiter_deinit();

	mutex_lock(&g_lynx_arbiter.arbiter_lock);
	leds_info = NULL;
	mutex_unlock(&g_lynx_arbiter.arbiter_lock);

	for (i = 0; m_leds && i < m_leds->nums; i++) {
		if (!m_leds->leds[i].parent)
			continue;
#ifdef CONFIG_LEDS_BRIGHTNESS_CHANGED
		/* BLK-36: Preserve shutdown notification event for kernel subsystems */
		call_notifier(2, &(m_leds->leds[i]));
#endif
		mtk_led_pwm_set_hardware(&(m_leds->leds[i]), 0);
		mtk_led_pwm_disable_hardware(&(m_leds->leds[i]));
	}
}

static const struct of_device_id of_mtk_pwm_leds_match[] = {
	{ .compatible = "mediatek,pwm-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_mtk_pwm_leds_match);

static struct platform_driver mtk_pwm_leds_driver = {
	.driver = {
		   .name = "mtk-pwm-leds",
		   .owner = THIS_MODULE,
		   .of_match_table = of_mtk_pwm_leds_match,
		   },
	.probe = mtk_leds_probe,
	.remove = mtk_leds_remove,
	.shutdown = mtk_leds_shutdown,

};

static int __init mtk_leds_init(void)
{
	int ret;

	pr_info("Leds init\n");
	ret = platform_driver_register(&mtk_pwm_leds_driver);

	if (ret) {
		pr_info("driver register error: %d\n", ret);
		return ret;
	}

	return ret;
}

static void __exit mtk_leds_exit(void)
{
	platform_driver_unregister(&mtk_pwm_leds_driver);
}

/* delay leds init, for (1)display has delayed to use clock upstream.
 * (2)to fix repeat switch battary and power supply caused BL KE issue,
 * battary calling bl .shutdown whitch need to call disp_pwm and display
 * function and they not yet probe.
 */
late_initcall(mtk_leds_init);
module_exit(mtk_leds_exit);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("MTK Disp PWM Backlight Driver");
MODULE_LICENSE("GPL");


