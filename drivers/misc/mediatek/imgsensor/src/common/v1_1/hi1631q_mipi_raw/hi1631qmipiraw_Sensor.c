// SPDX-License-Identifier: GPL-2.0
/*
 * Infinix X698 Stock HI1631Q (0x1631) Sensor Driver
 */

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/types.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "hi1631qmipiraw_Sensor.h"

#define PFX "HI1631Q_camera_sensor"
#define LOG_INF(format, args...) pr_debug(PFX "[%s] " format, __func__, ##args)

static DEFINE_SPINLOCK(imgsensor_drv_lock);

static struct imgsensor_info_struct imgsensor_info = {
	.sensor_id = HI1631Q_SENSOR_ID,
	.checksum_value = 0x6d016475,
	.pre = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 3745,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 2320,
		.grabwindow_height = 1744,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 292800000,
	},
	.cap = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 3745,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4640,
		.grabwindow_height = 3488,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 585600000,
	},
	.cap1 = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 4096,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4640,
		.grabwindow_height = 3488,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 240,
		.mipi_pixel_rate = 585600000,
	},
	.normal_video = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 3745,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 2320,
		.grabwindow_height = 1744,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 300,
		.mipi_pixel_rate = 292800000,
	},
	.hs_video = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 936,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1280,
		.grabwindow_height = 720,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 1200,
		.mipi_pixel_rate = 292800000,
	},
	.slim_video = {
		.pclk = 640000000,
		.linelength = 5696,
		.framelength = 936,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1280,
		.grabwindow_height = 720,
		.mipi_data_lp2hs_settle_dc = 85,
		.max_framerate = 1200,
		.mipi_pixel_rate = 292800000,
	},
	.margin = 4,
	.min_shutter = 4,
	.max_frame_length = 0xffff,
	.min_gain = 64,
	.max_gain = 1024,
	.min_gain_iso = 50,
	.gain_step = 4,
	.exp_step = 2,
	.gain_type = 3,
	.ae_shut_delay_frame = 0,
	.ae_sensor_gain_delay_frame = 0,
	.ae_ispGain_delay_frame = 2,
	.frame_time_delay_frame = 1,
	.ihdr_support = 0,
	.ihdr_le_firstline = 0,
	.sensor_mode_num = 5,
	.cap_delay_frame = 2,
	.pre_delay_frame = 2,
	.video_delay_frame = 2,
	.hs_video_delay_frame = 2,
	.slim_video_delay_frame = 2,
	.isp_driving_current = ISP_DRIVING_4MA,
	.sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,
	.mipi_sensor_type = MIPI_OPHY_NCSI2,
	.mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,
	.sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_Gr,
	.mclk = 24,
	.i2c_speed = 400,
	.mipi_lane_num = SENSOR_MIPI_4_LANE,
	.i2c_addr_table = {0x40, 0xff},
};

static struct imgsensor_struct imgsensor = {
	.mirror = IMAGE_NORMAL,
	.sensor_mode = IMGSENSOR_MODE_INIT,
	.shutter = 0x3D0,
	.gain = 0x40,
	.dummy_pixel = 0,
	.dummy_line = 0,
	.current_fps = 300,
	.autoflicker_en = KAL_FALSE,
	.test_pattern = KAL_FALSE,
	.current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,
	.ihdr_en = 0,
	.i2c_write_id = 0x40,
};

static struct SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[5] = {
	{4672, 3512, 0,    8, 4672, 3496, 2336, 1748,   8, 2, 2320, 1744, 0, 0, 2320, 1744}, /* Preview */
	{4672, 3512, 0,   10, 4672, 3492, 4672, 3492,  16, 2, 4640, 3488, 0, 0, 4640, 3488}, /* Capture */
	{4672, 3512, 0,    8, 4672, 3496, 2336, 1748,   8, 2, 2320, 1744, 0, 0, 2320, 1744}, /* Video */
	{4672, 3512, 0, 1032, 4672, 1448, 2336,  724, 528, 2, 1280,  720, 0, 0, 1280,  720}, /* HS Video */
	{4672, 3512, 0, 1032, 4672, 1448, 2336,  724, 528, 2, 1280,  720, 0, 0, 1280,  720}, /* Slim Video */
};

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
	kal_uint16 get_byte = 0;
	char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF)};

	iReadRegI2C(pu_send_cmd, 2, (u8 *)&get_byte, 1, imgsensor.i2c_write_id);
	return get_byte;
}

static void write_cmos_sensor(kal_uint16 addr, kal_uint16 para)
{
	char pu_send_cmd[4] = {
		(char)(addr >> 8), (char)(addr & 0xFF),
		(char)(para >> 8), (char)(para & 0xFF)
	};

	iWriteRegI2C(pu_send_cmd, 4, imgsensor.i2c_write_id);
}

static void write_cmos_sensor_8(kal_uint16 addr, kal_uint8 para)
{
	char pu_send_cmd[3] = {
		(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)
	};

	iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}

#define MULTI_WRITE 1
#if MULTI_WRITE
#define I2C_BUFFER_LEN 1020
#else
#define I2C_BUFFER_LEN 4
#endif

static kal_uint16 hi1631q_table_write_cmos_sensor(kal_uint16 *para, kal_uint32 len)
{
	char puSendCmd[I2C_BUFFER_LEN];
	kal_uint32 tosend = 0;
	kal_uint32 IDX = 0;
	kal_uint16 addr = 0, addr_last = 0, data = 0;

	while (len > IDX) {
		addr = para[IDX];
		data = para[IDX + 1];
		IDX += 2;

		puSendCmd[tosend++] = (char)(addr >> 8);
		puSendCmd[tosend++] = (char)(addr & 0xFF);
		puSendCmd[tosend++] = (char)(data >> 8);
		puSendCmd[tosend++] = (char)(data & 0xFF);
		addr_last = addr;

		if ((I2C_BUFFER_LEN - tosend) < 4 || IDX == len || addr != addr_last) {
			iBurstWriteReg_multi(puSendCmd, tosend,
				imgsensor.i2c_write_id, 4, imgsensor_info.i2c_speed);
			tosend = 0;
		}
	}
	return 0;
}

static void set_dummy(void)
{
	write_cmos_sensor(0x020e, imgsensor.frame_length & 0xFFFF);
	write_cmos_sensor(0x0206, imgsensor.line_length / 8);
}

static kal_uint32 return_sensor_id(void)
{
	return ((read_cmos_sensor(0x0716) << 8) | read_cmos_sensor(0x0717));
}

static void set_max_framerate(UINT16 framerate, kal_bool min_framelength_en)
{
	kal_uint32 frame_length = imgsensor.frame_length;

	frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
	spin_lock(&imgsensor_drv_lock);
	imgsensor.frame_length = (frame_length > imgsensor.min_frame_length)
		? frame_length : imgsensor.min_frame_length;
	imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length) {
		imgsensor.frame_length = imgsensor_info.max_frame_length;
		imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
	}
	if (min_framelength_en)
		imgsensor.min_frame_length = imgsensor.frame_length;
	spin_unlock(&imgsensor_drv_lock);
	set_dummy();
}

static void write_shutter(kal_uint32 shutter)
{
	kal_uint16 realtime_fps = 0;
	unsigned long flags;

	spin_lock_irqsave(&imgsensor_drv_lock, flags);
	imgsensor.shutter = shutter;
	spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);

	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;
	shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin))
		? (imgsensor_info.max_frame_length - imgsensor_info.margin) : shutter;

	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
		if (realtime_fps >= 297 && realtime_fps <= 305)
			set_max_framerate(296, 0);
		else if (realtime_fps >= 147 && realtime_fps <= 150)
			set_max_framerate(146, 0);
		else
			write_cmos_sensor(0x020e, imgsensor.frame_length);
	} else {
		write_cmos_sensor(0x020e, imgsensor.frame_length);
	}

	write_cmos_sensor_8(0x020D, (shutter & 0xFF0000) >> 16);
	write_cmos_sensor(0x020A, shutter & 0xFFFF);
}

