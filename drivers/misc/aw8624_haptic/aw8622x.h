/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AW8622X_H_
#define _AW8622X_H_

#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/leds.h>
#include "awinic_haptic.h"

#define AW8622X_SEQUENCER_SIZE		8
#define AW8622X_SEQUENCER_LOOP_SIZE	4
#define AW8622X_TRIG_NUM		3
#define AW8622X_VBAT_REFER		4200
#define AW8622X_VBAT_MIN		3000
#define AW8622X_VBAT_MAX		5500

enum aw8622x_haptic_work_mode {
	AW8622X_HAPTIC_STANDBY_MODE = 0,
	AW8622X_HAPTIC_RAM_MODE = 1,
	AW8622X_HAPTIC_RTP_MODE = 2,
	AW8622X_HAPTIC_TRIG_MODE = 3,
	AW8622X_HAPTIC_CONT_MODE = 4,
	AW8622X_HAPTIC_RAM_LOOP_MODE = 5,
};

enum aw8622x_haptic_activate_mode {
	AW8622X_HAPTIC_ACTIVATE_RAM_MODE = 0,
	AW8622X_HAPTIC_ACTIVATE_CONT_MODE = 1,
};

enum aw8622x_haptic_ram_vbat_comp_mode {
	AW8622X_HAPTIC_RAM_VBAT_COMP_DISABLE = 0,
	AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE = 1,
};

struct aw8622x_trig {
	unsigned char trig_level;
	unsigned char trig_polar;
	unsigned char pos_enable;
	unsigned char pos_sequence;
	unsigned char neg_enable;
	unsigned char neg_sequence;
	unsigned char trig_brk;
};

struct aw8622x_dts_info {
	unsigned int mode;
	unsigned int f0_pre;
	unsigned int f0_cali_percen;
	unsigned int cont_drv1_lvl;
	unsigned int cont_drv2_lvl;
	unsigned int cont_drv1_time;
	unsigned int cont_drv2_time;
	unsigned int cont_drv_width;
	unsigned int cont_wait_num;
	unsigned int cont_brk_time;
	unsigned int cont_track_margin;
	unsigned int cont_tset;
	unsigned int cont_bemf_set;
	unsigned int cont_brk_gain;
	unsigned int d2s_gain;
	unsigned int prctmode[3];
	unsigned int sine_array[4];
	unsigned int trig_config[21];
	unsigned int duration_time[3];
	bool is_enabled_auto_bst;
};

struct aw8622x_ram {
	unsigned int len;
	unsigned int check_sum;
	unsigned int base_addr;
	unsigned char version;
	unsigned char ram_shift;
	unsigned char baseaddr_shift;
	unsigned char ram_num;
};

struct aw8622x_container {
	int len;
	unsigned char data[];
};

struct aw8622x {
	u32 magic;
	struct i2c_client *i2c;
	struct device *dev;
	struct mutex lock;
	struct mutex rtp_lock;
	struct hrtimer timer;
	struct work_struct vibrator_work;
	struct work_struct rtp_work;
	struct delayed_work ram_work;
	struct led_classdev cdev;

	struct aw8622x_trig trig[AW8622X_TRIG_NUM];
	struct aw8622x_dts_info dts_info;
	struct aw8622x_ram ram;

	int reset_gpio;
	int irq_gpio;

	unsigned char chip_name;
	unsigned char play_mode;
	unsigned char activate_mode;
	unsigned char ram_vbat_comp;
	unsigned char ram_init;
	unsigned char rtp_init;
	unsigned char is_used_intn;
	unsigned char standby_flag;
	unsigned char fw_retry_cnt;

	int state;
	int duration;
	int amplitude;
	int index;
	int gain;
	int f0_value;

	unsigned int f0;
	unsigned int cont_f0;
	unsigned int cont_drv1_lvl;
	unsigned int cont_drv2_lvl;
	unsigned int cont_brk_time;
	unsigned int cont_wait_num;
	unsigned int cont_drv1_time;
	unsigned int cont_drv2_time;
	unsigned int vbat;
	unsigned int lra;
	unsigned int f0_cali_data;
	unsigned int osc_cali_data;
	unsigned int rtp_cnt;
	unsigned int rtp_file_num;
	unsigned int gun_type;
	unsigned int bullet_nr;

	unsigned char seq[AW8622X_SEQUENCER_SIZE];
	unsigned char loop[AW8622X_SEQUENCER_LOOP_SIZE * 2];

	bool f0_cali_needed;
	struct aw8622x_container *rtp_container;
};

#endif
