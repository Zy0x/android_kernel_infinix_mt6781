// SPDX-License-Identifier: GPL-2.0
/*
 * Awinic AW8622x (AW86223 / AW86224 / AW86225) Linear LRA Haptic Driver
 * Reconstructed against Infinix Note 11S (X698 / MT6781) stock hardware contract
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include "aw8622x.h"
#include "aw8622x_reg.h"

#define AW8622X_DRIVER_VERSION		"v2.0.0-sol"
#define AW8622X_I2C_RETRIES		3
#define AW8622X_I2C_RETRY_DELAY		2
#define AW8622X_MAX_FIRMWARE_LOAD_CNT	20
#define AW8622X_RTP_SINGLE_MAX_NUM	512
#define AW8622X_READ_F0_COEFF		3840000U

static const char * const aw8622x_ram_name = "aw8622x_haptic.bin";

#define AW8622X_RTP_NAME_MAX		64
static const char aw8622x_rtp_name[][AW8622X_RTP_NAME_MAX] = {
	"aw8622x_osc_rtp_12K_10s.bin",
	"aw8622x_rtp.bin",
	"aw8622x_rtp_lighthouse.bin",
	"aw8622x_rtp_silk.bin",
};

static int aw8622x_i2c_read(struct aw8622x *aw8622x, unsigned char reg_addr,
			    unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8622X_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw8622x->i2c, reg_addr);
		if (ret >= 0) {
			*reg_data = (unsigned char)ret;
			return 0;
		}
		cnt++;
		usleep_range(AW8622X_I2C_RETRY_DELAY * 1000,
			     AW8622X_I2C_RETRY_DELAY * 1000 + 500);
	}

	dev_err(aw8622x->dev, "aw8622x: i2c read reg 0x%02x err=%d\n",
		reg_addr, ret);
	return ret;
}

static int aw8622x_i2c_write(struct aw8622x *aw8622x, unsigned char reg_addr,
			     unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW8622X_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw8622x->i2c, reg_addr, reg_data);
		if (ret >= 0)
			return 0;
		cnt++;
		usleep_range(AW8622X_I2C_RETRY_DELAY * 1000,
			     AW8622X_I2C_RETRY_DELAY * 1000 + 500);
	}

	dev_err(aw8622x->dev, "aw8622x: i2c write reg 0x%02x=0x%02x err=%d\n",
		reg_addr, reg_data, ret);
	return ret;
}

static int aw8622x_i2c_write_bits(struct aw8622x *aw8622x, unsigned char reg_addr,
				  unsigned int mask, unsigned char reg_data)
{
	unsigned char reg_val = 0;
	int ret;

	ret = aw8622x_i2c_read(aw8622x, reg_addr, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= (unsigned char)mask;
	reg_val |= (reg_data & (unsigned char)(~mask));
	return aw8622x_i2c_write(aw8622x, reg_addr, reg_val);
}

static int aw8622x_i2c_writes(struct aw8622x *aw8622x, unsigned char reg_addr,
			      unsigned char *buf, unsigned int len)
{
	int ret;
	unsigned char *data;

	data = kmalloc(len + 1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data[0] = reg_addr;
	memcpy(&data[1], buf, len);

	ret = i2c_master_send(aw8622x->i2c, data, len + 1);
	if (ret < 0)
		dev_err(aw8622x->dev, "aw8622x: i2c writes reg 0x%02x len=%u err=%d\n",
			reg_addr, len, ret);

	kfree(data);
	return ret < 0 ? ret : 0;
}

static void aw8622x_haptic_stop(struct aw8622x *aw8622x)
{
	unsigned char cnt = 40;
	unsigned char reg_val = 0;

	if (aw8622x->standby_flag)
		return;

	aw8622x->play_mode = AW8622X_HAPTIC_STANDBY_MODE;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG4, AW8622X_BIT_PLAYCFG4_STOP);

	while (cnt--) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBCFG4, &reg_val);
		reg_val &= AW8622X_BIT_GLBRD5_STATE_MASK;
		if (reg_val == AW8622X_BIT_GLBRD5_STATE_STANDBY ||
		    reg_val == AW8622X_BIT_GLBRD5_STATE_I2S_GO) {
			usleep_range(2000, 2500);
			aw8622x->standby_flag = 1;
			return;
		}
		usleep_range(2000, 2500);
	}

	dev_warn(aw8622x->dev, "aw8622x: force standby mode (glb=0x%02x)\n", reg_val);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
			       AW8622X_BIT_SYSCTRL3_STANDBY_MASK,
			       AW8622X_BIT_SYSCTRL3_STANDBY_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
			       AW8622X_BIT_SYSCTRL3_STANDBY_MASK,
			       AW8622X_BIT_SYSCTRL3_STANDBY_OFF);
	aw8622x->standby_flag = 1;
}

static void aw8622x_haptic_play_go(struct aw8622x *aw8622x)
{
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG4, AW8622X_BIT_PLAYCFG4_GO);
	udelay(2000);
	aw8622x->standby_flag = 0;
}

static void aw8622x_haptic_play_mode(struct aw8622x *aw8622x,
				     unsigned char play_mode)
{
	switch (play_mode) {
	case AW8622X_HAPTIC_STANDBY_MODE:
		aw8622x_haptic_stop(aw8622x);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
				       AW8622X_BIT_SYSCTRL3_WAIT_MODE_MASK, 0x02);
		break;
	case AW8622X_HAPTIC_RAM_MODE:
		aw8622x->play_mode = AW8622X_HAPTIC_RAM_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW8622X_HAPTIC_RAM_LOOP_MODE:
		aw8622x->play_mode = AW8622X_HAPTIC_RAM_LOOP_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RAM);
		break;
	case AW8622X_HAPTIC_RTP_MODE:
		aw8622x->play_mode = AW8622X_HAPTIC_RTP_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_RTP);
		break;
	case AW8622X_HAPTIC_CONT_MODE:
		aw8622x->play_mode = AW8622X_HAPTIC_CONT_MODE;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_MASK,
				       AW8622X_BIT_PLAYCFG3_PLAY_MODE_CONT);
		break;
	default:
		dev_err(aw8622x->dev, "aw8622x: invalid play_mode=%u\n", play_mode);
		break;
	}
}

static void aw8622x_haptic_set_gain(struct aw8622x *aw8622x, unsigned char gain)
{
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PLAYCFG2, gain);
}

static void aw8622x_haptic_set_wav_seq(struct aw8622x *aw8622x,
				       unsigned char wav, unsigned char seq)
{
	aw8622x_i2c_write(aw8622x, AW8622X_REG_WAVCFG1 + wav, seq);
}

static void aw8622x_haptic_set_wav_loop(struct aw8622x *aw8622x,
					unsigned char wav, unsigned char loop)
{
	unsigned char tmp = 0;

	if (wav % 2) {
		tmp = loop & 0x0F;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9 + (wav / 2),
				       0xF0, tmp);
	} else {
		tmp = (loop & 0x0F) << 4;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9 + (wav / 2),
				       0x0F, tmp);
	}
}

static void aw8622x_haptic_upload_lra(struct aw8622x *aw8622x, unsigned int flag)
{
	if (flag == 0) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK, 0x00);
	} else if (flag == 1) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK,
				       (unsigned char)aw8622x->f0_cali_data);
	} else if (flag == 2) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
				       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK,
				       (unsigned char)aw8622x->osc_cali_data);
	}
}

static int aw8622x_haptic_get_vbat(struct aw8622x *aw8622x)
{
	unsigned char reg_hi = 0, reg_lo = 0;
	unsigned int raw;

	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_VBAT_GO_MASK,
			       AW8622X_BIT_DETCFG2_VBAT_GO_EN);
	usleep_range(20000, 25000);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_VBAT, &reg_hi);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_LO, &reg_lo);

	raw = ((unsigned int)reg_hi << 2) | ((reg_lo >> 4) & 0x03);
	aw8622x->vbat = (raw * 6100U) >> 10;
	if (aw8622x->vbat > AW8622X_VBAT_MAX)
		aw8622x->vbat = AW8622X_VBAT_MAX;
	if (aw8622x->vbat < AW8622X_VBAT_MIN)
		aw8622x->vbat = AW8622X_VBAT_MIN;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
	return 0;
}

static void aw8622x_haptic_ram_vbat_comp(struct aw8622x *aw8622x, bool flag)
{
	int temp_gain;

	if (flag && aw8622x->ram_vbat_comp == AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE) {
		aw8622x_haptic_get_vbat(aw8622x);
		temp_gain = aw8622x->gain * AW8622X_VBAT_REFER / (int)aw8622x->vbat;
		if (temp_gain > 0xB3)
			temp_gain = 0xB3;
		aw8622x_haptic_set_gain(aw8622x, (unsigned char)temp_gain);
	} else {
		aw8622x_haptic_set_gain(aw8622x, (unsigned char)aw8622x->gain);
	}
}

static void aw8622x_haptic_cont_config(struct aw8622x *aw8622x)
{
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_CONT_MODE);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_TRACK_EN_MASK,
			       AW8622X_BIT_CONTCFG6_TRACK_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
			       (unsigned char)aw8622x->cont_drv1_lvl);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
			  (unsigned char)aw8622x->cont_drv2_lvl);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9, 0xFF);
	aw8622x_haptic_play_go(aw8622x);
}

static int aw8622x_haptic_read_lra_f0(struct aw8622x *aw8622x)
{
	unsigned char hi = 0, lo = 0;
	unsigned int f0_reg;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD14, &hi);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD15, &lo);
	f0_reg = ((unsigned int)hi << 8) | lo;
	if (!f0_reg) {
		aw8622x->f0 = aw8622x->dts_info.f0_pre;
		aw8622x->f0_value = 0;
		dev_err(aw8622x->dev, "aw8622x: f0_reg is 0\n");
		return -EINVAL;
	}
	aw8622x->f0 = AW8622X_READ_F0_COEFF / f0_reg;
	aw8622x->f0_value = (int)aw8622x->f0;
	return 0;
}

static int aw8622x_haptic_read_cont_f0(struct aw8622x *aw8622x)
{
	unsigned char hi = 0, lo = 0;
	unsigned int f0_reg;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD16, &hi);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_CONTRD17, &lo);
	f0_reg = ((unsigned int)hi << 8) | lo;
	if (!f0_reg) {
		aw8622x->cont_f0 = aw8622x->dts_info.f0_pre;
		return -EINVAL;
	}
	aw8622x->cont_f0 = AW8622X_READ_F0_COEFF / f0_reg;
	return 0;
}

static int aw8622x_haptic_cont_get_f0(struct aw8622x *aw8622x)
{
	int ret = 0;
	unsigned char reg_val = 0;
	unsigned int cnt = 200;
	bool get_f0_flag = false;

	aw8622x->f0 = aw8622x->dts_info.f0_pre;
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_CONT_MODE);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
			       AW8622X_BIT_CONTCFG1_EN_F0_DET_MASK,
			       AW8622X_BIT_CONTCFG1_F0_DET_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_TRACK_EN_MASK,
			       AW8622X_BIT_CONTCFG6_TRACK_EN);

	aw8622x_i2c_read(aw8622x, AW8622X_REG_PLAYCFG3, &reg_val);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_MASK,
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_EN);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
			       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
			       (unsigned char)aw8622x->dts_info.cont_drv1_lvl);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
			  (unsigned char)aw8622x->dts_info.cont_drv2_lvl);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8,
			  (unsigned char)aw8622x->dts_info.cont_drv1_time);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9,
			  (unsigned char)aw8622x->dts_info.cont_drv2_time);
	if (aw8622x->dts_info.cont_track_margin)
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG11,
				  (unsigned char)aw8622x->dts_info.cont_track_margin);

	aw8622x_haptic_play_go(aw8622x);

	while (cnt--) {
		unsigned char glb = 0;

		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBCFG4, &glb);
		if ((glb & AW8622X_BIT_GLBRD5_STATE_MASK) ==
		    AW8622X_BIT_GLBRD5_STATE_STANDBY) {
			get_f0_flag = true;
			break;
		}
		usleep_range(10000, 10500);
	}

	if (get_f0_flag) {
		usleep_range(10000, 10500);
		aw8622x_haptic_read_lra_f0(aw8622x);
		aw8622x_haptic_read_cont_f0(aw8622x);
	} else {
		ret = -ETIMEDOUT;
		dev_err(aw8622x->dev, "aw8622x: cont_get_f0 standby timeout\n");
	}

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG1,
			       AW8622X_BIT_CONTCFG1_EN_F0_DET_MASK,
			       AW8622X_BIT_CONTCFG1_F0_DET_DIS);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_MASK,
			       reg_val & AW8622X_BIT_PLAYCFG3_AUTO_BST_EN);
	return ret;
}

static int aw8622x_haptic_f0_calibration(struct aw8622x *aw8622x)
{
	int ret;
	int f0_cali_step, f0_cali_min, f0_limit;
	int f0_pre = (int)aw8622x->dts_info.f0_pre;
	int percen = (int)aw8622x->dts_info.f0_cali_percen;
	int f0_cali_lra;
	char cali_tmp;

	aw8622x_haptic_upload_lra(aw8622x, 0);
	ret = aw8622x_haptic_cont_get_f0(aw8622x);
	if (ret) {
		aw8622x_haptic_upload_lra(aw8622x, 1);
		return ret;
	}

	f0_limit = f0_pre * percen / 100;
	if ((int)aw8622x->f0 > f0_pre + f0_limit ||
	    (int)aw8622x->f0 < f0_pre - f0_limit) {
		aw8622x->f0_cali_data = 0x20;
		aw8622x_haptic_upload_lra(aw8622x, 1);
		return -ERANGE;
	}

	f0_cali_step = 100000 * (f0_pre - (int)aw8622x->f0) / (24 * f0_pre);
	if (f0_cali_step >= 0) {
		f0_cali_min = f0_cali_step / 10;
		if (f0_cali_step % 10 >= 5)
			f0_cali_min++;
	} else {
		f0_cali_min = f0_cali_step / 10;
		if ((-f0_cali_step) % 10 >= 5)
			f0_cali_min--;
	}

	cali_tmp = (char)(f0_cali_min + 32);
	if (cali_tmp < 32)
		f0_cali_lra = cali_tmp + 32;
	else
		f0_cali_lra = cali_tmp - 32;

	aw8622x->f0_cali_data = (unsigned int)(f0_cali_lra & 0x3F);
	aw8622x_haptic_upload_lra(aw8622x, 1);
	aw8622x_haptic_stop(aw8622x);
	dev_info(aw8622x->dev, "aw8622x: calibrated f0=%u trim_lra=0x%02x\n",
		 aw8622x->f0, aw8622x->f0_cali_data);
	return 0;
}

static enum hrtimer_restart aw8622x_vibrator_timer_func(struct hrtimer *timer)
{
	struct aw8622x *aw8622x = container_of(timer, struct aw8622x, timer);

	aw8622x->state = 0;
	queue_work(system_wq, &aw8622x->vibrator_work);
	return HRTIMER_NORESTART;
}

static void aw8622x_long_vibrate_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x =
		container_of(work, struct aw8622x, vibrator_work);

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_upload_lra(aw8622x, 1);

	if (aw8622x->state) {
		if (aw8622x->activate_mode == AW8622X_HAPTIC_ACTIVATE_CONT_MODE) {
			aw8622x_haptic_cont_config(aw8622x);
		} else {
			aw8622x_haptic_ram_vbat_comp(aw8622x, true);
			aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RAM_LOOP_MODE);
			aw8622x_haptic_play_go(aw8622x);
		}
		hrtimer_start(&aw8622x->timer,
			      ktime_set(aw8622x->duration / 1000,
					(aw8622x->duration % 1000) * 1000000),
			      HRTIMER_MODE_REL);
	}

	mutex_unlock(&aw8622x->lock);
}

static enum led_brightness aw8622x_haptic_brightness_get(struct led_classdev *cdev)
{
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return aw8622x->amplitude;
}

static void aw8622x_haptic_brightness_set(struct led_classdev *cdev,
					  enum led_brightness level)
{
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	if (!aw8622x->ram_init)
		return;

	mutex_lock(&aw8622x->lock);
	aw8622x->amplitude = level;
	aw8622x_haptic_stop(aw8622x);
	if (aw8622x->amplitude > 0) {
		aw8622x_haptic_upload_lra(aw8622x, 1);
		aw8622x_haptic_ram_vbat_comp(aw8622x, false);
		aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RAM_MODE);
		aw8622x_haptic_play_go(aw8622x);
	}
	mutex_unlock(&aw8622x->lock);
}

static void aw8622x_haptic_swicth_motor_protect_config(struct aw8622x *aw8622x,
						       unsigned char addr,
						       unsigned char val)
{
	if (addr == 1) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CTRL,
				       AW8622X_BIT_PRCT_CTRL_EN_MASK,
				       AW8622X_BIT_PRCT_CTRL_DIS);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG1, ~0x80, 0x80);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG2, ~0x80, 0x80);
	} else if (addr == 0) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CTRL,
				       AW8622X_BIT_PRCT_CTRL_EN_MASK,
				       AW8622X_BIT_PRCT_CTRL_EN);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG1, ~0x80, 0x00);
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG2, ~0x80, 0x00);
	} else if (addr == AW8622X_REG_PRCT_CFG1) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG1, ~0x80, val);
	} else if (addr == AW8622X_REG_PRCT_CFG2) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CFG2, ~0x80, val);
	} else if (addr == AW8622X_REG_PRCT_CFG3) {
		aw8622x_i2c_write(aw8622x, AW8622X_REG_PRCT_CFG3, val);
	}
}

static void aw8622x_haptic_trig_param_config(struct aw8622x *aw8622x)
{
	unsigned char trig_cfg;

	if (!(aw8622x->chip_name == AW86224_5_CHIP && aw8622x->is_used_intn)) {
		trig_cfg = ((aw8622x->trig[0].trig_level << 1) |
			    (aw8622x->trig[0].trig_polar << 2) |
			    aw8622x->trig[0].trig_brk) << 5;
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG7, ~0xE0, trig_cfg);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG1,
				  (aw8622x->trig[0].pos_enable << 7) |
				  aw8622x->trig[0].pos_sequence);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG4,
				  (aw8622x->trig[0].neg_enable << 7) |
				  aw8622x->trig[0].neg_sequence);
	}

	if (aw8622x->chip_name == AW86224_5_CHIP && !aw8622x->is_used_intn) {
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
				       AW8622X_BIT_SYSCTRL3_INTN_TRIG_MASK, 0x00);
		return;
	}

	trig_cfg = ((aw8622x->trig[1].trig_level << 1) |
		    (aw8622x->trig[1].trig_polar << 2) |
		    aw8622x->trig[1].trig_brk) << 1;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG7, ~0x0E, trig_cfg);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG2,
			  (aw8622x->trig[1].pos_enable << 7) |
			  aw8622x->trig[1].pos_sequence);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG5,
			  (aw8622x->trig[1].neg_enable << 7) |
			  aw8622x->trig[1].neg_sequence);

	trig_cfg = ((aw8622x->trig[2].trig_level << 1) |
		    (aw8622x->trig[2].trig_polar << 2) |
		    aw8622x->trig[2].trig_brk) << 5;
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG8, ~0xE0, trig_cfg);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG3,
			  (aw8622x->trig[2].pos_enable << 7) |
			  aw8622x->trig[2].pos_sequence);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRGCFG6,
			  (aw8622x->trig[2].neg_enable << 7) |
			  aw8622x->trig[2].neg_sequence);
}

static void aw8622x_haptic_offset_calibration(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;
	int cnt = 5;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_DIAG_GO_MASK,
			       AW8622X_BIT_DETCFG2_DIAG_GO_EN);

	while (cnt--) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_DETCFG2, &reg_val);
		if ((reg_val & AW8622X_BIT_DETCFG2_DIAG_GO_EN) == 0)
			break;
		usleep_range(2000, 2500);
	}
	if (cnt < 0)
		dev_warn(aw8622x->dev, "aw8622x: offset calibration timeout\n");

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
}

static int aw8622x_haptic_init(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;

	mutex_lock(&aw8622x->lock);

	aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1, &reg_val);
	aw8622x->index = reg_val ? reg_val : 1;
	aw8622x->gain = 0x64;
	aw8622x->seq[0] = (unsigned char)aw8622x->index;
	aw8622x->loop[0] = 0x0F;

	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_STANDBY_MODE);

	aw8622x->cont_drv1_lvl = aw8622x->dts_info.cont_drv1_lvl;
	aw8622x->cont_drv2_lvl = aw8622x->dts_info.cont_drv2_lvl;
	aw8622x->cont_brk_time = aw8622x->dts_info.cont_brk_time;
	aw8622x->cont_wait_num = aw8622x->dts_info.cont_wait_num;
	aw8622x->cont_drv1_time = aw8622x->dts_info.cont_drv1_time;
	aw8622x->cont_drv2_time = aw8622x->dts_info.cont_drv2_time;

	aw8622x_i2c_write(aw8622x, AW8622X_REG_PWMCFG1,
			  (unsigned char)aw8622x->dts_info.sine_array[0]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PWMCFG2,
			  (unsigned char)aw8622x->dts_info.sine_array[1]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PWMCFG3,
			  (unsigned char)aw8622x->dts_info.sine_array[2]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_PWMCFG4,
			  (unsigned char)aw8622x->dts_info.sine_array[3]);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRGCFG8, ~0x18, 0x10);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL_77, ~0x40, 0xC0);

	if (aw8622x->dts_info.d2s_gain)
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
				       AW8622X_BIT_DETCFG1_D2S_GAIN_MASK,
				       (unsigned char)aw8622x->dts_info.d2s_gain);

	aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG3,
			  (unsigned char)aw8622x->dts_info.cont_drv_width);
	if (aw8622x->dts_info.cont_tset)
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG13,
				       AW8622X_BIT_CONTCFG13_TSET_MASK,
				       (unsigned char)(aw8622x->dts_info.cont_tset << 4));
	if (aw8622x->dts_info.cont_bemf_set)
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG13,
				       AW8622X_BIT_CONTCFG13_BEME_SET_MASK,
				       (unsigned char)aw8622x->dts_info.cont_bemf_set);
	if (aw8622x->cont_brk_time)
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG10,
				  (unsigned char)aw8622x->cont_brk_time);
	if (aw8622x->dts_info.cont_brk_gain)
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG5,
				       AW8622X_BIT_CONTCFG5_BRK_GAIN_MASK,
				       (unsigned char)aw8622x->dts_info.cont_brk_gain);

	aw8622x_haptic_swicth_motor_protect_config(aw8622x, 0x00, 0x00);
	/* Keep INTN as interrupt pad (0x44 bit 3 = 1) and reset-default trig regs (0x33..0x39)
	 * matching Stock X698 state so floating INTN does not lock GLB_STATE=0x09.
	 */
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
			       AW8622X_BIT_SYSCTRL3_INTN_TRIG_MASK, (1 << 3));
	aw8622x_haptic_offset_calibration(aw8622x);

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PLAYCFG3,
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_MASK,
			       aw8622x->dts_info.is_enabled_auto_bst ?
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_EN :
			       AW8622X_BIT_PLAYCFG3_AUTO_BST_DIS);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_EN_GDT_MASK,
			       AW8622X_BIT_SYSCTRL2_EN_GDT_EN);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_ANACFG8, ~0x40, 0x40);

	/* Mask chip interrupts to avoid spurious edge/level storms on floating INTN */
	aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSINT, &reg_val);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_INT_MODE_MASK,
			       AW8622X_BIT_DETCFG1_INT_MODE_EDGE);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_SYSINTM, 0xFF);

	aw8622x_haptic_upload_lra(aw8622x, 1);

	mutex_unlock(&aw8622x->lock);

	if (aw8622x->f0_cali_needed) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_f0_calibration(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}

	return 0;
}

