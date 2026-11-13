#ifndef _LYNX_POWER_H_
#define _LYNX_POWER_H_

#include <linux/types.h>
#include <linux/compiler.h>

extern bool g_lynx_bypass_active;
extern bool g_lynx_full_bypass_enabled;
extern bool g_lynx_full_bypass_latched;
extern bool g_lynx_hyper_charge;
extern bool g_lynx_thermal_silencer;

static inline bool lynx_is_manual_bypass_active(void)
{
	return READ_ONCE(g_lynx_bypass_active);
}

static inline bool lynx_is_full_bypass_enabled(void)
{
	return READ_ONCE(g_lynx_full_bypass_enabled);
}

static inline bool lynx_is_full_bypass_active(void)
{
	return READ_ONCE(g_lynx_full_bypass_enabled) &&
	       READ_ONCE(g_lynx_full_bypass_latched);
}

static inline bool lynx_is_bypass_effective(void)
{
	return READ_ONCE(g_lynx_bypass_active) ||
	       (READ_ONCE(g_lynx_full_bypass_enabled) &&
		READ_ONCE(g_lynx_full_bypass_latched));
}

void lynx_bypass_on_plug_out(void);
void lynx_bypass_set_full_latch(bool latch);
void lynx_bypass_clear_manual_emergency(void);

int rt9471_set_bypass_mode(bool enable);
int rt9471_lynx_get_hw_info(bool *hz, bool *chg_en, bool *chg_done,
			    bool *vbus_gd, u32 *aicr_ua);
void lynx_sync_bypass_charger(bool entering_bypass);
void lynx_sync_hyper_hv(bool en);

#endif /* _LYNX_POWER_H_ */