static void set_shutter(kal_uint32 shutter)
{
	write_shutter(shutter);
}

static kal_uint16 gain2reg(const kal_uint16 gain)
{
	return ((gain >> 2) - 16);
}

static kal_uint16 set_gain(kal_uint16 gain)
{
	kal_uint16 reg_gain;

	if (gain < BASEGAIN || gain > 16 * BASEGAIN) {
		if (gain < BASEGAIN)
			gain = BASEGAIN;
		if (gain > 16 * BASEGAIN)
			gain = 16 * BASEGAIN;
	}
	reg_gain = gain2reg(gain);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.gain = reg_gain;
	spin_unlock(&imgsensor_drv_lock);

	write_cmos_sensor_8(0x0213, reg_gain & 0xFF);
	return gain;
}

static void __maybe_unused set_mirror_flip(kal_uint8 image_mirror)
{
	switch (image_mirror) {
	case IMAGE_NORMAL:
		write_cmos_sensor(0x0000, 0x0000);
		break;
	case IMAGE_H_MIRROR:
		write_cmos_sensor(0x0000, 0x0100);
		break;
	case IMAGE_V_MIRROR:
		write_cmos_sensor(0x0000, 0x0200);
		break;
	case IMAGE_HV_MIRROR:
		write_cmos_sensor(0x0000, 0x0300);
		break;
	default:
		break;
	}
}

static kal_uint32 streaming_control(kal_bool enable)
{
	if (enable)
		write_cmos_sensor(0x0b00, 0x0100);
	else
		write_cmos_sensor(0x0b00, 0x0000);
	mdelay(10);
	return ERROR_NONE;
}

