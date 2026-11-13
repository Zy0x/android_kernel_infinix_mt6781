#ifndef _LYNX_POWER_H_
#define _LYNX_POWER_H_

#include <linux/types.h>

extern bool g_lynx_bypass_active;
extern bool g_lynx_hyper_charge;
extern bool g_lynx_thermal_silencer;

int rt9471_set_bypass_mode(bool enable);
void lynx_sync_hyper_hv(bool en);

#endif /* _LYNX_POWER_H_ */