static void aw8622x_haptic_get_ram_number(struct aw8622x *aw8622x)
{
	unsigned char ver = 0, hi = 0, lo = 0;
	unsigned int first_wave_addr;

	if (!aw8622x->ram_init)
		return;

	mutex_lock(&aw8622x->lock);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRH,
			  (unsigned char)(aw8622x->ram.base_addr >> 8));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  (unsigned char)(aw8622x->ram.base_addr & 0xFF));
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &ver);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &hi);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &lo);

	first_wave_addr = ((unsigned int)hi << 8) | lo;
	if (first_wave_addr > aw8622x->ram.base_addr + 1)
		aw8622x->ram.ram_num =
			(unsigned char)((first_wave_addr - aw8622x->ram.base_addr - 1) >> 2);
	aw8622x->ram.version = ver;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
	mutex_unlock(&aw8622x->lock);

	dev_info(aw8622x->dev,
		 "aw8622x: ram_version=0x%02x first_addr=0x%04x ram_num=%u\n",
		 aw8622x->ram.version, first_wave_addr, aw8622x->ram.ram_num);
}

static int aw8622x_container_update(struct aw8622x *aw8622x,
				    struct aw8622x_container *aw8622x_cont)
{
	unsigned int i;
	unsigned int shift;
	unsigned char reg_val = 0;
	int ret = 0;
	unsigned short ae_thr, af_thr;