static kal_uint16 hi1631q_init_setting[] = {
	0x2000, 0x2000,
	0x2002, 0x23eb,
	0x2004, 0x0170,
	0x2006, 0x40b2,
	0x2008, 0xf27a,
	0x200a, 0x8246,
	0x200c, 0x40b2,
	0x200e, 0xf038,
	0x2010, 0x827a,
	0x2012, 0x40b2,
	0x2014, 0xf0c2,
	0x2016, 0x8266,
	0x2018, 0x40b2,
	0x201a, 0xf10a,
	0x201c, 0x8244,
	0x201e, 0x40b2,
	0x2020, 0xfe00,
	0x2022, 0x826e,
	0x2024, 0x40b2,
	0x2026, 0xf162,
	0x2028, 0x825a,
	0x202a, 0x40b2,
	0x202c, 0xf13e,
	0x202e, 0x8282,
	0x2030, 0x40b2,
	0x2032, 0xf178,
	0x2034, 0x8286,
	0x2036, 0x4130,
	0x2038, 0x120b,
	0x203a, 0x430b,
	0x203c, 0x1292,
	0x203e, 0xb064,
	0x2040, 0x430c,
	0x2042, 0x4c0f,
	0x2044, 0x5f0f,
	0x2046, 0x5f0f,
	0x2048, 0x4c0e,
	0x204a, 0x5e0e,
	0x204c, 0x5e0f,
	0x204e, 0x5e0f,
	0x2050, 0x5e0f,
	0x2052, 0x9f92,
	0x2054, 0x84a8,
	0x2056, 0x842a,
	0x2058, 0x2803,
	0x205a, 0x531c,
	0x205c, 0x922c,
	0x205e, 0x2bf1,
	0x2060, 0x4c0d,
	0x2062, 0x5d0d,
	0x2064, 0x5d0d,
	0x2066, 0x4c0f,
	0x2068, 0x5f0f,
	0x206a, 0x5f0d,
	0x206c, 0x5f0d,
	0x206e, 0x5f0d,
	0x2070, 0x503d,
	0x2072, 0x84aa,
	0x2074, 0x430c,
	0x2076, 0x4c0f,
	0x2078, 0x5f0f,
	0x207a, 0x5d0f,
	0x207c, 0x4f2e,
	0x207e, 0xf03e,
	0x2080, 0x0100,
	0x2082, 0x4fec,
	0x2084, 0x066a,
	0x2086, 0xde0b,
	0x2088, 0x531c,
	0x208a, 0xc312,
	0x208c, 0x100b,
	0x208e, 0x922c,
	0x2090, 0x2bf2,
	0x2092, 0x4b0f,
	0x2094, 0xc312,
	0x2096, 0x100f,
	0x2098, 0x110f,
	0x209a, 0x110f,
	0x209c, 0x110f,
	0x209e, 0xdf4b,
	0x20a0, 0x4bc2,
	0x20a2, 0x06cd,
	0x20a4, 0x4292,
	0x20a6, 0x066a,
	0x20a8, 0x06de,
	0x20aa, 0x4292,
	0x20ac, 0x066c,
	0x20ae, 0x06e0,
	0x20b0, 0x430d,
	0x20b2, 0x403e,
	0x20b4, 0x000c,
	0x20b6, 0x403f,
	0x20b8, 0x8550,
	0x20ba, 0x1292,
	0x20bc, 0x824e,
	0x20be, 0x413b,
	0x20c0, 0x4130,
	0x20c2, 0x120b,
	0x20c4, 0x4f0b,
	0x20c6, 0xb3d2,
	0x20c8, 0x0240,
	0x20ca, 0x2406,
	0x20cc, 0x4292,
	0x20ce, 0x020e,
	0x20d0, 0x7316,
	0x20d2, 0x4292,
	0x20d4, 0x0210,
	0x20d6, 0x7318,
	0x20d8, 0x4b0f,
	0x20da, 0x1292,
	0x20dc, 0xb050,
	0x20de, 0x425f,
	0x20e0, 0x0265,
	0x20e2, 0xf35f,
	0x20e4, 0x9fcb,
	0x20e6, 0x8472,
	0x20e8, 0x240e,
	0x20ea, 0x4b0f,
	0x20ec, 0x5f0f,
	0x20ee, 0x5f0f,
	0x20f0, 0x5f0f,
	0x20f2, 0x4b0e,
	0x20f4, 0x5e0e,
	0x20f6, 0x5e0e,
	0x20f8, 0x5e0f,
	0x20fa, 0x421e,
	0x20fc, 0x8422,
	0x20fe, 0x8eaf,
	0x2100, 0x7544,
	0x2102, 0x5eaf,
	0x2104, 0x7546,
	0x2106, 0x413b,
	0x2108, 0x4130,
	0x210a, 0x1292,
	0x210c, 0xb02e,
	0x210e, 0x425f,
	0x2110, 0x846d,
	0x2112, 0xd25f,
	0x2114, 0x846e,
	0x2116, 0xf37f,
	0x2118, 0x4f82,
	0x211a, 0x7122,
	0x211c, 0x93c2,
	0x211e, 0x83f8,
	0x2120, 0x240d,
	0x2122, 0x4292,
	0x2124, 0x7316,
	0x2126, 0x8484,
	0x2128, 0x4292,
	0x212a, 0x7318,
	0x212c, 0x8486,
	0x212e, 0x4392,
	0x2130, 0x7f0c,
	0x2132, 0x4392,
	0x2134, 0x7f10,
	0x2136, 0x413f,
	0x2138, 0x4030,
	0x213a, 0xb244,
	0x213c, 0x4130,
	0x213e, 0x4292,
	0x2140, 0x731a,
	0x2142, 0x130a,
	0x2144, 0x93c2,
	0x2146, 0x846d,
	0x2148, 0x2403,
	0x214a, 0xd0f2,
	0x214c, 0x0010,
	0x214e, 0x0205,
	0x2150, 0x12b0,
	0x2152, 0xf284,
	0x2154, 0x93c2,
	0x2156, 0x846d,
	0x2158, 0x2403,
	0x215a, 0xf0f2,
	0x215c, 0xffef,
	0x215e, 0x0205,
	0x2160, 0x4130,
	0x2162, 0xb3d2,
	0x2164, 0x0600,
	0x2166, 0x2403,
	0x2168, 0xd3d2,
	0x216a, 0x0601,
	0x216c, 0x3c02,
	0x216e, 0xc3d2,
	0x2170, 0x0601,
	0x2172, 0x1292,
	0x2174, 0xb044,
	0x2176, 0x4130,
	0x2178, 0x120b,
	0x217a, 0x120a,
	0x217c, 0x1209,
	0x217e, 0x1208,
	0x2180, 0x1207,
	0x2182, 0x1206,
	0x2184, 0xb0f2,
	0x2186, 0x0040,
	0x2188, 0x0b05,
	0x218a, 0x246e,
	0x218c, 0xb2f2,
	0x218e, 0x0b05,
	0x2190, 0x2469,
	0x2192, 0x432a,
	0x2194, 0x4308,
	0x2196, 0x4259,
	0x2198, 0x0605,
	0x219a, 0xd392,
	0x219c, 0x7a12,
	0x219e, 0x4217,
	0x21a0, 0x0428,
	0x21a2, 0x4216,
	0x21a4, 0x042a,
	0x21a6, 0x8706,
	0x21a8, 0x0b00,
	0x21aa, 0x7304,
	0x21ac, 0x0000,
	0x21ae, 0xe279,
	0x21b0, 0x49c2,
	0x21b2, 0x0605,
	0x21b4, 0x0800,
	0x21b6, 0x7a10,
	0x21b8, 0x421f,
	0x21ba, 0x7100,
	0x21bc, 0x870f,
	0x21be, 0x4382,
	0x21c0, 0x0412,
	0x21c2, 0x9f06,
	0x21c4, 0x281c,
	0x21c6, 0x9382,
	0x21c8, 0x7106,
	0x21ca, 0x2419,
	0x21cc, 0x425b,
	0x21ce, 0x8470,
	0x21d0, 0xff4b,
	0x21d2, 0x434d,
	0x21d4, 0x9382,
	0x21d6, 0x8430,
	0x21d8, 0x2412,
	0x21da, 0x421c,
	0x21dc, 0x8434,
	0x21de, 0x4d4f,
	0x21e0, 0x4f0e,
	0x21e2, 0x5e0e,
	0x21e4, 0x5e0f,
	0x21e6, 0x5f0f,
	0x21e8, 0x5c0f,
	0x21ea, 0x4f6f,
	0x21ec, 0xf25f,
	0x21ee, 0x8470,
	0x21f0, 0x9b4f,
	0x21f2, 0x2428,
	0x21f4, 0x535d,
	0x21f6, 0x4d4f,
	0x21f8, 0x921f,
	0x21fa, 0x8430,
	0x21fc, 0x2bf0,
	0x21fe, 0x403b,
	0x2200, 0x823e,
	0x2202, 0x480f,
	0x2204, 0x5f0f,
	0x2206, 0x580f,
	0x2208, 0x5f0f,
	0x220a, 0x503f,
	0x220c, 0x8550,
	0x220e, 0x403d,
	0x2210, 0x0003,
	0x2212, 0x403e,
	0x2214, 0x0412,
	0x2216, 0x12ab,
	0x2218, 0x5318,
	0x221a, 0xf038,
	0x221c, 0x0003,
	0x221e, 0x4a0f,
	0x2220, 0x5f0f,
	0x2222, 0x5a0f,
	0x2224, 0x5f0f,
	0x2226, 0x503f,
	0x2228, 0x8550,
	0x222a, 0x403d,
	0x222c, 0x0003,
	0x222e, 0x4f0e,
	0x2230, 0x403f,
	0x2232, 0x042e,
	0x2234, 0x12ab,
	0x2236, 0x531a,
	0x2238, 0xf03a,
	0x223a, 0x0003,
	0x223c, 0x9382,
	0x223e, 0x7112,
	0x2240, 0x27b3,
	0x2242, 0x3c14,
	0x2244, 0x403d,
	0x2246, 0x0412,
	0x2248, 0x4e0f,
	0x224a, 0x5f0f,
	0x224c, 0x5e0f,
	0x224e, 0x4c0e,
	0x2250, 0x5f0e,
	0x2252, 0x434f,
	0x2254, 0x4ebd,
	0x2256, 0x0000,
	0x2258, 0x532d,
	0x225a, 0x535f,
	0x225c, 0x907f,
	0x225e, 0x0003,
	0x2260, 0x2bf9,
	0x2262, 0x3fcd,
	0x2264, 0x430a,
	0x2266, 0x3f96,
	0x2268, 0x1292,
	0x226a, 0xb070,
	0x226c, 0x4136,
	0x226e, 0x4137,
	0x2270, 0x4138,
	0x2272, 0x4139,
	0x2274, 0x413a,
	0x2276, 0x413b,
	0x2278, 0x4130,
	0x227a, 0x43c2,
	0x227c, 0x0700,
	0x227e, 0x1292,
	0x2280, 0xb030,
	0x2282, 0x4130,
	0x2284, 0x422d,
	0x2286, 0x403e,
	0x2288, 0xf298,
	0x228a, 0x403f,
	0x228c, 0x83d6,
	0x228e, 0x12b0,
	0x2290, 0xb75c,
	0x2292, 0x1292,
	0x2294, 0xb06c,
	0x2296, 0x4130,
	0x2298, 0x0022,
	0x229a, 0x1133,
	0x229c, 0x4466,
	0x229e, 0x5577,
	0x2e00, 0x425f,
	0x2e02, 0x0250,
	0x2e04, 0xf37f,
	0x2e06, 0x421e,
	0x2e08, 0x0254,
	0x2e0a, 0x903f,
	0x2e0c, 0x0003,
	0x2e0e, 0x2405,
	0x2e10, 0xc3d2,
	0x2e12, 0x0255,
	0x2e14, 0x0900,
	0x2e16, 0x732c,
	0x2e18, 0x4130,
	0x2e1a, 0x120e,
	0x2e1c, 0x403d,
	0x2e1e, 0xfe32,
	0x2e20, 0x120d,
	0x2e22, 0x120b,
	0x2e24, 0x120a,
	0x2e26, 0x1209,
	0x2e28, 0x421b,
	0x2e2a, 0x0256,
	0x2e2c, 0x4f0c,
	0x2e2e, 0x4030,
	0x2e30, 0xd72a,
	0x2e32, 0x413e,
	0x2e34, 0xd03e,
	0x2e36, 0x0009,
	0x2e38, 0x4e82,
	0x2e3a, 0x0254,
	0x2e3c, 0x4130,
	0x2ffe, 0xb000,
	0x33de, 0x84d0,
	0x33e0, 0x0000,
	0x33e2, 0x0000,
	0x33e4, 0x0000,
	0x33e6, 0x0000,
	0x33e8, 0x0000,
	0x33ea, 0x0000,
	0x33ec, 0x0000,
	0x34a8, 0x0042,
	0x34aa, 0x0000,
	0x34ac, 0x0000,
	0x34ae, 0x0000,
	0x34b0, 0x0000,
	0x34b2, 0x0064,
	0x34b4, 0x0000,
	0x34b6, 0x0000,
	0x34b8, 0x0000,
	0x34ba, 0x0000,
	0x34bc, 0x00c8,
	0x34be, 0x0000,
	0x34c0, 0x0000,
	0x34c2, 0x0000,
	0x34c4, 0x0000,
	0x34c6, 0x012c,
	0x34c8, 0x0000,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x0000,
	0x34d0, 0x0040,
	0x34d2, 0x0040,
	0x34d4, 0x003f,
	0x34d6, 0x003f,
	0x34d8, 0x003e,
	0x34da, 0x003d,
	0x34dc, 0x003c,
	0x34de, 0x003c,
	0x34e0, 0x003c,
	0x34e2, 0x003c,
	0x34e4, 0x003d,
	0x34e6, 0x003e,
	0x34e8, 0x003f,
	0x34ea, 0x0040,
	0x34ec, 0x0040,
	0x34ee, 0x0042,
	0x34f0, 0x0044,
	0x34f2, 0x0046,
	0x34f4, 0x0048,
	0x34f6, 0x004a,
	0x34f8, 0x004c,
	0x34fa, 0x004e,
	0x34fc, 0x0050,
	0x34fe, 0x0054,
	0x3500, 0x0058,
	0x3502, 0x005c,
	0x3504, 0x0062,
	0x3506, 0x0068,
	0x3508, 0x006e,
	0x350a, 0x0074,
	0x350c, 0x007a,
	0x350e, 0x0080,
	0x3510, 0x0040,
	0x3512, 0x0040,
	0x3514, 0x0042,
	0x3516, 0x0045,
	0x3518, 0x0049,
	0x351a, 0x004e,
	0x351c, 0x0053,
	0x351e, 0x0058,
	0x3520, 0x005d,
	0x3522, 0x0062,
	0x3524, 0x0067,
	0x3526, 0x006c,
	0x3528, 0x0071,
	0x352a, 0x0076,
	0x352c, 0x0078,
	0x352e, 0x0080,
	0x3530, 0x0085,
	0x3532, 0x008a,
	0x3534, 0x008f,
	0x3536, 0x0094,
	0x3538, 0x0099,
	0x353a, 0x009e,
	0x353c, 0x00a3,
	0x353e, 0x00a8,
	0x3540, 0x00ad,
	0x3542, 0x00b2,
	0x3544, 0x00b7,
	0x3546, 0x00bc,
	0x3548, 0x00c1,
	0x354a, 0x00c6,
	0x354c, 0x00c8,
	0x354e, 0x00d0,
	0xbffe, 0x8000,
	0x0510, 0x007f,
	0x0512, 0x007f,
	0x0514, 0x007f,
	0x0516, 0x007f,
	0x0260, 0x0021,
	0x0266, 0x000b,
	0x026a, 0x0064,
	0x0256, 0x0100,
	0x0258, 0x0002,
	0x025a, 0x03e8,
	0x025c, 0x0002,
	0x0250, 0x0000,
	0x0254, 0x0010,
	0x0354, 0x1a00,
	0x0410, 0x2000,
	0x043a, 0x0031,
	0x0560, 0x90f0,
	0x0900, 0x0002,
	0x0910, 0x0002,
	0x0912, 0x0102,
	0x170a, 0x0041,
	0x1714, 0x004a,
	0x1716, 0xb6e7,
	0x1718, 0x0001,
	0x17b0, 0x0050,
	0x17b2, 0x029d,
	0x17b4, 0x0001,
	0x17b8, 0x0002,
	0x171a, 0x017c,
	0x171c, 0x00ee,
	0x1720, 0x0041,
	0x0a04, 0xb400,
	0x0a06, 0xd400,
	0x0a08, 0xc88c,
	0x0a0a, 0xc381,
	0x0a0c, 0xf750,
	0x0a0e, 0xfcd8,
	0x0a10, 0xc040,
	0x0a12, 0x1700,
	0x0a20, 0x0015,
	0x0600, 0x11b1,
	0x0602, 0x0152,
	0x0606, 0x01eb,
	0x06b4, 0x3ff0,
	0x06e2, 0xcccc,
	0x0618, 0x0a80,
	0x0666, 0xb050,
	0x0674, 0x03ff,
	0x0b10, 0x403e,
	0x0d00, 0x400b,
	0x0e1e, 0x0001,
	0x0736, 0x500b,
	0x0738, 0xf030,
	0x0730, 0xb74b,
	0x0708, 0x2f80,
	0x070c, 0x01ff,
	0x0740, 0x008f,
	0x0750, 0x0100,
	0x1002, 0xc319,
	0x1048, 0x0000,
	0x1044, 0x0000,
	0x1046, 0x0004,
	0x105a, 0x0001,
	0x105c, 0x0f00,
	0x1060, 0x3408,
	0x027e, 0x0100,
};

