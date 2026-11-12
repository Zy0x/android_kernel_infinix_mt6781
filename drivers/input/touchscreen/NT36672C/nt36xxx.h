/*
 * Copyright (C) 2010 - 2018 Novatek, Inc.
 *
 * $Revision: 46000 $
 * $Date: 2019-06-12 14:25:52 +0800 (週三, 12 六月 2019) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#ifndef _LINUX_NVT_TOUCH_H
#define	_LINUX_NVT_TOUCH_H

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "nt36xxx_mem_map.h"

#ifdef CONFIG_MTK_SPI
/* Please copy mt_spi.h file under mtk spi driver folder */
#include "mt_spi.h"
#endif

#ifdef CONFIG_SPI_MT65XX
#include <linux/platform_data/spi-mt65xx.h>
#endif

#define NVT_DEBUG 1

//---GPIO number---
#define NVTTOUCH_RST_PIN 980
#define NVTTOUCH_INT_PIN 943


//---INT trigger mode---
//#define IRQ_TYPE_EDGE_RISING 1
//#define IRQ_TYPE_EDGE_FALLING 2
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING

//---GPIO pin mode---
#define PINCTRL_STATE_SPI_DEFAULT   "gt9896s_spi_mode"

//---SPI driver info.---
#define NVT_SPI_NAME "novatek,NVT-ts-spi"

#if NVT_DEBUG
#define NVT_LOG(fmt, args...)    pr_info("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#else
#define NVT_LOG(fmt, args...)    pr_debug("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)
#endif
#define NVT_ERR(fmt, args...)    pr_info("[%s] %s %d: " fmt, NVT_SPI_NAME, __func__, __LINE__, ##args)

//---Input device info.---
#define NVT_TS_NAME "mtk-tpd"


//---Touch info.---
#define TOUCH_DEFAULT_MAX_WIDTH 1080
#define TOUCH_DEFAULT_MAX_HEIGHT 2460
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0
#if TOUCH_KEY_NUM > 0
extern const uint16_t touch_key_array[TOUCH_KEY_NUM];
#endif
#define TOUCH_FORCE_NUM 1000

/* Enable only when module have tp reset pin and connected to host */
#define NVT_TOUCH_SUPPORT_HW_RST 0

//---Customerized func.---
#define NVT_TOUCH_PROC 1
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_MP 1
#define MT_PROTOCOL_B 1
#define WAKEUP_GESTURE 1
#if WAKEUP_GESTURE
extern const uint16_t gesture_key_array[];
#endif
#define BOOT_UPDATE_FIRMWARE 1
#define BOOT_UPDATE_FIRMWARE_NAME novatek_firmware
#define MP_UPDATE_FIRMWARE_NAME   "novatek_ts_mp.bin"
#define POINT_DATA_CHECKSUM 1
#define POINT_DATA_CHECKSUM_LEN 65
extern char novatek_firmware[25];

//---ESD Protect.---
#define NVT_TOUCH_ESD_PROTECT 0
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500	/* ms */
#define NVT_TOUCH_WDT_RECOVERY 1

struct nvt_ts_data {
	struct spi_device *client;
	struct input_dev *input_dev;
	struct delayed_work nvt_fwu_work;
	struct kthread_delayed_work nvt_fwu_dw;
	uint16_t addr;
	int8_t phys[32];
#if defined(CONFIG_FB)
#ifdef _MSM_DRM_NOTIFY_H_
	struct notifier_block drm_notif;
#else
	struct notifier_block fb_notif;
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	uint8_t fw_ver;
	uint8_t x_num;
	uint8_t y_num;
	uint16_t abs_x_max;
	uint16_t abs_y_max;
	uint8_t max_touch_num;
	uint8_t max_button_num;
	uint32_t int_trigger_type;
	int32_t irq_gpio;
	uint32_t irq_flags;
	int32_t reset_gpio;
	uint32_t reset_flags;
	struct mutex lock;
	const struct nvt_ts_mem_map *mmap;
	uint8_t carrier_system;
	uint8_t hw_crc;
	uint16_t nvt_pid;
	uint8_t rbuf[1025];
	uint8_t *xbuf;
	struct mutex xbuf_lock;
	bool irq_enabled;
	bool game_mode_enabled;
	bool hopping_fix_freq;
	bool has_jitter_level;
	uint8_t jitter_level;
	bool has_smooth_level;
	uint8_t smooth_level;
	uint8_t last_cmd;
	uint8_t last_cmd_sub;
	uint8_t last_cmd_val;
	uint8_t last_cmd_ack;
	int32_t last_cmd_ret;
	uint32_t spi_max_hz_req;
	u64 irq_cnt;
	u64 spi_err_cnt;
	u64 csum_err_cnt;
	u64 frame_cnt;
	u64 dup_xy_cnt;
	u64 micro_move_cnt;
	ktime_t last_frame_kt;
	u32 dt_us_last;
	u32 dt_us_min;
	u32 dt_us_max;
	uint32_t last_x;
	uint32_t last_y;
	int32_t last_dx;
	int32_t last_dy;
	bool last_xy_valid;
#ifdef CONFIG_MTK_SPI
	struct mt_chip_conf spi_ctrl;
#endif
#ifdef CONFIG_SPI_MT65XX
	struct mtk_chip_config spi_ctrl;
#endif
};