	mutex_lock(&aw8622x->lock);

	aw8622x->ram.baseaddr_shift = 2;
	aw8622x->ram.ram_shift = 4;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_haptic_stop(aw8622x);

	shift = aw8622x->ram.baseaddr_shift;
	aw8622x->ram.base_addr = ((unsigned int)aw8622x_cont->data[shift] << 8) |
				 aw8622x_cont->data[shift + 1];

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
			       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK, 0x10);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
			       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK, 0x20);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
			       AW8622X_BIT_RTPCFG1_ADDRH_MASK,
			       aw8622x_cont->data[shift]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG2,
			  aw8622x_cont->data[shift + 1]);

	ae_thr = (unsigned short)(aw8622x->ram.base_addr >> 1);
	af_thr = (unsigned short)(aw8622x->ram.base_addr -
				  (aw8622x->ram.base_addr >> 2));
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG3, 0x0F,
			       (unsigned char)((ae_thr >> 4) & 0xF0));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG4,
			  (unsigned char)(ae_thr & 0xFF));
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG3, 0xF0,
			       (unsigned char)((af_thr >> 8) & 0x0F));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RTPCFG5,
			  (unsigned char)(af_thr & 0xFF));

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RAMADDRH,
			       AW8622X_BIT_RAMADDRH_MASK,
			       aw8622x_cont->data[shift]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  aw8622x_cont->data[shift + 1]);

	for (i = aw8622x->ram.ram_shift; i < (unsigned int)aw8622x_cont->len; i++) {
		ret = aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMDATA,
					aw8622x_cont->data[i]);
		if (ret < 0)
			goto out_unlock;
	}

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RAMADDRH,
			       AW8622X_BIT_RAMADDRH_MASK,
			       aw8622x_cont->data[shift]);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  aw8622x_cont->data[shift + 1]);

	for (i = aw8622x->ram.ram_shift; i < (unsigned int)aw8622x_cont->len; i++) {
		ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		if (ret < 0)
			goto out_unlock;
		if (reg_val != aw8622x_cont->data[i]) {
			dev_err(aw8622x->dev,
				"aw8622x: ram check err addr=0x%04x file=0x%02x ram=0x%02x\n",
				i, aw8622x_cont->data[i], reg_val);
			ret = -EIO;
			break;
		}
	}

