/*
 * Copyright (c) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_DISP_AAL_H__
#define __MTK_DISP_AAL_H__

#include <linux/uaccess.h>
#include <drm/mediatek_drm.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/compiler.h>

#define AAL_SERVICE_FORCE_UPDATE 0x1
/*******************************/
/* field definition */
/* ------------------------------------------------------------- */
/* AAL */
#define DISP_AAL_EN                             (0x000)
#define DISP_AAL_RESET                          (0x004)
#define DISP_AAL_INTEN                          (0x008)
#define DISP_AAL_INTSTA                         (0x00c)
#define DISP_AAL_STATUS                         (0x010)
#define DISP_AAL_CFG                            (0x020)
#define DISP_AAL_IN_CNT                         (0x024)
#define DISP_AAL_OUT_CNT                        (0x028)
#define DISP_AAL_CHKSUM                         (0x02c)
#define DISP_AAL_SIZE                           (0x030)
#define DISP_AAL_SHADOW_CTL                     (0x0B0)
#define DISP_AAL_DUMMY_REG                      (0x0c0)
#define DISP_AAL_SHADOW_CTRL                    (0x0f0)
#define AAL_BYPASS_SHADOW	BIT(0)
#define AAL_READ_WRK_REG	BIT(2)
#define DISP_AAL_MAX_HIST_CONFIG_00             (0x204)
#define DISP_AAL_CABC_00                        (0x20c)
#define DISP_AAL_CABC_02                        (0x214)
#define DISP_AAL_CABC_04                        (0x21c)
#define DISP_AAL_STATUS_00                      (0x224)
/* 00 ~ 32: max histogram */
#define DISP_AAL_STATUS_32                      (0x2a4)
/* bit 8: dre_gain_force_en */
#define DISP_AAL_DRE_GAIN_FILTER_00             (0x354)
#define DISP_AAL_DRE_FLT_FORCE(idx) \
	(0x358 + (idx) * 4)
#define DISP_AAL_DRE_CRV_CAL_00                 (0x344)
#define DISP_AAL_DRE_MAPPING_00                 (0x3b4)
#define DISP_AAL_CABC_GAINLMT_TBL(idx) \
	(0x410 + (idx) * 4)

#define DISP_AAL_DBG_CFG_MAIN                   (0x45c)

/* DRE 3.0 */
#define DISP_AAL_CFG_MAIN                       (0x200)
#define DISP_AAL_SRAM_CFG                       (0x0c4)
#define DISP_AAL_SRAM_STATUS                    (0x0c8)
#define DISP_AAL_SRAM_RW_IF_0                   (0x0cc)
#define DISP_AAL_SRAM_RW_IF_1                   (0x0d0)
#define DISP_AAL_SRAM_RW_IF_2                   (0x0d4)
#define DISP_AAL_SRAM_RW_IF_3                   (0x0d8)
#define DISP_AAL_WIN_X_MAIN                     (0x460)
#define DISP_AAL_WIN_Y_MAIN                     (0x464)
#define DISP_AAL_DRE_BLOCK_INFO_00              (0x468)
#define DISP_AAL_DRE_BLOCK_INFO_01              (0x46c)
#define DISP_AAL_DRE_BLOCK_INFO_02              (0x470)
#define DISP_AAL_DRE_BLOCK_INFO_03              (0x474)
#define DISP_AAL_DRE_BLOCK_INFO_04              (0x478)
#define DISP_AAL_DRE_CHROMA_HIST_00             (0x480)
#define DISP_AAL_DRE_CHROMA_HIST_01             (0x484)
#define DISP_AAL_DRE_ALPHA_BLEND_00             (0x488)
#define DISP_AAL_DRE_BITPLUS_00                 (0x48c)
#define DISP_AAL_DRE_BITPLUS_01                 (0x490)
#define DISP_AAL_DRE_BITPLUS_02                 (0x494)
#define DISP_AAL_DRE_BITPLUS_03                 (0x498)
#define DISP_AAL_DRE_BITPLUS_04                 (0x49c)
#define DISP_AAL_DRE_BLOCK_INFO_05              (0x4b4)
#define DISP_AAL_DRE_BLOCK_INFO_06              (0x4b8)
#define DISP_AAL_DUAL_PIPE_INFO_00              (0x4d0)
#define DISP_AAL_DUAL_PIPE_INFO_01              (0x4d4)
#define DISP_AAL_OUTPUT_SIZE                    (0x4d8)
#define DISP_AAL_OUTPUT_OFFSET                  (0x4dc)