static kal_uint16 hi1631q_preview_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x0121,
	0x34ac, 0x0121,
	0x34ae, 0x0121,
	0x34b0, 0x0121,
	0x34b2, 0x004a,
	0x34b4, 0x011d,
	0x34b6, 0x011d,
	0x34b8, 0x011d,
	0x34ba, 0x011d,
	0x34bc, 0x0050,
	0x34be, 0x0109,
	0x34c0, 0x010f,
	0x34c2, 0x010f,
	0x34c4, 0x0109,
	0x34c6, 0x1024,
	0x34c8, 0x000a,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x000a,
	0x34d0, 0x0040,
	0x34d2, 0x003e,
	0x34d4, 0x003c,
	0x34d6, 0x003a,
	0x34d8, 0x0038,
	0x34da, 0x0034,
	0x34dc, 0x0030,
	0x34de, 0x002e,
	0x34e0, 0x002d,
	0x34e2, 0x002c,
	0x34e4, 0x002a,
	0x34e6, 0x0028,
	0x34e8, 0x0026,
	0x34ea, 0x0024,
	0x34ec, 0x0022,
	0x34ee, 0x0020,
	0x34f0, 0x001e,
	0x34f2, 0x001e,
	0x34f4, 0x001e,
	0x34f6, 0x001e,
	0x34f8, 0x001e,
	0x34fa, 0x001e,
	0x34fc, 0x001e,
	0x34fe, 0x001e,
	0x3500, 0x001e,
	0x3502, 0x001e,
	0x3504, 0x001e,
	0x3506, 0x0022,
	0x3508, 0x0026,
	0x350a, 0x002a,
	0x350c, 0x002e,
	0x350e, 0x0032,
	0x0206, 0x02c8,
	0x0204, 0x0009,
	0x020a, 0x0e9d,
	0x020e, 0x0ea1,
	0x0212, 0x0000,
	0x022a, 0x0016,
	0x022c, 0x0e23,
	0x0224, 0x004c,
	0x022e, 0x0df2,
	0x0234, 0x2222,
	0x0238, 0x2222,
	0x023a, 0x4242,
	0x0268, 0x00ff,
	0x0440, 0x001d,
	0x1710, 0x0811,
	0x1712, 0x0098,
	0x17b6, 0x0040,
	0x06cc, 0x00ff,
	0x0b04, 0x0034,
	0x0b12, 0x0910,
	0x0b14, 0x06d0,
	0x0b20, 0x5200,
	0x0f00, 0x0400,
	0x0f06, 0x0002,
	0x0f04, 0x0010,
	0x0f08, 0x0011,
	0x0f0a, 0x2233,
	0x0f0c, 0x4455,
	0x0f0e, 0x6677,
	0x0732, 0xb9b0,
	0x103e, 0x0100,
	0x1010, 0x00a5,
	0x1012, 0x0004,
	0x1014, 0x0001,
	0x1016, 0x0001,
	0x1020, 0xc103,
	0x1022, 0x040d,
	0x1024, 0x0304,
	0x1026, 0x0505,
	0x1028, 0x0b04,
	0x102a, 0x0507,
	0x102c, 0x0a0a,
	0x0b00, 0x0100,
};