out_unlock:
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
	mutex_unlock(&aw8622x->lock);
	return ret;
}

static void aw8622x_ram_loaded(const struct firmware *cont, void *context)
{
	struct aw8622x *aw8622x = context;
	struct aw8622x_container *aw8622x_fw = NULL;
	unsigned short check_sum = 0;
	int i, ret;

	if (!cont) {
		release_firmware(cont);
		if (aw8622x->fw_retry_cnt++ < AW8622X_MAX_FIRMWARE_LOAD_CNT) {
			queue_delayed_work(system_wq, &aw8622x->ram_work,
					   msecs_to_jiffies(250));
		} else {
			dev_err(aw8622x->dev, "aw8622x: failed to load %s after %d tries\n",
				aw8622x_ram_name, aw8622x->fw_retry_cnt);
		}
		return;
	}

	if (cont->size < 6) {
		dev_err(aw8622x->dev, "aw8622x: %s too small (%zu)\n",
			aw8622x_ram_name, cont->size);
		release_firmware(cont);
		return;
	}

	for (i = 2; i < cont->size; i++)
		check_sum += cont->data[i];

	if (check_sum != (((unsigned short)cont->data[0] << 8) | cont->data[1])) {
		dev_err(aw8622x->dev, "aw8622x: checksum error (calc=0x%04x)\n",
			check_sum);
		release_firmware(cont);
		return;
	}

	aw8622x->ram.check_sum = check_sum;
	aw8622x->ram.len = cont->size - 4;

	aw8622x_fw = kzalloc(sizeof(*aw8622x_fw) + cont->size, GFP_KERNEL);
	if (!aw8622x_fw) {
		release_firmware(cont);
		return;
	}

	aw8622x_fw->len = (int)cont->size;
	memcpy(aw8622x_fw->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw8622x_container_update(aw8622x, aw8622x_fw);
	kfree(aw8622x_fw);

	if (ret) {
		aw8622x->ram_init = 0;
		dev_err(aw8622x->dev, "aw8622x: ram firmware update failed\n");
		return;
	}

	aw8622x->ram_init = 1;
	dev_info(aw8622x->dev,
		 "aw8622x: loaded %s (%u bytes, csum=0x%04x)\n",
		 aw8622x_ram_name, aw8622x->ram.len + 4, check_sum);
	aw8622x_haptic_get_ram_number(aw8622x);
}

static int aw8622x_ram_update(struct aw8622x *aw8622x)
{
	aw8622x->ram_init = 0;
	aw8622x->fw_retry_cnt = 0;
	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				       aw8622x_ram_name, aw8622x->dev,
				       GFP_KERNEL, aw8622x, aw8622x_ram_loaded);
}

static void aw8622x_ram_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x =
		container_of(work, struct aw8622x, ram_work.work);

	request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw8622x_ram_name, aw8622x->dev,
				GFP_KERNEL, aw8622x, aw8622x_ram_loaded);
}

static void aw8622x_rtp_work_routine(struct work_struct *work)
{
	struct aw8622x *aw8622x =
		container_of(work, struct aw8622x, rtp_work);
	const struct firmware *rtp_file = NULL;
	unsigned char glb = 0;
	int ret;

	mutex_lock(&aw8622x->rtp_lock);
	aw8622x->rtp_init = 0;
	vfree(aw8622x->rtp_container);
	aw8622x->rtp_container = NULL;

	if (aw8622x->rtp_file_num >= ARRAY_SIZE(aw8622x_rtp_name)) {
		mutex_unlock(&aw8622x->rtp_lock);
		return;
	}

	ret = request_firmware(&rtp_file, aw8622x_rtp_name[aw8622x->rtp_file_num],
			       aw8622x->dev);
	if (ret < 0 || !rtp_file) {
		dev_err(aw8622x->dev, "aw8622x: failed to load %s (%d)\n",
			aw8622x_rtp_name[aw8622x->rtp_file_num], ret);
		mutex_unlock(&aw8622x->rtp_lock);
		return;
	}

	aw8622x->rtp_container = vmalloc(sizeof(*aw8622x->rtp_container) +
					 rtp_file->size);
	if (!aw8622x->rtp_container) {
		release_firmware(rtp_file);
		mutex_unlock(&aw8622x->rtp_lock);
		return;
	}
	aw8622x->rtp_container->len = (int)rtp_file->size;
	memcpy(aw8622x->rtp_container->data, rtp_file->data, rtp_file->size);
	release_firmware(rtp_file);
	aw8622x->rtp_init = 1;

	mutex_lock(&aw8622x->lock);
	aw8622x->rtp_cnt = 0;
	aw8622x_haptic_stop(aw8622x);
	aw8622x_haptic_upload_lra(aw8622x, 2);
	aw8622x_haptic_set_gain(aw8622x, (unsigned char)aw8622x->gain);
	aw8622x_haptic_play_mode(aw8622x, AW8622X_HAPTIC_RTP_MODE);
	aw8622x_haptic_play_go(aw8622x);
	mutex_unlock(&aw8622x->lock);

	usleep_range(2000, 2500);
	while (aw8622x->rtp_cnt < (unsigned int)aw8622x->rtp_container->len &&
	       aw8622x->play_mode == AW8622X_HAPTIC_RTP_MODE) {
		unsigned char sysst = 0;

		aw8622x_i2c_read(aw8622x, AW8622X_REG_GLBCFG4, &glb);
		if ((glb & AW8622X_BIT_GLBRD5_STATE_MASK) ==
		    AW8622X_BIT_GLBRD5_STATE_STANDBY)
			break;

		aw8622x_i2c_read(aw8622x, AW8622X_REG_SYSST, &sysst);
		if (!(sysst & AW8622X_BIT_SYSST_FF_AFS)) {
			unsigned int rem = (unsigned int)aw8622x->rtp_container->len -
					   aw8622x->rtp_cnt;
			unsigned int max_chunk = aw8622x->ram.base_addr >> 2;
			unsigned int chunk = min(rem, max_chunk ? max_chunk : 256U);

			chunk = min(chunk, (unsigned int)AW8622X_RTP_SINGLE_MAX_NUM);
			aw8622x_i2c_writes(aw8622x, AW8622X_REG_RTPDATA,
					   &aw8622x->rtp_container->data[aw8622x->rtp_cnt],
					   chunk);
			aw8622x->rtp_cnt += chunk;
		} else {
			usleep_range(1000, 1500);
		}
	}

	vfree(aw8622x->rtp_container);
	aw8622x->rtp_container = NULL;
	aw8622x->rtp_init = 0;
	mutex_unlock(&aw8622x->rtp_lock);
}