#if NVT_TOUCH_PROC
struct nvt_flash_data {
	rwlock_t lock;
};
#endif

typedef enum {
	RESET_STATE_INIT = 0xA0,	// IC reset
	RESET_STATE_REK,	// ReK baseline
	RESET_STATE_REK_FINISH,	// baseline is ready
	RESET_STATE_NORMAL_RUN,	// normal run
	RESET_STATE_MAX = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
	EVENT_MAP_HOST_CMD = 0x50,
	EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE = 0x51,
	EVENT_MAP_RESET_COMPLETE = 0x60,
	EVENT_MAP_FWINFO = 0x78,
	EVENT_MAP_PROJECTID = 0x9A,
} SPI_EVENT_MAP;

/* Novatek extended host commands (register EVENT_MAP_HOST_CMD = 0x50) */
#define EVENTBUFFER_HOPPING_POLLING_ON   0x73
#define EVENTBUFFER_HOPPING_POLLING_OFF  0x74
#define EVENTBUFFER_HOPPING_FIX_FREQ_ON  0x75
#define EVENTBUFFER_HOPPING_FIX_FREQ_OFF 0x76
#define EVENTBUFFER_HS_PLUG_IN           0x77
#define EVENTBUFFER_HS_PLUG_OUT          0x78
#define EVENTBUFFER_EDGE_LIMIT_VERTICAL  0x7A
#define EVENTBUFFER_EDGE_LIMIT_LEFT_UP   0x7B
#define EVENTBUFFER_EDGE_LIMIT_RIGHT_UP  0x7C
#define EVENTBUFFER_GAME_ON              0x7D
#define EVENTBUFFER_GAME_OFF             0x7E
#define EVENTBUFFER_EXT_CMD              0x7F
#define EVENTBUFFER_EXT_JITTER_LEVEL     0x0C
#define EVENTBUFFER_EXT_SMOOTH_LEVEL     0x0D

//---SPI READ/WRITE---
#define SPI_WRITE_MASK(a)	(a | 0x80)
#define SPI_READ_MASK(a)	(a & 0x7F)

#define DUMMY_BYTES (1)
#define NVT_TRANSFER_LEN	(63*1024)

typedef enum {
	NVTWRITE = 0,
	NVTREAD = 1
} NVT_SPI_RW;

//---extern structures---
extern struct nvt_ts_data *ts;

//---extern functions---
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len);
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len);
void nvt_bootloader_reset(void);
void nvt_eng_reset(void);
void nvt_sw_reset(void);
void nvt_sw_reset_idle(void);
void nvt_boot_ready(void);
void nvt_bld_crc_enable(void);
void nvt_fw_crc_enable(void);
int32_t nvt_update_firmware(char *firmware_name);
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state);
int32_t nvt_get_fw_info(void);
int32_t nvt_clear_fw_status(void);
int32_t nvt_check_fw_status(void);
int32_t nvt_set_page(uint32_t addr);
int32_t nvt_write_addr(uint32_t addr, uint8_t data);
int32_t nvt_send_host_cmd(uint8_t cmd, uint8_t subcmd, bool has_subcmd,
			  uint8_t val, bool has_val, uint8_t *ack_out);
int32_t nvt_set_game_mode(bool enable);
int32_t nvt_set_spi_speed(uint32_t hz);
int32_t nvt_read_event_regs(uint8_t start_reg, uint8_t *out, uint16_t len);
void nvt_reset_touch_stats(void);
void nvt_irq_enable(bool enable);
extern uint8_t bTouchIsAwake;
#if NVT_TOUCH_ESD_PROTECT
extern void nvt_esd_check_enable(uint8_t enable);
#endif				/* #if NVT_TOUCH_ESD_PROTECT */
#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif
#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif
#if BOOT_UPDATE_FIRMWARE
static struct kthread_worker *nvt_fwu_worker;
extern void Boot_Update_Firmware(struct kthread_work *work);
#endif

#endif				/* _LINUX_NVT_TOUCH_H */