static kal_uint16 hi1631q_capture_30_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x001a,
	0x34ac, 0x001c,
	0x34ae, 0x001c,
	0x34b0, 0x001a,
	0x34b2, 0x0050,
	0x34b4, 0x0023,
	0x34b6, 0x001b,
	0x34b8, 0x001b,
	0x34ba, 0x0023,
	0x34bc, 0x0050,
	0x34be, 0x0025,
	0x34c0, 0x000e,
	0x34c2, 0x000e,
	0x34c4, 0x0025,
	0x34c6, 0x1024,
	0x34c8, 0x0026,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x002a,
	0x34d0, 0x0040,
	0x34d2, 0x0040,
	0x34d4, 0x003f,
	0x34d6, 0x003f,
	0x34d8, 0x003e,
	0x34da, 0x003d,
	0x34dc, 0x003c,
	0x34de, 0x003c,
	0x34e0, 0x003c,
	0x34e2, 0x003c,
	0x34e4, 0x003d,
	0x34e6, 0x003e,
	0x34e8, 0x003f,
	0x34ea, 0x0040,
	0x34ec, 0x0040,
	0x34ee, 0x0042,
	0x34f0, 0x0044,
	0x34f2, 0x0046,
	0x34f4, 0x0048,
	0x34f6, 0x004a,
	0x34f8, 0x004c,
	0x34fa, 0x004e,
	0x34fc, 0x0050,
	0x34fe, 0x0054,
	0x3500, 0x0058,
	0x3502, 0x005c,
	0x3504, 0x0062,
	0x3506, 0x0068,
	0x3508, 0x006e,
	0x350a, 0x0074,
	0x350c, 0x007a,
	0x350e, 0x0080,
	0x0206, 0x02c8,
	0x0204, 0x0008,
	0x020a, 0x0e9d,
	0x020e, 0x0ea1,
	0x0212, 0x0000,
	0x022a, 0x0017,
	0x022c, 0x0e21,
	0x0224, 0x0050,
	0x022e, 0x0def,
	0x0234, 0x1111,
	0x0238, 0x1111,
	0x023a, 0x2121,
	0x0268, 0x00ff,
	0x0440, 0x001d,
	0x1710, 0x0ee3,
	0x1712, 0x009a,
	0x17b6, 0x0000,
	0x06cc, 0x0000,
	0x0b04, 0x0014,
	0x0b12, 0x1220,
	0x0b14, 0x0da0,
	0x0b20, 0x0100,
	0x0f00, 0x0000,
	0x0f06, 0x0000,
	0x0f04, 0x0020,
	0x0f08, 0x0022,
	0x0f0a, 0x1133,
	0x0f0c, 0x4466,
	0x0f0e, 0x5577,
	0x0732, 0xb8b0,
	0x103e, 0x0000,
	0x1010, 0x02e4,
	0x1012, 0x001d,
	0x1014, 0x0001,
	0x1016, 0x0001,
	0x1020, 0xc107,
	0x1022, 0x071b,
	0x1024, 0x0406,
	0x1026, 0x0908,
	0x1028, 0x1207,
	0x102a, 0x0a0d,
	0x102c, 0x0a0a,
	0x0b00, 0x0100,
};

static kal_uint16 hi1631q_capture_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x001a,
	0x34ac, 0x001c,
	0x34ae, 0x001c,
	0x34b0, 0x001a,
	0x34b2, 0x0050,
	0x34b4, 0x0023,
	0x34b6, 0x001b,
	0x34b8, 0x001b,
	0x34ba, 0x0023,
	0x34bc, 0x0050,
	0x34be, 0x0025,
	0x34c0, 0x000e,
	0x34c2, 0x000e,
	0x34c4, 0x0025,
	0x34c6, 0x1024,
	0x34c8, 0x0026,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x002a,
	0x34d0, 0x0040,
	0x34d2, 0x0040,
	0x34d4, 0x003f,
	0x34d6, 0x003f,
	0x34d8, 0x003e,
	0x34da, 0x003d,
	0x34dc, 0x003c,
	0x34de, 0x003c,
	0x34e0, 0x003c,
	0x34e2, 0x003c,
	0x34e4, 0x003d,
	0x34e6, 0x003e,
	0x34e8, 0x003f,
	0x34ea, 0x0040,
	0x34ec, 0x0040,
	0x34ee, 0x0042,
	0x34f0, 0x0044,
	0x34f2, 0x0046,
	0x34f4, 0x0048,
	0x34f6, 0x004a,
	0x34f8, 0x004c,
	0x34fa, 0x004e,
	0x34fc, 0x0050,
	0x34fe, 0x0054,
	0x3500, 0x0058,
	0x3502, 0x005c,
	0x3504, 0x0062,
	0x3506, 0x0068,
	0x3508, 0x006e,
	0x350a, 0x0074,
	0x350c, 0x007a,
	0x350e, 0x0080,
	0x0206, 0x0516,
	0x0204, 0x0008,
	0x020a, 0x0fab,
	0x020e, 0x1000,
	0x0212, 0x0000,
	0x022a, 0x0017,
	0x022c, 0x0e17,
	0x0224, 0x004c,
	0x022e, 0x0df3,
	0x0234, 0x1111,
	0x0238, 0x1111,
	0x023a, 0x2121,
	0x0260, 0x0021,
	0x0268, 0x008c,
	0x026a, 0x0064,
	0x0410, 0x2000,
	0x0440, 0x0010,
	0x1710, 0x0e68,
	0x1712, 0x0122,
	0x1716, 0xf003,
	0x1718, 0x0000,
	0x17b6, 0x0000,
	0x06e2, 0xcccc,
	0x06cc, 0x0000,
	0x0b04, 0x0016,
	0x0b12, 0x1230,
	0x0b14, 0x0da8,
	0x0b20, 0x0100,
	0x0f00, 0x0000,
	0x0f06, 0x0000,
	0x0f04, 0x0018,
	0x0f08, 0x0022,
	0x0f0a, 0x1133,
	0x0f0c, 0x4466,
	0x0f0e, 0x5577,
	0x0d00, 0x400b,
	0x0d2a, 0x125f,
	0x1102, 0x0408,
	0x1108, 0x2006,
	0x1136, 0x2006,
	0x1116, 0x0000,
	0x1118, 0x0000,
	0x1200, 0x09ff,
	0x1204, 0x0000,
	0x120a, 0x0050,
	0x1212, 0x2850,
	0x121e, 0x2150,
	0x1220, 0x0078,
	0x1222, 0x003c,
	0x1224, 0x0104,
	0x1226, 0x1ea0,
	0x0730, 0xc88b,
	0x0732, 0xb9b0,
	0x103e, 0x0000,
	0x1010, 0x0309,
	0x1012, 0x0037,
	0x1014, 0x000b,
	0x1016, 0x000b,
	0x1020, 0xc103,
	0x1022, 0x030e,
	0x1024, 0x0204,
	0x1026, 0x0205,
	0x1028, 0x0904,
	0x102a, 0x0505,
	0x102c, 0x0a0f,
};

static kal_uint16 hi1631q_normal_video_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x0121,
	0x34ac, 0x0121,
	0x34ae, 0x0121,
	0x34b0, 0x0121,
	0x34b2, 0x004a,
	0x34b4, 0x011d,
	0x34b6, 0x011d,
	0x34b8, 0x011d,
	0x34ba, 0x011d,
	0x34bc, 0x0050,
	0x34be, 0x0109,
	0x34c0, 0x010f,
	0x34c2, 0x010f,
	0x34c4, 0x0109,
	0x34c6, 0x1024,
	0x34c8, 0x000a,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x000a,
	0x34d0, 0x0040,
	0x34d2, 0x003e,
	0x34d4, 0x003c,
	0x34d6, 0x003a,
	0x34d8, 0x0038,
	0x34da, 0x0034,
	0x34dc, 0x0030,
	0x34de, 0x002e,
	0x34e0, 0x002d,
	0x34e2, 0x002c,
	0x34e4, 0x002a,
	0x34e6, 0x0028,
	0x34e8, 0x0026,
	0x34ea, 0x0024,
	0x34ec, 0x0022,
	0x34ee, 0x0020,
	0x34f0, 0x001e,
	0x34f2, 0x001e,
	0x34f4, 0x001e,
	0x34f6, 0x001e,
	0x34f8, 0x001e,
	0x34fa, 0x001e,
	0x34fc, 0x001e,
	0x34fe, 0x001e,
	0x3500, 0x001e,
	0x3502, 0x001e,
	0x3504, 0x001e,
	0x3506, 0x0022,
	0x3508, 0x0026,
	0x350a, 0x002a,
	0x350c, 0x002e,
	0x350e, 0x0032,
	0x0206, 0x02c8,
	0x0204, 0x0009,
	0x020a, 0x0e9d,
	0x020e, 0x0ea1,
	0x0212, 0x0000,
	0x022a, 0x0016,
	0x022c, 0x0e23,
	0x0224, 0x004c,
	0x022e, 0x0df2,
	0x0234, 0x2222,
	0x0238, 0x2222,
	0x023a, 0x4242,
	0x0268, 0x00ff,
	0x0440, 0x001d,
	0x1710, 0x0811,
	0x1712, 0x0098,
	0x17b6, 0x0040,
	0x06cc, 0x00ff,
	0x0b04, 0x0034,
	0x0b12, 0x0910,
	0x0b14, 0x06d0,
	0x0b20, 0x5200,
	0x0f00, 0x0400,
	0x0f06, 0x0002,
	0x0f04, 0x0010,
	0x0f08, 0x0011,
	0x0f0a, 0x2233,
	0x0f0c, 0x4455,
	0x0f0e, 0x6677,
	0x0732, 0xb9b0,
	0x103e, 0x0100,
	0x1010, 0x00a5,
	0x1012, 0x0004,
	0x1014, 0x0001,
	0x1016, 0x0001,
	0x1020, 0xc103,
	0x1022, 0x040d,
	0x1024, 0x0304,
	0x1026, 0x0505,
	0x1028, 0x0b04,
	0x102a, 0x0507,
	0x102c, 0x0a0a,
	0x0b00, 0x0100,
};