static ssize_t aw8622x_state_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "state = %d\n", aw8622x->state);
}

static ssize_t aw8622x_state_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_duration_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	ktime_t time_rem = ktime_set(0, 0);

	if (hrtimer_active(&aw8622x->timer))
		time_rem = hrtimer_get_remaining(&aw8622x->timer);

	return snprintf(buf, PAGE_SIZE, "duration = %lldms\n",
			ktime_to_ms(time_rem));
}

static ssize_t aw8622x_duration_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	unsigned int *dt = aw8622x->dts_info.duration_time;
	unsigned char wav = 0, loop = 0;
	int rc, i;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val == 0)
		return count;

	for (i = 0; i < 2; i++) {
		if (dt[i] >= dt[i + 1]) {
			dev_err(aw8622x->dev, "aw8622x: duration_time range error\n");
			return -ERANGE;
		}
	}

	if (val == 65 || val == 95) {
		wav = 1;
		loop = 0;
	} else if (val >= 1 && val < dt[0] && val != 12) {
		wav = 3;
		loop = 0;
	} else if (val == 12) {
		wav = 0;
		loop = 0;
	} else if (val >= dt[0] && val < dt[1]) {
		wav = 1;
		loop = 0;
	} else if (val >= dt[1] && val < dt[2]) {
		wav = 5;
		loop = 0xF0;
	} else if (val >= dt[2]) {
		wav = 4;
		loop = 0xF0;
	}

	mutex_lock(&aw8622x->lock);
	aw8622x->duration = (int)val;
	aw8622x_i2c_write(aw8622x, AW8622X_REG_WAVCFG1, wav);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9, ~0xF0, loop);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_WAVCFG2, 0x00);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_WAVCFG9, ~0x0F, 0x00);
	mutex_unlock(&aw8622x->lock);

	return count;
}

static ssize_t aw8622x_activate_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "activate = %d\n", aw8622x->state);
}

static ssize_t aw8622x_activate_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	if (!aw8622x->ram_init) {
		dev_warn(aw8622x->dev, "aw8622x: ram init not ready\n");
		return count;
	}

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val <= 1) {
		mutex_lock(&aw8622x->lock);
		hrtimer_cancel(&aw8622x->timer);
		aw8622x->state = (int)val;
		mutex_unlock(&aw8622x->lock);
		queue_work(system_wq, &aw8622x->vibrator_work);
	}

	return count;
}

static ssize_t aw8622x_activate_mode_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "activate_mode = %u\n",
			aw8622x->activate_mode);
}

static ssize_t aw8622x_activate_mode_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	mutex_lock(&aw8622x->lock);
	aw8622x->activate_mode = (unsigned char)val;
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1, &reg_val);
	aw8622x->index = reg_val;
	return snprintf(buf, PAGE_SIZE, "index = %d\n", aw8622x->index);
}

static ssize_t aw8622x_index_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (aw8622x->ram.ram_num && val > aw8622x->ram.ram_num)
		return count;

	mutex_lock(&aw8622x->lock);
	aw8622x->index = (int)val;
	aw8622x_haptic_set_wav_seq(aw8622x, 0x00, (unsigned char)val);
	aw8622x_haptic_set_wav_loop(aw8622x, 0x00, 0x0F);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_gain_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "gain = 0x%02X\n", aw8622x->gain);
}

static ssize_t aw8622x_gain_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val > 0x80)
		val = 0x80;

	mutex_lock(&aw8622x->lock);
	aw8622x->gain = (int)val;
	aw8622x_haptic_set_gain(aw8622x, (unsigned char)val);
	mutex_unlock(&aw8622x->lock);
	return count;
}

static ssize_t aw8622x_seq_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	size_t count = 0;
	unsigned char i, reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG1 + i, &reg_val);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d = %u\n", i + 1, reg_val);
		aw8622x->seq[i] = reg_val;
	}
	return count;
}

static ssize_t aw8622x_seq_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] >= AW8622X_SEQUENCER_SIZE)
			return count;
		if (aw8622x->ram.ram_num && databuf[1] > aw8622x->ram.ram_num)
			return count;
		mutex_lock(&aw8622x->lock);
		aw8622x->seq[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_seq(aw8622x, (unsigned char)databuf[0],
					   (unsigned char)databuf[1]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_loop_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	size_t count = 0;
	unsigned char i, reg_val = 0;

	for (i = 0; i < AW8622X_SEQUENCER_LOOP_SIZE; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_WAVCFG9 + i, &reg_val);
		aw8622x->loop[i * 2] = (reg_val >> 4) & 0x0F;
		aw8622x->loop[i * 2 + 1] = reg_val & 0x0F;
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = %u\n", i * 2 + 1, aw8622x->loop[i * 2]);
		count += snprintf(buf + count, PAGE_SIZE - count,
				  "seq%d_loop = %u\n", i * 2 + 2, aw8622x->loop[i * 2 + 1]);
	}
	return count;
}

static ssize_t aw8622x_loop_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] >= AW8622X_SEQUENCER_SIZE)
			return count;
		mutex_lock(&aw8622x->lock);
		aw8622x->loop[databuf[0]] = (unsigned char)databuf[1];
		aw8622x_haptic_set_wav_loop(aw8622x, (unsigned char)databuf[0],
					    (unsigned char)databuf[1]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	ssize_t len = 0;
	unsigned int i;
	unsigned char reg_val = 0;

	for (i = 0; i < 0x5C; i++) {
		if (!(aw8622x_reg_access[i] & AW8622X_REG_RD_ACCESS))
			continue;
		aw8622x_i2c_read(aw8622x, (unsigned char)i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02X=0x%02X\n", i, reg_val);
	}
	return len;
}

static ssize_t aw8622x_reg_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw8622x_i2c_write(aw8622x, (unsigned char)databuf[0],
				  (unsigned char)databuf[1]);

	return count;
}

static ssize_t aw8622x_rtp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "rtp_cnt = %u\nrtp_file_num = %u\n",
			aw8622x->rtp_cnt, aw8622x->rtp_file_num);
}

static ssize_t aw8622x_rtp_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val > 0 && val < ARRAY_SIZE(aw8622x_rtp_name)) {
		aw8622x->rtp_file_num = val;
		queue_work(system_wq, &aw8622x->rtp_work);
	} else if (val == 0) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_stop(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_ram_update_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	ssize_t len = 0;
	unsigned int i;
	unsigned char reg_val = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRH,
			  (unsigned char)(aw8622x->ram.base_addr >> 8));
	aw8622x_i2c_write(aw8622x, AW8622X_REG_RAMADDRL,
			  (unsigned char)(aw8622x->ram.base_addr & 0xFF));
	len += snprintf(buf + len, PAGE_SIZE - len, "haptic_ram len = %u\n",
			aw8622x->ram.len);
	for (i = 0; i < aw8622x->ram.len && len < PAGE_SIZE - 8; i++) {
		aw8622x_i2c_read(aw8622x, AW8622X_REG_RAMDATA, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				(i % 5 == 4) ? "0x%02X\n" : "0x%02X,", reg_val);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
	mutex_unlock(&aw8622x->lock);
	return len;
}