#define DISP_Y_HISTOGRAM_00                     (0x504)

#define DISP_AAL_DRE_BLOCK_INFO_07              (0x0f8)
#define MDP_AAL_TILE_00				(0x4EC)
#define MDP_AAL_TILE_01				(0x4F0)
#define MDP_AAL_TILE_02				(0x0F4)

#define MDP_AAL_DUAL_PIPE00				(0x500)
#define MDP_AAL_DUAL_PIPE08				(0x544)
#define AAL_DRE30_GAIN_REGISTER_NUM		(544)
#define AAL_DRE30_HIST_REGISTER_NUM		(768)

#define AAL_U32_PTR(x) ((unsigned int *)(unsigned long)x)
#define aal_u32_handle_t unsigned long long

enum AAL_ESS_UD_MODE {
	CONFIG_BY_CUSTOM_LIB = 0,
	CONFIG_TO_LCD = 1,
	CONFIG_TO_AMOLED = 2
};

enum AAL_DRE_MODE {
	DRE_EN_BY_CUSTOM_LIB = 0xFFFF,
	DRE_OFF = 0,
	DRE_ON = 1
};

enum AAL_ESS_MODE {
	ESS_EN_BY_CUSTOM_LIB = 0xFFFF,
	ESS_OFF = 0,
	ESS_ON = 1
};

enum AAL_ESS_LEVEL {
	ESS_LEVEL_BY_CUSTOM_LIB = 0xFFFF
};

enum DISP_AAL_REFRESH_LATENCY {
	AAL_REFRESH_17MS = 17,
	AAL_REFRESH_33MS = 33
};

struct DISP_DRE30_HIST {
	unsigned int aal0_dre_hist[AAL_DRE30_HIST_REGISTER_NUM];
	unsigned int aal1_dre_hist[AAL_DRE30_HIST_REGISTER_NUM];
	int dre_blk_x_num;
	int dre_blk_y_num;
};

struct DISP_DRE30_PARAM {
	unsigned int dre30_gain[AAL_DRE30_GAIN_REGISTER_NUM];
};

void disp_aal_debug(const char *opt);

/* Provide for LED */
void disp_aal_notify_backlight_changed(int bl_1024);

void disp_aal_on_start_of_frame(void);

/* AAL Control API in Kernel */
void disp_aal_set_lcm_type(unsigned int panel_type);
void disp_aal_set_ess_level(int level);
void disp_aal_set_ess_en(int enable);
void disp_aal_set_dre_en(int enable);

/*
 * BLK-COMPILE-03: Forward declarations of DRM structures required before
 * AAL IOCTL prototypes so non-DRM translation units (e.g. leds-mtk-pwm.c)
 * do not trigger -Wvisibility warnings.
 */
struct drm_device;
struct drm_file;