static kal_uint16 hi1631q_hs_video_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x0121,
	0x34ac, 0x0121,
	0x34ae, 0x0121,
	0x34b0, 0x0121,
	0x34b2, 0x004a,
	0x34b4, 0x011d,
	0x34b6, 0x011d,
	0x34b8, 0x011d,
	0x34ba, 0x011d,
	0x34bc, 0x0050,
	0x34be, 0x0109,
	0x34c0, 0x010f,
	0x34c2, 0x010f,
	0x34c4, 0x0109,
	0x34c6, 0x1024,
	0x34c8, 0x000a,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x000a,
	0x34d0, 0x0040,
	0x34d2, 0x003e,
	0x34d4, 0x003c,
	0x34d6, 0x003a,
	0x34d8, 0x0038,
	0x34da, 0x0034,
	0x34dc, 0x0030,
	0x34de, 0x002e,
	0x34e0, 0x002d,
	0x34e2, 0x002c,
	0x34e4, 0x002a,
	0x34e6, 0x0028,
	0x34e8, 0x0026,
	0x34ea, 0x0024,
	0x34ec, 0x0022,
	0x34ee, 0x0020,
	0x34f0, 0x001e,
	0x34f2, 0x001e,
	0x34f4, 0x001e,
	0x34f6, 0x001e,
	0x34f8, 0x001e,
	0x34fa, 0x001e,
	0x34fc, 0x001e,
	0x34fe, 0x001e,
	0x3500, 0x001e,
	0x3502, 0x001e,
	0x3504, 0x001e,
	0x3506, 0x0022,
	0x3508, 0x0026,
	0x350a, 0x002a,
	0x350c, 0x002e,
	0x350e, 0x0032,
	0x0206, 0x02c8,
	0x0204, 0x0009,
	0x020a, 0x03a4,
	0x020e, 0x03a8,
	0x0212, 0x0000,
	0x022a, 0x0016,
	0x022c, 0x0e23,
	0x0224, 0x044c,
	0x022e, 0x09f2,
	0x0234, 0x2222,
	0x0238, 0x2222,
	0x023a, 0x4242,
	0x0268, 0x00ff,
	0x0440, 0x001d,
	0x1710, 0x0411,
	0x1712, 0x0096,
	0x17b6, 0x0040,
	0x06cc, 0x00ff,
	0x0b04, 0x0034,
	0x0b12, 0x0500,
	0x0b14, 0x02d0,
	0x0b20, 0x5200,
	0x0f00, 0x0400,
	0x0f06, 0x0002,
	0x0f04, 0x0218,
	0x0f08, 0x0011,
	0x0f0a, 0x2233,
	0x0f0c, 0x4455,
	0x0f0e, 0x6677,
	0x0732, 0xb9b0,
	0x103e, 0x0100,
	0x1010, 0x00a5,
	0x1012, 0x00a7,
	0x1014, 0x0042,
	0x1016, 0x0042,
	0x1020, 0xc103,
	0x1022, 0x040d,
	0x1024, 0x0304,
	0x1026, 0x0505,
	0x1028, 0x0b04,
	0x102a, 0x0507,
	0x102c, 0x0a0a,
	0x0b00, 0x0100,
};

static kal_uint16 hi1631q_slim_video_setting[] = {
	0x34a8, 0x0041,
	0x34aa, 0x0121,
	0x34ac, 0x0121,
	0x34ae, 0x0121,
	0x34b0, 0x0121,
	0x34b2, 0x004a,
	0x34b4, 0x011d,
	0x34b6, 0x011d,
	0x34b8, 0x011d,
	0x34ba, 0x011d,
	0x34bc, 0x0050,
	0x34be, 0x0109,
	0x34c0, 0x010f,
	0x34c2, 0x010f,
	0x34c4, 0x0109,
	0x34c6, 0x1024,
	0x34c8, 0x000a,
	0x34ca, 0x0000,
	0x34cc, 0x0000,
	0x34ce, 0x000a,
	0x34d0, 0x0040,
	0x34d2, 0x003e,
	0x34d4, 0x003c,
	0x34d6, 0x003a,
	0x34d8, 0x0038,
	0x34da, 0x0034,
	0x34dc, 0x0030,
	0x34de, 0x002e,
	0x34e0, 0x002d,
	0x34e2, 0x002c,
	0x34e4, 0x002a,
	0x34e6, 0x0028,
	0x34e8, 0x0026,
	0x34ea, 0x0024,
	0x34ec, 0x0022,
	0x34ee, 0x0020,
	0x34f0, 0x001e,
	0x34f2, 0x001e,
	0x34f4, 0x001e,
	0x34f6, 0x001e,
	0x34f8, 0x001e,
	0x34fa, 0x001e,
	0x34fc, 0x001e,
	0x34fe, 0x001e,
	0x3500, 0x001e,
	0x3502, 0x001e,
	0x3504, 0x001e,
	0x3506, 0x0022,
	0x3508, 0x0026,
	0x350a, 0x002a,
	0x350c, 0x002e,
	0x350e, 0x0032,
	0x0206, 0x02c8,
	0x0204, 0x0009,
	0x020a, 0x03a4,
	0x020e, 0x03a8,
	0x0212, 0x0000,
	0x022a, 0x0016,
	0x022c, 0x0e23,
	0x0224, 0x044c,
	0x022e, 0x09f2,
	0x0234, 0x2222,
	0x0238, 0x2222,
	0x023a, 0x4242,
	0x0268, 0x00ff,
	0x0440, 0x001d,
	0x1710, 0x0411,
	0x1712, 0x0096,
	0x17b6, 0x0040,
	0x06cc, 0x00ff,
	0x0b04, 0x0034,
	0x0b12, 0x0500,
	0x0b14, 0x02d0,
	0x0b20, 0x5200,
	0x0f00, 0x0400,
	0x0f06, 0x0002,
	0x0f04, 0x0218,
	0x0f08, 0x0011,
	0x0f0a, 0x2233,
	0x0f0c, 0x4455,
	0x0f0e, 0x6677,
	0x0732, 0xb9b0,
	0x103e, 0x0100,
	0x1010, 0x00a5,
	0x1012, 0x00a7,
	0x1014, 0x0042,
	0x1016, 0x0042,
	0x1020, 0xc103,
	0x1022, 0x040d,
	0x1024, 0x0304,
	0x1026, 0x0505,
	0x1028, 0x0b04,
	0x102a, 0x0507,
	0x102c, 0x0a0a,
	0x0b00, 0x0100,
};

static void sensor_init(void)
{
	hi1631q_table_write_cmos_sensor(hi1631q_init_setting,
		ARRAY_SIZE(hi1631q_init_setting));
}

static void preview_setting(void)
{
	hi1631q_table_write_cmos_sensor(hi1631q_preview_setting,
		ARRAY_SIZE(hi1631q_preview_setting));
}

static void capture_setting(kal_uint16 currefps)
{
	if (currefps == 300)
		hi1631q_table_write_cmos_sensor(hi1631q_capture_30_setting,
			ARRAY_SIZE(hi1631q_capture_30_setting));
	else
		hi1631q_table_write_cmos_sensor(hi1631q_capture_setting,
			ARRAY_SIZE(hi1631q_capture_setting));
}