static ssize_t aw8622x_ram_update_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val == 1)
		aw8622x_ram_update(aw8622x);
	return count;
}

static ssize_t aw8622x_ram_num_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	aw8622x_haptic_get_ram_number(aw8622x);
	return snprintf(buf, PAGE_SIZE, "ram_num = %u\n", aw8622x->ram.ram_num);
}

static ssize_t aw8622x_f0_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned char trim_saved = 0;

	mutex_lock(&aw8622x->lock);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_D2S_GAIN_MASK, 0x07);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_TRIMCFG3, &trim_saved);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_TRIMCFG3,
			       AW8622X_BIT_TRIMCFG3_TRIM_LRA_MASK, 0x00);
	aw8622x_haptic_cont_get_f0(aw8622x);
	aw8622x_i2c_write(aw8622x, AW8622X_REG_TRIMCFG3, trim_saved);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_D2S_GAIN_MASK,
			       (unsigned char)aw8622x->dts_info.d2s_gain);
	mutex_unlock(&aw8622x->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", aw8622x->f0);
}

static ssize_t aw8622x_f0_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_cali_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", aw8622x->f0);
}

static ssize_t aw8622x_cali_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;
	int rc;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_f0_calibration(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_f0_save_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "f0_cali_data = 0x%02X\n",
			aw8622x->f0_cali_data);
}

static ssize_t aw8622x_f0_save_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0)
		aw8622x->f0_cali_data = val & 0x3F;
	return count;
}

static ssize_t aw8622x_osc_save_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);
}

static ssize_t aw8622x_osc_save_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0)
		aw8622x->osc_cali_data = val & 0xFF;
	return count;
}

static ssize_t aw8622x_cont_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	aw8622x_haptic_read_cont_f0(aw8622x);
	return snprintf(buf, PAGE_SIZE, "cont_f0 = %u\n", aw8622x->cont_f0);
}

static ssize_t aw8622x_cont_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_stop(aw8622x);
		if (val)
			aw8622x_haptic_cont_config(aw8622x);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_wait_num_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "cont_wait_num = 0x%02X\n",
			aw8622x->cont_wait_num);
}

static ssize_t aw8622x_cont_wait_num_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (sscanf(buf, "%x", &val) == 1) {
		mutex_lock(&aw8622x->lock);
		aw8622x->cont_wait_num = val;
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG4,
				  (unsigned char)val);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_lvl_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE,
			"cont_drv1_lvl = 0x%02X\ncont_drv2_lvl = 0x%02X\n",
			aw8622x->cont_drv1_lvl, aw8622x->cont_drv2_lvl);
}

static ssize_t aw8622x_cont_drv_lvl_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		mutex_lock(&aw8622x->lock);
		aw8622x->cont_drv1_lvl = databuf[0];
		aw8622x->cont_drv2_lvl = databuf[1];
		aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_CONTCFG6,
				       AW8622X_BIT_CONTCFG6_DRV1_LVL_MASK,
				       (unsigned char)databuf[0]);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG7,
				  (unsigned char)databuf[1]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_drv_time_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE,
			"cont_drv1_time = 0x%02X\ncont_drv2_time = 0x%02X\n",
			aw8622x->cont_drv1_time, aw8622x->cont_drv2_time);
}

static ssize_t aw8622x_cont_drv_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		mutex_lock(&aw8622x->lock);
		aw8622x->cont_drv1_time = databuf[0];
		aw8622x->cont_drv2_time = databuf[1];
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG8,
				  (unsigned char)databuf[0]);
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG9,
				  (unsigned char)databuf[1]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_cont_brk_time_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "cont_brk_time = 0x%02X\n",
			aw8622x->cont_brk_time);
}

static ssize_t aw8622x_cont_brk_time_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (sscanf(buf, "%x", &val) == 1) {
		mutex_lock(&aw8622x->lock);
		aw8622x->cont_brk_time = val;
		aw8622x_i2c_write(aw8622x, AW8622X_REG_CONTCFG10,
				  (unsigned char)val);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_vbat_monitor_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_get_vbat(aw8622x);
	mutex_unlock(&aw8622x->lock);
	return snprintf(buf, PAGE_SIZE, "vbat_monitor = %u\n", aw8622x->vbat);
}

static ssize_t aw8622x_vbat_monitor_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_lra_resistance_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned char reg_53 = 0, reg_57 = 0, reg_49 = 0;
	unsigned int raw;

	mutex_lock(&aw8622x->lock);
	aw8622x_haptic_stop(aw8622x);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DETCFG1, &reg_49);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_D2S_GAIN_MASK,
			       (unsigned char)aw8622x->dts_info.d2s_gain);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_EN);
	aw8622x_haptic_stop(aw8622x);
	usleep_range(2000, 2500);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL3,
			       AW8622X_BIT_SYSCTRL3_STANDBY_MASK,
			       AW8622X_BIT_SYSCTRL3_STANDBY_OFF);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_PRCT_CTRL, ~0x10, 0x10);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG2,
			       AW8622X_BIT_DETCFG2_DIAG_GO_MASK,
			       AW8622X_BIT_DETCFG2_DIAG_GO_EN);
	usleep_range(30000, 35000);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_RL, &reg_53);
	aw8622x_i2c_read(aw8622x, AW8622X_REG_DET_LO, &reg_57);

	raw = ((unsigned int)reg_53 << 2) | (reg_57 & 0x03);
	aw8622x->lra = ((raw * 67800U) / 10240U) * 10U;

	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_SYSCTRL2,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_MASK,
			       AW8622X_BIT_SYSCTRL2_RAMINIT_OFF);
	aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_DETCFG1,
			       AW8622X_BIT_DETCFG1_D2S_GAIN_MASK,
			       reg_49 & 0x07);
	mutex_unlock(&aw8622x->lock);

	return snprintf(buf, PAGE_SIZE, "lra_resistance = %u\n", aw8622x->lra);
}

static ssize_t aw8622x_prctmode_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_PRCT_CTRL, &reg_val);
	return snprintf(buf, PAGE_SIZE, "prctmode = %u\n", reg_val & 0x08);
}

static ssize_t aw8622x_prctmode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		mutex_lock(&aw8622x->lock);
		aw8622x_haptic_swicth_motor_protect_config(aw8622x,
							   (unsigned char)databuf[0],
							   (unsigned char)databuf[1]);
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_sram_size_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned char reg_val = 0;

	aw8622x_i2c_read(aw8622x, AW8622X_REG_RTPCFG1, &reg_val);
	switch (reg_val & 0x30) {
	case 0x30:
		return snprintf(buf, PAGE_SIZE, "sram_size = 3K\n");
	case 0x20:
		return snprintf(buf, PAGE_SIZE, "sram_size = 2K\n");
	case 0x10:
		return snprintf(buf, PAGE_SIZE, "sram_size = 1K\n");
	default:
		return snprintf(buf, PAGE_SIZE,
				"sram_size = 0x%02x error, plz check reg.\n",
				reg_val & 0x30);
	}
}

static ssize_t aw8622x_sram_size_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0) {
		mutex_lock(&aw8622x->lock);
		if (val == 1) {
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK, 0);
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_EN);
		} else if (val == 2) {
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_EN);
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK, 0);
		} else if (val == 3) {
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_MASK,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_1K_EN);
			aw8622x_i2c_write_bits(aw8622x, AW8622X_REG_RTPCFG1,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_MASK,
					       AW8622X_BIT_RTPCFG1_SRAM_SIZE_2K_EN);
		}
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_ram_vbat_compensate_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "ram_vbat_comp = %u\n",
			aw8622x->ram_vbat_comp);
}