int mtk_drm_ioctl_aal_eventctl(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
int mtk_drm_ioctl_aal_get_hist(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
int mtk_drm_ioctl_aal_init_reg(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
int mtk_drm_ioctl_aal_init_dre30(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
int mtk_drm_ioctl_aal_get_size(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
int mtk_drm_ioctl_aal_set_param(struct drm_device *dev, void *data,
	struct drm_file *file_priv);

/*
 * Forward declarations. struct mtk_led_data is intentionally OPAQUE outside
 * leds-mtk-pwm.c (BLK-22); arbiter code must only use accessor helpers.
 */
struct mtk_led_data;
struct drm_device;
struct drm_file;
struct drm_crtc;
struct mtk_ddp_comp;
struct file;
struct task_struct;

/*
 * BLK-39: Producer-Consumer FIFO Queue for Per-Transaction Silky Provenance.
 * Each SET_PARAM (with silky_bright_flag != 0) enqueues an immutable transaction
 * token record at silky_fifo_tail. Each Silky SET_CCORR consumer pops the oldest
 * unconsumed record at silky_fifo_head, preventing a newer SET_PARAM from
 * overwriting the provenance token of a delayed older SET_CCORR callback.
 */
#define LYNX_SILKY_FIFO_DEPTH 16

struct lynx_silky_tx_item {
	u64 tx_id;
	u64 seq;
	u32 epoch;
	u64 open_id;
	pid_t tid;
	int backlight;
};

/* Lynx Unified Arbiter: Per-File DRM Context (BLK-25, BLK-30, BLK-39, BLK-40, BLK-41, BLK-42) */
struct lynx_drm_file_ctx {
	u64 drm_open_id;              /* Monotonically increasing unique 64-bit ID */
	u32 daemon_epoch;             /* Snapshot of g_aal_daemon_epoch at registration */
	u32 daemon_secid;             /* SELinux task secid bound at authenticated INIT_REG (BLK-30, BLK-41) */
	pid_t daemon_tid;             /* Dedicated AALMain thread TID bound at INIT_REG (BLK-40) */
	u64 fetched_seq;              /* Published sequence bound after drm_ioctl copy_to_user (BLK-20) */
	u64 staged_seq;               /* Staged sequence captured inside GET_HIST before copy_to_user */
	u64 fetch_gen;                /* Fetch generation counter for active reservation (BLK-42) */
	struct lynx_silky_tx_item silky_fifo[LYNX_SILKY_FIFO_DEPTH]; /* Per-tx FIFO queue (BLK-39) */
	u32 silky_fifo_head;          /* Consumer pop index under g_aal_hist_lock (BLK-39) */
	u32 silky_fifo_tail;          /* Producer push index under g_aal_hist_lock (BLK-39) */
	bool fetch_in_progress;       /* True while GET_HIST copy_to_user is in-flight (BLK-20) */
	struct task_struct *fetch_owner; /* Task that staged the in-flight GET_HIST (BLK-20/23) */
	bool fetched_valid;           /* True ONLY after drm_ioctl copy_to_user succeeds (BLK-12/14/20) */
	struct pid *tgid;             /* Process TGID struct pid (holds reference) */
	bool is_aal_daemon;           /* True if this FD successfully executed synchronous INIT_REG */
	struct rcu_head rcu;          /* RCU head for safe, non-blocking deallocation */
};

/*
 * Lynx Unified Arbiter: Full Backlight Authority Ownership Spectrum (BLK-19, BLK-26, BLK-34)
 * Every physical PWM backlight mutation on X698 must pass through the arbiter
 * under g_lynx_arbiter.arbiter_lock and attribute ownership to one of these
 * six canonical sources.
 */
enum lynx_arbiter_owner {
	LYNX_OWNER_SLIDER   = 0,
	LYNX_OWNER_AAL      = 1,
	LYNX_OWNER_CCORR    = 2,
	LYNX_OWNER_THERMAL  = 3,
	LYNX_OWNER_LEGACY   = 4,
	LYNX_OWNER_WATCHDOG = 5,
};

/*
 * Lynx Unified Arbiter: Master State Structure
 *
 * Lock Ordering & Authority Model (V5.4.8):
 * 0. Transaction Outer Lock: g_aal_ioctl_lock (struct mutex in mtk_disp_aal.c)
 *    - Serializes AAL ioctl transactions (INIT_REG, GET_HIST, SET_PARAM),
 *      post-copy_to_user token publication (disp_aal_finalize_get_hist_delivery),
 *      global g_aal_param, and callback/GCE completion flags.
 * 1. Arbiter Transaction Lock: g_lynx_arbiter.arbiter_lock (struct mutex)
 *    - Held across the ENTIRE synchronous SET_PARAM hardware transaction:
 *        token validation -> synchronous GCE CMDQ flush & wait
 *        (mtk_crtc_user_cmd_sync -> cmdq_pkt_flush -> cmdq_pkt_wait_complete) ->
 *        GCE completion verification -> hardware PWM commit / Silky handoff.
 *    - Also held across all slider, CCORR Silky handoff, thermal, legacy, and
 *      watchdog backlight updates (BLK-17, BLK-18, BLK-19, BLK-26, BLK-28, BLK-30..34).
 * 2. Disjoint Inner / Leaf Locks (Never Nested Inside Each Other):
 *    - Level 2A: mtk_crtc->lock (struct mutex in mtk_drm_crtc.c, acquired and
 *      released inside mtk_crtc_user_cmd_sync() / mtk_crtc_user_cmd()).
 *    - Level 2B: leds_mutex (struct mutex in leds-mtk-pwm.c, acquired and
 *      released inside mtk_led_pwm_set_hardware()).
 *    - Level 2C: g_aal_hist_lock (spinlock_t, irqsave) — leaf spinlock
 *      protecting causal sequence variables (slider_seq, min_valid_seq,
 *      g_aal_hist_backlight_seq, ctx->staged_seq, ctx->fetch_in_progress,
 *      ctx->fetched_seq, ctx->fetched_valid, g_aal_hist_fetch_busy,
 *      silky_pending, silky_pending_seq, silky_pending_epoch,
 *      silky_pending_backlight, silky_handoff_gen, silky_deadline_jiffies),
 *      max_backlight, g_aal_hist, daemon secid (BLK-30), and daemon identity.
 *
 * Strict Nesting Hierarchy (Never Reversed):
 *   g_aal_ioctl_lock -> arbiter_lock -> mtk_crtc->lock
 *                    -> arbiter_lock -> g_aal_hist_lock
 *                    -> arbiter_lock -> leds_mutex
 */
struct lynx_brightness_arbiter {
	struct mutex arbiter_lock;         /* Master lock for arbiter state & transactions */
	enum lynx_arbiter_owner active_owner;

	/*
	 * BLK-21, BLK-28, BLK-31: Explicit Separation of Unclamped Target
	 * Brightness vs Thermally Clamped Applied Brightness.
	 * When thermal throttling is released (even from a 0% thermal cap),
	 * setMaxBrightness() unconditionally restores brightness from the
	 * active owner's unclamped *_target_level.
	 */
	int slider_target_level;           /* Unclamped user slider target (0..4095) */
	int aal_target_level;              /* Unclamped AAL FinalBacklight target (0..4095) */
	int ccorr_target_level;            /* Unclamped CCORR Silky target (0..4095) */
	int legacy_target_level;           /* Unclamped legacy workqueue target (0..4095) */
	int slider_level;                  /* Latest applied slider level (post-clamp) */
	int aal_level;                     /* Latest applied AAL level (post-clamp) */
	int last_applied_level;            /* Current physical hardware PWM level */
	int max_backlight;                 /* Physical maximum backlight (4095 on X698) */

	/* Causal Sequence Fencing (Protected by g_aal_hist_lock) */
	u64 slider_seq;                    /* Monotonic sequence for slider events */
	u64 min_valid_seq;                 /* Causal fence: minimum valid sequence */

	/*
	 * BLK-26 & BLK-33: Silky Brightness Handoff State (Protected by g_aal_hist_lock
	 * and serialized by arbiter_lock). Armed ONLY by mtk_drm_ioctl_aal_set_param()
	 * when silky_bright_flag != 0 after synchronous GCE CMDQ completion.
	 */
	bool silky_pending;                /* True when AAL handed off frame to CCORR */
	u64 silky_pending_seq;             /* Causal sequence bound to pending handoff */
	u32 silky_pending_epoch;           /* Daemon epoch bound to pending handoff */
	u64 silky_pending_open_id;         /* Bound DRM open_id for pending handoff (BLK-39) */
	pid_t silky_pending_tid;           /* Bound daemon thread TID for pending handoff (BLK-39) */
	int silky_pending_backlight;       /* Expected FinalBacklight from AAL SET_PARAM */
	u64 silky_handoff_gen;             /* Monotonic handoff generation counter (BLK-33) */
	u64 silky_pending_gen;             /* Explicit generation for pending handoff (BLK-39) */
	u64 silky_consumed_gen;            /* Last consumed handoff generation (BLK-39) */
	unsigned long silky_deadline_jiffies; /* Expiration deadline in jiffies (BLK-33) */

	/* Outdoor HBM Override (Protected by arbiter_lock) */
	bool hbm_override;                 /* True while outdoor HBM peak is held */

	/* Thermal Throttling Integration (Protected by arbiter_lock) */
	bool thermal_limited;              /* True when thermal throttling active */
	int thermal_limit_level;           /* Clamped upper bound during throttle */

	/* Watchdog & Safe Teardown (Protected by arbiter_lock) */
	struct delayed_work timeout_work;  /* 1500ms watchdog for daemon stalls */
	u64 watchdog_gen;                  /* Monotonic generation counter (BLK-06) */
	u64 armed_watchdog_gen;            /* Generation bound to currently armed timer */
	bool watchdog_armed;               /* True only when timer is actively armed */
	bool shutting_down;                /* Flag preventing operations during shutdown */
	bool initialized;                  /* True once lynx_arbiter_init succeeds */
	struct mtk_led_data *led_dat;      /* Pointer to physical PWM LED data */

	/* Telemetry & Diagnostic Counters */
	u64 diag_slider_commits;
	u64 diag_aal_commits;
	u64 diag_ccorr_commits;
	u64 diag_ccorr_rejects;
	u64 diag_thermal_commits;
	u64 diag_legacy_commits;
	u64 diag_legacy_rejects;
	u64 diag_auth_rejects;
	u64 diag_stale_seq_drops;
	u64 diag_watchdog_triggers;
	u64 diag_invalid_pid;
	u64 diag_daemon_inactive;
	u64 diag_invalid_epoch;
	u64 diag_out_of_range;
	u64 diag_cmdq_sync_failures;
};

extern struct lynx_brightness_arbiter g_lynx_arbiter;

/* Lynx Arbiter Public APIs */
int lynx_arbiter_init(struct mtk_led_data *led_dat, int max_bl);
void lynx_arbiter_deinit(void);
bool lynx_arbiter_route_slider(struct mtk_led_data *led_dat, int slider_level);
void lynx_arbiter_claim_ccorr_call_context(struct lynx_drm_file_ctx *ctx, unsigned long user_arg);
void lynx_arbiter_bind_ccorr_call_context(u64 open_id, u32 epoch, pid_t tid, u64 tx_id, u64 seq, int backlight);
void lynx_arbiter_unbind_ccorr_call_context(void);
void lynx_arbiter_get_current_ccorr_context(u64 *open_id, u32 *epoch, pid_t *tid, u64 *tx_id, u64 *seq, int *backlight);
bool lynx_arbiter_route_ccorr_silky(struct mtk_led_data *led_dat, int level, u64 open_id, u32 epoch, pid_t tid, u64 tx_id, u64 seq, int backlight);
bool lynx_arbiter_route_ccorr_silky_locked(struct mtk_led_data *led_dat, int level, u64 open_id, u32 epoch, pid_t tid, u64 tx_id, u64 seq, int backlight);
void lynx_arbiter_cancel_silky_pending_locked(void);
bool lynx_arbiter_route_legacy(struct mtk_led_data *led_dat, int level);
bool lynx_arbiter_route_legacy_locked(struct mtk_led_data *led_dat, int level);
void lynx_arbiter_notify_slider(int slider_level);
void lynx_arbiter_set_hbm(bool enable);
int lynx_arbiter_commit_aal_locked(struct lynx_drm_file_ctx *ctx,
				   int backlight_level,
				   u64 causal_seq,
				   u32 expected_epoch);
int disp_aal_validate_and_consume_token_locked(struct lynx_drm_file_ctx *ctx,
					       int backlight_value,
					       u64 *out_seq,
					       u32 *out_epoch);
void disp_aal_set_task_fetch_gen(u64 gen);
u64 disp_aal_get_task_fetch_gen(void);
void disp_aal_clear_task_fetch_gen(void);
void disp_aal_finalize_get_hist_delivery(struct drm_file *file_priv, int ioctl_ret, u64 req_fetch_gen);
void disp_aal_commit_daemon_registration(struct lynx_drm_file_ctx *ctx);
void disp_aal_unregister_daemon_fd(u64 open_id);
int mtk_crtc_user_cmd_sync(struct drm_crtc *crtc, struct mtk_ddp_comp *comp,
			   unsigned int cmd, void *params,
			   bool *out_cb_executed, bool *out_hw_completed);
int mtk_led_pwm_set_hardware(struct mtk_led_data *led_dat, int brightness);
void mtk_led_pwm_commit_level(struct mtk_led_data *led_dat, int level,
			      bool update_brightness);
int mtk_led_pwm_disable_hardware(struct mtk_led_data *led_dat);
int mtk_drm_open(struct drm_device *dev, struct drm_file *file_priv);
void mtk_drm_postclose(struct drm_device *dev, struct drm_file *file_priv);
long mtk_drm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#endif