static void normal_video_setting(kal_uint16 currefps)
{
	hi1631q_table_write_cmos_sensor(hi1631q_normal_video_setting,
		ARRAY_SIZE(hi1631q_normal_video_setting));
}

static void hs_video_setting(void)
{
	hi1631q_table_write_cmos_sensor(hi1631q_hs_video_setting,
		ARRAY_SIZE(hi1631q_hs_video_setting));
}

static void slim_video_setting(void)
{
	hi1631q_table_write_cmos_sensor(hi1631q_slim_video_setting,
		ARRAY_SIZE(hi1631q_slim_video_setting));
}

static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
	kal_uint8 i = 0;
	kal_uint8 retry = 2;

	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			*sensor_id = return_sensor_id();
			if (*sensor_id == imgsensor_info.sensor_id) {
				pr_info("hi1631q i2c write id: 0x%x, sensor id: 0x%x\n",
					imgsensor.i2c_write_id, *sensor_id);
				return ERROR_NONE;
			}
			pr_debug("hi1631q read id fail, read:0x%x id:0x%x\n",
				*sensor_id, imgsensor.i2c_write_id);
			retry--;
		} while (retry > 0);
		i++;
		retry = 2;
	}
	if (*sensor_id != imgsensor_info.sensor_id) {
		*sensor_id = 0xFFFFFFFF;
		return ERROR_SENSOR_CONNECT_FAIL;
	}
	return ERROR_NONE;
}

static kal_uint32 open(void)
{
	kal_uint8 i = 0;
	kal_uint8 retry = 2;
	kal_uint16 sensor_id = 0;

	while (imgsensor_info.i2c_addr_table[i] != 0xff) {
		spin_lock(&imgsensor_drv_lock);
		imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
		spin_unlock(&imgsensor_drv_lock);
		do {
			sensor_id = return_sensor_id();
			if (sensor_id == imgsensor_info.sensor_id)
				break;
			retry--;
		} while (retry > 0);
		i++;
		if (sensor_id == imgsensor_info.sensor_id)
			break;
		retry = 2;
	}
	if (imgsensor_info.sensor_id != sensor_id)
		return ERROR_SENSOR_CONNECT_FAIL;

	sensor_init();
	spin_lock(&imgsensor_drv_lock);
	imgsensor.autoflicker_en = KAL_FALSE;
	imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.dummy_pixel = 0;
	imgsensor.dummy_line = 0;
	imgsensor.ihdr_en = 0;
	imgsensor.test_pattern = KAL_FALSE;
	imgsensor.current_fps = imgsensor_info.pre.max_framerate;
	spin_unlock(&imgsensor_drv_lock);

	return ERROR_NONE;
}

static kal_uint32 close(void)
{
	return ERROR_NONE;
}

static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	preview_setting();
	return ERROR_NONE;
}

static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
	if (imgsensor.current_fps == imgsensor_info.cap.max_framerate) {
		imgsensor.pclk = imgsensor_info.cap.pclk;
		imgsensor.line_length = imgsensor_info.cap.linelength;
		imgsensor.frame_length = imgsensor_info.cap.framelength;
		imgsensor.min_frame_length = imgsensor_info.cap.framelength;
	} else {
		imgsensor.pclk = imgsensor_info.cap1.pclk;
		imgsensor.line_length = imgsensor_info.cap1.linelength;
		imgsensor.frame_length = imgsensor_info.cap1.framelength;
		imgsensor.min_frame_length = imgsensor_info.cap1.framelength;
	}
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	capture_setting(imgsensor.current_fps);
	return ERROR_NONE;
}

static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			       MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
	imgsensor.pclk = imgsensor_info.normal_video.pclk;
	imgsensor.line_length = imgsensor_info.normal_video.linelength;
	imgsensor.frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	normal_video_setting(imgsensor.current_fps);
	return ERROR_NONE;
}

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			   MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
	imgsensor.pclk = imgsensor_info.hs_video.pclk;
	imgsensor.line_length = imgsensor_info.hs_video.linelength;
	imgsensor.frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	hs_video_setting();
	return ERROR_NONE;
}

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			     MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
	imgsensor.pclk = imgsensor_info.slim_video.pclk;
	imgsensor.line_length = imgsensor_info.slim_video.linelength;
	imgsensor.frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	slim_video_setting();
	return ERROR_NONE;
}

static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *sensor_resolution)
{
	sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
	sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;
	sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
	sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;
	sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
	sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;
	sensor_resolution->SensorHighSpeedVideoWidth = imgsensor_info.hs_video.grabwindow_width;
	sensor_resolution->SensorHighSpeedVideoHeight = imgsensor_info.hs_video.grabwindow_height;
	sensor_resolution->SensorSlimVideoWidth = imgsensor_info.slim_video.grabwindow_width;
	sensor_resolution->SensorSlimVideoHeight = imgsensor_info.slim_video.grabwindow_height;
	return ERROR_NONE;
}

static kal_uint32 get_info(enum MSDK_SCENARIO_ID_ENUM scenario_id,
			   MSDK_SENSOR_INFO_STRUCT *sensor_info,
			   MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
	sensor_info->SensorInterruptDelayLines = 4;
	sensor_info->SensorResetActiveHigh = FALSE;
	sensor_info->SensorResetDelayCount = 5;

	sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
	sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
	sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
	sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

	sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
	sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
	sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
	sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
	sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;

	sensor_info->SensorMasterClockSwitch = 0;
	sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;
	sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;
	sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;
	sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
	sensor_info->FrameTimeDelayFrame = imgsensor_info.frame_time_delay_frame;
	sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
	sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
	sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;
	sensor_info->PDAF_Support = 0;
	sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
	sensor_info->SensorClockFreq = imgsensor_info.mclk;
	sensor_info->SensorClockDividCount = 3;
	sensor_info->SensorClockRisingCount = 0;
	sensor_info->SensorClockFallingCount = 2;
	sensor_info->SensorPixelClockCount = 3;
	sensor_info->SensorDataLatchCount = 2;
	sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
	sensor_info->SensorWidthSampling = 0;
	sensor_info->SensorHightSampling = 0;
	sensor_info->SensorPacketECCOrder = 1;

	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.cap.mipi_data_lp2hs_settle_dc;
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc;
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;
		break;
	default:
		sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
		sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;
		sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount =
			imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
		break;
	}
	return ERROR_NONE;
}

static kal_uint32 control(enum MSDK_SCENARIO_ID_ENUM scenario_id,
			  MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
			  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.current_scenario_id = scenario_id;
	spin_unlock(&imgsensor_drv_lock);

	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		preview(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		capture(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		normal_video(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		hs_video(image_window, sensor_config_data);
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		slim_video(image_window, sensor_config_data);
		break;
	default:
		preview(image_window, sensor_config_data);
		return ERROR_INVALID_SCENARIO_ID;
	}
	return ERROR_NONE;
}

static kal_uint32 set_video_mode(UINT16 framerate)
{
	spin_lock(&imgsensor_drv_lock);
	if (framerate == 0) {
		spin_unlock(&imgsensor_drv_lock);
		return ERROR_NONE;
	}
	imgsensor.current_fps = framerate;
	spin_unlock(&imgsensor_drv_lock);
	set_max_framerate(imgsensor.current_fps, 1);
	return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
	spin_lock(&imgsensor_drv_lock);
	imgsensor.autoflicker_en = enable ? KAL_TRUE : KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}

static kal_uint32 set_max_framerate_by_scenario(
		enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate)
{
	kal_uint32 frame_length;

	if (framerate == 0)
		return ERROR_NONE;

	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength)
			? (frame_length - imgsensor_info.pre.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		frame_length = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength)
			? (frame_length - imgsensor_info.normal_video.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength)
			? (frame_length - imgsensor_info.cap.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		frame_length = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.hs_video.framelength)
			? (frame_length - imgsensor_info.hs_video.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength)
			? (frame_length - imgsensor_info.slim_video.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	default:
		frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
		spin_lock(&imgsensor_drv_lock);
		imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength)
			? (frame_length - imgsensor_info.pre.framelength) : 0;
		imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
		imgsensor.min_frame_length = imgsensor.frame_length;
		spin_unlock(&imgsensor_drv_lock);
		if (imgsensor.frame_length > imgsensor.shutter)
			set_dummy();
		break;
	}
	return ERROR_NONE;
}