static ssize_t aw8622x_ram_vbat_compensate_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0) {
		mutex_lock(&aw8622x->lock);
		aw8622x->ram_vbat_comp = val ?
			AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE :
			AW8622X_HAPTIC_RAM_VBAT_COMP_DISABLE;
		mutex_unlock(&aw8622x->lock);
	}
	return count;
}

static ssize_t aw8622x_osc_cali_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "osc_cali_data = 0x%02X\n",
			aw8622x->osc_cali_data);
}

static ssize_t aw8622x_osc_cali_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	return count;
}

static ssize_t aw8622x_gun_type_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->gun_type);
}

static ssize_t aw8622x_gun_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0)
		aw8622x->gun_type = val;
	return count;
}

static ssize_t aw8622x_bullet_nr_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", aw8622x->bullet_nr);
}

static ssize_t aw8622x_bullet_nr_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int val = 0;

	if (kstrtouint(buf, 0, &val) == 0)
		aw8622x->bullet_nr = val;
	return count;
}

static ssize_t aw8622x_trig_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	ssize_t len = 0;
	int i;

	for (i = 0; i < AW8622X_TRIG_NUM; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"%d: trig_level=%u trig_polar=%u pos_en=%u pos_seq=%u neg_en=%u neg_seq=%u brk=%u\n",
				i + 1,
				aw8622x->trig[i].trig_level,
				aw8622x->trig[i].trig_polar,
				aw8622x->trig[i].pos_enable,
				aw8622x->trig[i].pos_sequence,
				aw8622x->trig[i].neg_enable,
				aw8622x->trig[i].neg_sequence,
				aw8622x->trig[i].trig_brk);
	}
	return len;
}

static ssize_t aw8622x_trig_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8622x *aw8622x = container_of(cdev, struct aw8622x, cdev);
	unsigned int d[8];

	if (sscanf(buf, "%u %u %u %u %u %u %u %u",
		   &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]) == 8) {
		if (d[0] >= 1 && d[0] <= AW8622X_TRIG_NUM) {
			unsigned int idx = d[0] - 1;

			aw8622x->trig[idx].trig_level = d[1];
			aw8622x->trig[idx].trig_polar = d[2];
			aw8622x->trig[idx].pos_enable = d[3];
			aw8622x->trig[idx].pos_sequence = d[4];
			aw8622x->trig[idx].neg_enable = d[5];
			aw8622x->trig[idx].neg_sequence = d[6];
			aw8622x->trig[idx].trig_brk = d[7];
			mutex_lock(&aw8622x->lock);
			aw8622x_haptic_trig_param_config(aw8622x);
			mutex_unlock(&aw8622x->lock);
		}
	}
	return count;
}

static DEVICE_ATTR(state, 0664, aw8622x_state_show, aw8622x_state_store);
static DEVICE_ATTR(duration, 0664, aw8622x_duration_show, aw8622x_duration_store);
static DEVICE_ATTR(activate, 0664, aw8622x_activate_show, aw8622x_activate_store);
static DEVICE_ATTR(activate_mode, 0664, aw8622x_activate_mode_show, aw8622x_activate_mode_store);
static DEVICE_ATTR(index, 0664, aw8622x_index_show, aw8622x_index_store);
static DEVICE_ATTR(gain, 0664, aw8622x_gain_show, aw8622x_gain_store);
static DEVICE_ATTR(seq, 0664, aw8622x_seq_show, aw8622x_seq_store);
static DEVICE_ATTR(loop, 0664, aw8622x_loop_show, aw8622x_loop_store);
static DEVICE_ATTR(register, 0664, aw8622x_reg_show, aw8622x_reg_store);
static DEVICE_ATTR(rtp, 0664, aw8622x_rtp_show, aw8622x_rtp_store);
static DEVICE_ATTR(ram_update, 0664, aw8622x_ram_update_show, aw8622x_ram_update_store);
static DEVICE_ATTR(ram_num, 0444, aw8622x_ram_num_show, NULL);
static DEVICE_ATTR(f0, 0664, aw8622x_f0_show, aw8622x_f0_store);
static DEVICE_ATTR(cali, 0664, aw8622x_cali_show, aw8622x_cali_store);
static DEVICE_ATTR(f0_save, 0664, aw8622x_f0_save_show, aw8622x_f0_save_store);
static DEVICE_ATTR(osc_save, 0664, aw8622x_osc_save_show, aw8622x_osc_save_store);
static DEVICE_ATTR(cont, 0664, aw8622x_cont_show, aw8622x_cont_store);
static DEVICE_ATTR(cont_wait_num, 0664, aw8622x_cont_wait_num_show, aw8622x_cont_wait_num_store);
static DEVICE_ATTR(cont_drv_lvl, 0664, aw8622x_cont_drv_lvl_show, aw8622x_cont_drv_lvl_store);
static DEVICE_ATTR(cont_drv_time, 0664, aw8622x_cont_drv_time_show, aw8622x_cont_drv_time_store);
static DEVICE_ATTR(cont_brk_time, 0664, aw8622x_cont_brk_time_show, aw8622x_cont_brk_time_store);
static DEVICE_ATTR(vbat_monitor, 0664, aw8622x_vbat_monitor_show, aw8622x_vbat_monitor_store);
static DEVICE_ATTR(lra_resistance, 0444, aw8622x_lra_resistance_show, NULL);
static DEVICE_ATTR(prctmode, 0664, aw8622x_prctmode_show, aw8622x_prctmode_store);
static DEVICE_ATTR(sram_size, 0664, aw8622x_sram_size_show, aw8622x_sram_size_store);
static DEVICE_ATTR(ram_vbat_comp, 0664, aw8622x_ram_vbat_compensate_show, aw8622x_ram_vbat_compensate_store);
static DEVICE_ATTR(osc_cali, 0664, aw8622x_osc_cali_show, aw8622x_osc_cali_store);
static DEVICE_ATTR(gun_type, 0664, aw8622x_gun_type_show, aw8622x_gun_type_store);
static DEVICE_ATTR(bullet_nr, 0664, aw8622x_bullet_nr_show, aw8622x_bullet_nr_store);
static DEVICE_ATTR(trig, 0664, aw8622x_trig_show, aw8622x_trig_store);

static struct attribute *aw8622x_vibrator_attributes[] = {
	&dev_attr_state.attr,
	&dev_attr_duration.attr,
	&dev_attr_activate.attr,
	&dev_attr_activate_mode.attr,
	&dev_attr_index.attr,
	&dev_attr_gain.attr,
	&dev_attr_seq.attr,
	&dev_attr_loop.attr,
	&dev_attr_register.attr,
	&dev_attr_rtp.attr,
	&dev_attr_ram_update.attr,
	&dev_attr_ram_num.attr,
	&dev_attr_f0.attr,
	&dev_attr_cali.attr,
	&dev_attr_f0_save.attr,
	&dev_attr_osc_save.attr,
	&dev_attr_cont.attr,
	&dev_attr_cont_wait_num.attr,
	&dev_attr_cont_drv_lvl.attr,
	&dev_attr_cont_drv_time.attr,
	&dev_attr_cont_brk_time.attr,
	&dev_attr_vbat_monitor.attr,
	&dev_attr_lra_resistance.attr,
	&dev_attr_prctmode.attr,
	&dev_attr_sram_size.attr,
	&dev_attr_ram_vbat_comp.attr,
	&dev_attr_osc_cali.attr,
	&dev_attr_gun_type.attr,
	&dev_attr_bullet_nr.attr,
	&dev_attr_trig.attr,
	NULL
};

static struct attribute_group aw8622x_vibrator_attribute_group = {
	.attrs = aw8622x_vibrator_attributes,
};