static kal_uint32 get_default_framerate_by_scenario(
		enum MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
	switch (scenario_id) {
	case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		*framerate = imgsensor_info.pre.max_framerate;
		break;
	case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		*framerate = imgsensor_info.normal_video.max_framerate;
		break;
	case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		*framerate = imgsensor_info.cap.max_framerate;
		break;
	case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		*framerate = imgsensor_info.hs_video.max_framerate;
		break;
	case MSDK_SCENARIO_ID_SLIM_VIDEO:
		*framerate = imgsensor_info.slim_video.max_framerate;
		break;
	default:
		break;
	}
	return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
	if (enable) {
		write_cmos_sensor(0x0b04, 0x00D9);
		write_cmos_sensor(0x0C0a, 0x0100);
	} else {
		write_cmos_sensor(0x0b04, 0x009C);
		write_cmos_sensor(0x0C0a, 0x0000);
	}
	spin_lock(&imgsensor_drv_lock);
	imgsensor.test_pattern = enable;
	spin_unlock(&imgsensor_drv_lock);
	return ERROR_NONE;
}

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
				  UINT8 *feature_para, UINT32 *feature_para_len)
{
	UINT16 *feature_return_para_16 = (UINT16 *)feature_para;
	UINT16 *feature_data_16 = (UINT16 *)feature_para;
	UINT32 *feature_return_para_32 = (UINT32 *)feature_para;
	UINT32 *feature_data_32 = (UINT32 *)feature_para;
	unsigned long long *feature_data = (unsigned long long *)feature_para;
	struct SENSOR_WINSIZE_INFO_STRUCT *wininfo;
	MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data =
		(MSDK_SENSOR_REG_INFO_STRUCT *)feature_para;

	switch (feature_id) {
	case SENSOR_FEATURE_GET_GAIN_RANGE_BY_SCENARIO:
		*(feature_data + 1) = imgsensor_info.min_gain;
		*(feature_data + 2) = imgsensor_info.max_gain;
		break;
	case SENSOR_FEATURE_GET_BASE_GAIN_ISO_AND_STEP:
		*(feature_data + 0) = imgsensor_info.min_gain_iso;
		*(feature_data + 1) = imgsensor_info.gain_step;
		*(feature_data + 2) = imgsensor_info.gain_type;
		break;
	case SENSOR_FEATURE_GET_MIN_SHUTTER_BY_SCENARIO:
		*(feature_data + 1) = imgsensor_info.min_shutter;
		*(feature_data + 2) = imgsensor_info.exp_step;
		break;
	case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ_BY_SCENARIO:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.cap.pclk;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.normal_video.pclk;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.hs_video.pclk;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.slim_video.pclk;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.pre.pclk;
			break;
		}
		break;
	case SENSOR_FEATURE_GET_PERIOD_BY_SCENARIO:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				(imgsensor_info.cap.framelength << 16) + imgsensor_info.cap.linelength;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				(imgsensor_info.normal_video.framelength << 16) + imgsensor_info.normal_video.linelength;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				(imgsensor_info.hs_video.framelength << 16) + imgsensor_info.hs_video.linelength;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				(imgsensor_info.slim_video.framelength << 16) + imgsensor_info.slim_video.linelength;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) =
				(imgsensor_info.pre.framelength << 16) + imgsensor_info.pre.linelength;
			break;
		}
		break;
	case SENSOR_FEATURE_GET_PERIOD:
		*feature_return_para_16++ = imgsensor.line_length;
		*feature_return_para_16 = imgsensor.frame_length;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
		*feature_return_para_32 = imgsensor.pclk;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_ESHUTTER:
		set_shutter(*feature_data);
		break;
	case SENSOR_FEATURE_SET_NIGHTMODE:
		break;
	case SENSOR_FEATURE_SET_GAIN:
		set_gain((UINT16)*feature_data);
		break;
	case SENSOR_FEATURE_SET_FLASHLIGHT:
		break;
	case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
		break;
	case SENSOR_FEATURE_SET_REGISTER:
		write_cmos_sensor_8(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
		break;
	case SENSOR_FEATURE_GET_REGISTER:
		sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
		break;
	case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
		*feature_return_para_32 = LENS_DRIVER_ID_DO_NOT_CARE;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_VIDEO_MODE:
		set_video_mode(*feature_data);
		break;
	case SENSOR_FEATURE_CHECK_SENSOR_ID:
		get_imgsensor_id(feature_return_para_32);
		break;
	case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
		set_auto_flicker_mode((BOOL)*feature_data_16, *(feature_data_16 + 1));
		break;
	case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
		set_max_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*feature_data, *(feature_data + 1));
		break;
	case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
		get_default_framerate_by_scenario((enum MSDK_SCENARIO_ID_ENUM)*(feature_data),
			(MUINT32 *)(uintptr_t)(*(feature_data + 1)));
		break;
	case SENSOR_FEATURE_SET_TEST_PATTERN:
		set_test_pattern_mode((BOOL)*feature_data);
		break;
	case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE:
		*feature_return_para_32 = imgsensor_info.checksum_value;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_FRAMERATE:
		spin_lock(&imgsensor_drv_lock);
		imgsensor.current_fps = *feature_data_32;
		spin_unlock(&imgsensor_drv_lock);
		break;
	case SENSOR_FEATURE_SET_HDR:
		spin_lock(&imgsensor_drv_lock);
		imgsensor.ihdr_en = (BOOL)*feature_data_32;
		spin_unlock(&imgsensor_drv_lock);
		break;
	case SENSOR_FEATURE_GET_CROP_INFO:
		wininfo = (struct SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data + 1));
		switch (*feature_data_32) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[1],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[2],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[3],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[4],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			memcpy((void *)wininfo, (void *)&imgsensor_winsize_info[0],
				sizeof(struct SENSOR_WINSIZE_INFO_STRUCT));
			break;
		}
		break;
	case SENSOR_FEATURE_GET_TEMPERATURE_VALUE:
		*feature_return_para_32 = 0;
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_SET_STREAMING_SUSPEND:
		streaming_control(KAL_FALSE);
		break;
	case SENSOR_FEATURE_SET_STREAMING_RESUME:
		if (*feature_data != 0)
			set_shutter(*feature_data);
		streaming_control(KAL_TRUE);
		break;
	case SENSOR_FEATURE_GET_MIPI_PIXEL_RATE:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.cap.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.normal_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.hs_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.slim_video.mipi_pixel_rate;
			break;
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = imgsensor_info.pre.mipi_pixel_rate;
			break;
		}
		break;
	case SENSOR_FEATURE_GET_PIXEL_RATE:
		switch (*feature_data) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 528774400;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 264387200;
			break;
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 145868800;
			break;
		}
		break;
	case SENSOR_FEATURE_GET_BINNING_TYPE:
		switch (*(feature_data + 1)) {
		case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
			*feature_return_para_32 = 1;
			break;
		case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
		case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
		case MSDK_SCENARIO_ID_SLIM_VIDEO:
		case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
		default:
			*feature_return_para_32 = 4;
			break;
		}
		*feature_para_len = 4;
		break;
	case SENSOR_FEATURE_GET_AWB_REQ_BY_SCENARIO:
		*(MUINT32 *)(uintptr_t)(*(feature_data + 1)) = 0;
		break;
	default:
		break;
	}
	return ERROR_NONE;
}

static struct SENSOR_FUNCTION_STRUCT sensor_func = {
	open,
	get_info,
	get_resolution,
	feature_control,
	control,
	close
};

UINT32 HI1631Q_MIPI_RAW_SensorInit(struct SENSOR_FUNCTION_STRUCT **pfFunc)
{
	if (pfFunc != NULL)
		*pfFunc = &sensor_func;
	return ERROR_NONE;
}