static void aw8622x_parse_dt(struct aw8622x *aw8622x, struct device_node *np)
{
	struct aw8622x_dts_info *info = &aw8622x->dts_info;
	int ret;

	info->mode = 0;
	info->f0_pre = 2350;
	info->f0_cali_percen = 7;
	info->cont_drv1_lvl = 0x7F;
	info->cont_drv2_lvl = 0x36;
	info->cont_drv1_time = 0x04;
	info->cont_drv2_time = 0x14;
	info->cont_drv_width = 0x47;
	info->cont_wait_num = 0x06;
	info->cont_brk_time = 0x08;
	info->cont_track_margin = 0x12;
	info->cont_tset = 0x06;
	info->cont_bemf_set = 0x02;
	info->cont_brk_gain = 0x08;
	info->d2s_gain = 0x07;
	info->sine_array[0] = 0x05;
	info->sine_array[1] = 0xB2;
	info->sine_array[2] = 0xFF;
	info->sine_array[3] = 0xEF;
	info->duration_time[0] = 30;
	info->duration_time[1] = 60;
	info->duration_time[2] = 90;

	if (!np)
		return;

	of_property_read_u32(np, "aw8622x_vib_mode", &info->mode);
	of_property_read_u32(np, "aw8622x_vib_f0_pre", &info->f0_pre);
	of_property_read_u32(np, "aw8622x_vib_f0_cali_percen", &info->f0_cali_percen);
	of_property_read_u32(np, "aw8622x_vib_cont_drv1_lvl", &info->cont_drv1_lvl);
	of_property_read_u32(np, "aw8622x_vib_cont_drv2_lvl", &info->cont_drv2_lvl);
	of_property_read_u32(np, "aw8622x_vib_cont_drv1_time", &info->cont_drv1_time);
	of_property_read_u32(np, "aw8622x_vib_cont_drv2_time", &info->cont_drv2_time);
	of_property_read_u32(np, "aw8622x_vib_cont_drv_width", &info->cont_drv_width);
	of_property_read_u32(np, "aw8622x_vib_cont_wait_num", &info->cont_wait_num);
	of_property_read_u32(np, "aw8622x_vib_cont_brk_time", &info->cont_brk_time);
	of_property_read_u32(np, "aw8622x_vib_cont_track_margin", &info->cont_track_margin);
	of_property_read_u32(np, "aw8622x_vib_cont_tset", &info->cont_tset);
	of_property_read_u32(np, "aw8622x_vib_cont_bemf_set", &info->cont_bemf_set);
	of_property_read_u32(np, "aw8622x_vib_cont_brk_gain", &info->cont_brk_gain);
	of_property_read_u32(np, "aw8622x_vib_d2s_gain", &info->d2s_gain);

	of_property_read_u32_array(np, "aw8622x_vib_prctmode", info->prctmode,
				   ARRAY_SIZE(info->prctmode));
	of_property_read_u32_array(np, "aw8622x_vib_sine_array", info->sine_array,
				   ARRAY_SIZE(info->sine_array));
	of_property_read_u32_array(np, "aw8622x_vib_trig_config", info->trig_config,
				   ARRAY_SIZE(info->trig_config));
	ret = of_property_read_u32_array(np, "aw8622x_vib_duration_time",
					 info->duration_time,
					 ARRAY_SIZE(info->duration_time));
	if (ret)
		dev_info(aw8622x->dev, "aw8622x: using default duration_time\n");

	info->is_enabled_auto_bst =
		of_property_read_bool(np, "aw8622x_vib_is_enabled_auto_bst");
}

static int aw8622x_check_qualify(struct aw8622x *aw8622x)
{
	unsigned char reg_val = 0;
	int ret;

	ret = aw8622x_i2c_read(aw8622x, AW8622X_REG_EFRD9, &reg_val);
	if (ret < 0)
		return ret;

	if ((signed char)reg_val < 0)
		return 1;

	dev_err(aw8622x->dev, "aw8622x: qualify register 0x64=0x%02x failed\n",
		reg_val);
	return 0;
}

static int aw8622x_vibrator_init(struct aw8622x *aw8622x)
{
	char *p;
	int ret;

	p = strstr(saved_command_line, "aw8622x_lk_f0_cali=");
	if (p) {
		aw8622x->f0_cali_data =
			(unsigned int)(simple_strtol(p + 19, NULL, 0) & 0x3F);
		aw8622x->f0_cali_needed = false;
		dev_info(aw8622x->dev, "aw8622x: lk_f0_cali=0x%02x\n",
			 aw8622x->f0_cali_data);
	} else {
		aw8622x->f0_cali_data = 0x20;
		aw8622x->f0_cali_needed = true;
	}

	mutex_init(&aw8622x->lock);
	mutex_init(&aw8622x->rtp_lock);
	hrtimer_init(&aw8622x->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw8622x->timer.function = aw8622x_vibrator_timer_func;
	INIT_WORK(&aw8622x->vibrator_work, aw8622x_long_vibrate_work_routine);
	INIT_WORK(&aw8622x->rtp_work, aw8622x_rtp_work_routine);
	INIT_DELAYED_WORK(&aw8622x->ram_work, aw8622x_ram_work_routine);

	aw8622x->activate_mode = (unsigned char)aw8622x->dts_info.mode;
	aw8622x->ram_vbat_comp = AW8622X_HAPTIC_RAM_VBAT_COMP_ENABLE;
	aw8622x->is_used_intn = 1;

	aw8622x->cdev.name = "vibrator";
	aw8622x->cdev.brightness_get = aw8622x_haptic_brightness_get;
	aw8622x->cdev.brightness_set = aw8622x_haptic_brightness_set;

	ret = devm_led_classdev_register(&aw8622x->i2c->dev, &aw8622x->cdev);
	if (ret < 0) {
		dev_err(aw8622x->dev, "aw8622x: fail to create leds/vibrator (%d)\n",
			ret);
		return ret;
	}

	ret = sysfs_create_group(&aw8622x->cdev.dev->kobj,
				 &aw8622x_vibrator_attribute_group);
	if (ret < 0) {
		dev_err(aw8622x->dev, "aw8622x: fail to create sysfs group (%d)\n",
			ret);
		return ret;
	}

	return 0;
}

int aw8622x_probe_dev(struct i2c_client *i2c, int reset_gpio, int irq_gpio,
		      unsigned char chip_name)
{
	struct aw8622x *aw8622x;
	struct device_node *np = i2c->dev.of_node;
	int ret;

	aw8622x = devm_kzalloc(&i2c->dev, sizeof(*aw8622x), GFP_KERNEL);
	if (!aw8622x)
		return -ENOMEM;

	aw8622x->magic = AW8622X_STRUCT_MAGIC;
	aw8622x->i2c = i2c;
	aw8622x->dev = &i2c->dev;
	aw8622x->reset_gpio = reset_gpio;
	aw8622x->irq_gpio = irq_gpio;
	aw8622x->chip_name = chip_name;

	i2c_set_clientdata(i2c, aw8622x);
	dev_set_drvdata(&i2c->dev, aw8622x);

	aw8622x_parse_dt(aw8622x, np);

	ret = aw8622x_check_qualify(aw8622x);
	if (ret <= 0)
		return -EINVAL;

	ret = aw8622x_vibrator_init(aw8622x);
	if (ret < 0)
		return ret;

	aw8622x_haptic_init(aw8622x);
	queue_delayed_work(system_wq, &aw8622x->ram_work, msecs_to_jiffies(2000));

	dev_info(aw8622x->dev,
		 "aw8622x: driver %s probe ok (chip=%s)\n",
		 AW8622X_DRIVER_VERSION,
		 chip_name == AW86223_CHIP ? "AW86223" : "AW86224/AW86225");
	return 0;
}

int aw8622x_remove_dev(struct i2c_client *i2c)
{
	struct aw8622x *aw8622x = i2c_get_clientdata(i2c);

	cancel_delayed_work_sync(&aw8622x->ram_work);
	cancel_work_sync(&aw8622x->rtp_work);
	cancel_work_sync(&aw8622x->vibrator_work);
	hrtimer_cancel(&aw8622x->timer);

	if (aw8622x->cdev.dev)
		sysfs_remove_group(&aw8622x->cdev.dev->kobj,
				   &aw8622x_vibrator_attribute_group);

	mutex_destroy(&aw8622x->lock);
	mutex_destroy(&aw8622x->rtp_lock);
	return 0;
}

int aw8622x_suspend_dev(struct device *dev)
{
	struct aw8622x *aw8622x = dev_get_drvdata(dev);

	mutex_lock(&aw8622x->lock);
	hrtimer_cancel(&aw8622x->timer);
	aw8622x->state = 0;
	aw8622x_haptic_stop(aw8622x);
	mutex_unlock(&aw8622x->lock);
	return 0;
}

int aw8622x_resume_dev(struct device *dev)
{
	return 0;
}
