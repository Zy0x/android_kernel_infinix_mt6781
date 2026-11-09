/*
 * Transsion BOE TXD 120Hz Novatek NT36672C Panel Driver for Infinix Note 11 Pro (X698)
 * Reconstructed 100% byte-for-byte from stock Infinix X698 factory kernel binary
 * Build #42:
 *   - Fix 1: Single-byte MIPI DCS Exit Sleep (0x11) & Display On (0x29) (fixes blank screen)
 *   - Fix 2: Single-byte MIPI DCS Display Off (0x28) & Enter Sleep (0x10) in lcm_unprepare
 *   - Fix 3: Dual-slice hardware DSC compression configuration in ext_params (fixes horizontal scanlines/glitch)
 *   - Fix 4: Authentic dynamic timings (.switch_en = 1, .vfp_lp_dyn = 0, .data_rate = 900)
 *   - Fix 5: LED class fallback in lcm_enable() — ctx->backlight is always NULL because stock
 *            Transsion X698 uses mediatek,disp-leds (LED class, NOT backlight class) and
 *            panel DTS node has no "backlight" phandle. Fallback calls led_classdev
 *            "lcd-backlight" directly via led_set_brightness_sync() to trigger initial brightness.
 */

#include <linux/backlight.h>
#include <linux/leds.h>
/* extern symbols from led-core.c for leds_list iteration */
extern struct list_head leds_list;
extern struct rw_semaphore leds_list_lock;
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/fb.h>

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;

	bool prepared;
	bool enabled;
	int error;
};

#define panel_to_lcm(p) container_of(p, struct lcm, panel)

/* Authentic stock Novatek NT36672C Transsion BOE 120Hz packet table */

enum pkt_type { PKT_GENERIC = 0, PKT_DCS = 1 };

struct panel_init_cmd {
	enum pkt_type type;
	size_t len;
	u8 data[32];
	u16 delay_ms;
};

static const struct panel_init_cmd authentic_init_cmds[257] = {
	/*   0 */ { PKT_GENERIC, 2, { 0xff, 0x10 }, 0 },
	/*   1 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/*   2 */ { PKT_GENERIC, 2, { 0xb0, 0x00 }, 0 },
	/*   3 */ { PKT_GENERIC, 17, { 0xc1, 0x89, 0x28, 0x00, 0x14, 0x02, 0x00, 0x02, 0x0e, 0x01, 0xe8, 0x00, 0x07, 0x05, 0x0e, 0x05, 0x16 }, 0 },
	/*   4 */ { PKT_GENERIC, 3, { 0xc2, 0x10, 0xf0 }, 0 },
	/*   5 */ { PKT_GENERIC, 2, { 0xff, 0x20 }, 0 },
	/*   6 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/*   7 */ { PKT_DCS, 2, { 0x01, 0x66 }, 0 },
	/*   8 */ { PKT_DCS, 2, { 0x07, 0x3c }, 0 },
	/*   9 */ { PKT_DCS, 2, { 0x1b, 0x01 }, 0 },
	/*  10 */ { PKT_DCS, 2, { 0x5c, 0x90 }, 0 },
	/*  11 */ { PKT_DCS, 2, { 0x5e, 0xe6 }, 0 },
	/*  12 */ { PKT_DCS, 2, { 0x69, 0xd0 }, 0 },
	/*  13 */ { PKT_DCS, 2, { 0x95, 0xef }, 0 },
	/*  14 */ { PKT_DCS, 2, { 0x96, 0xef }, 0 },
	/*  15 */ { PKT_GENERIC, 2, { 0xf2, 0x64 }, 0 },
	/*  16 */ { PKT_GENERIC, 2, { 0xf4, 0x64 }, 0 },
	/*  17 */ { PKT_GENERIC, 2, { 0xf6, 0x64 }, 0 },
	/*  18 */ { PKT_GENERIC, 2, { 0xf8, 0x64 }, 0 },
	/*  19 */ { PKT_GENERIC, 2, { 0xff, 0x24 }, 0 },
	/*  20 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/*  21 */ { PKT_DCS, 2, { 0x04, 0x22 }, 0 },
	/*  22 */ { PKT_DCS, 2, { 0x05, 0x00 }, 0 },
	/*  23 */ { PKT_DCS, 2, { 0x06, 0xa3 }, 0 },
	/*  24 */ { PKT_DCS, 2, { 0x07, 0xa3 }, 0 },
	/*  25 */ { PKT_DCS, 2, { 0x08, 0x0f }, 0 },
	/*  26 */ { PKT_DCS, 2, { 0x09, 0x0f }, 0 },
	/*  27 */ { PKT_DCS, 2, { 0x0a, 0x17 }, 0 },
	/*  28 */ { PKT_DCS, 2, { 0x0b, 0x15 }, 0 },
	/*  29 */ { PKT_DCS, 2, { 0x0c, 0x13 }, 0 },
	/*  30 */ { PKT_DCS, 2, { 0x0d, 0x2d }, 0 },
	/*  31 */ { PKT_DCS, 2, { 0x0e, 0x2c }, 0 },
	/*  32 */ { PKT_DCS, 2, { 0x0f, 0x2f }, 0 },
	/*  33 */ { PKT_DCS, 2, { 0x10, 0x2e }, 0 },
	/*  34 */ { PKT_DCS, 2, { 0x11, 0x29 }, 0 },
	/*  35 */ { PKT_DCS, 2, { 0x12, 0x24 }, 0 },
	/*  36 */ { PKT_DCS, 2, { 0x13, 0x24 }, 0 },
	/*  37 */ { PKT_DCS, 2, { 0x14, 0x0b }, 0 },
	/*  38 */ { PKT_DCS, 2, { 0x15, 0x0c }, 0 },
	/*  39 */ { PKT_DCS, 2, { 0x16, 0x1c }, 0 },
	/*  40 */ { PKT_DCS, 2, { 0x17, 0x01 }, 0 },
	/*  41 */ { PKT_DCS, 2, { 0x1c, 0x22 }, 0 },
	/*  42 */ { PKT_DCS, 2, { 0x1d, 0x00 }, 0 },
	/*  43 */ { PKT_DCS, 2, { 0x1e, 0xa3 }, 0 },
	/*  44 */ { PKT_DCS, 2, { 0x1f, 0xa3 }, 0 },
	/*  45 */ { PKT_DCS, 2, { 0x20, 0x0f }, 0 },
	/*  46 */ { PKT_DCS, 2, { 0x21, 0x0f }, 0 },
	/*  47 */ { PKT_DCS, 2, { 0x22, 0x17 }, 0 },
	/*  48 */ { PKT_DCS, 2, { 0x23, 0x15 }, 0 },
	/*  49 */ { PKT_DCS, 2, { 0x24, 0x13 }, 0 },
	/*  50 */ { PKT_DCS, 2, { 0x25, 0x2d }, 0 },
	/*  51 */ { PKT_DCS, 2, { 0x26, 0x2c }, 0 },
	/*  52 */ { PKT_DCS, 2, { 0x27, 0x2f }, 0 },
	/*  53 */ { PKT_DCS, 2, { 0x28, 0x2e }, 0 },
	/*  54 */ { PKT_DCS, 2, { 0x29, 0x29 }, 0 },
	/*  55 */ { PKT_DCS, 2, { 0x2a, 0x24 }, 0 },
	/*  56 */ { PKT_DCS, 2, { 0x2b, 0x24 }, 0 },
	/*  57 */ { PKT_DCS, 2, { 0x2d, 0x0b }, 0 },
	/*  58 */ { PKT_DCS, 2, { 0x2f, 0x0c }, 0 },
	/*  59 */ { PKT_DCS, 2, { 0x30, 0x1c }, 0 },
	/*  60 */ { PKT_DCS, 2, { 0x31, 0x01 }, 0 },
	/*  61 */ { PKT_DCS, 2, { 0x32, 0x44 }, 0 },
	/*  62 */ { PKT_DCS, 2, { 0x33, 0x02 }, 0 },
	/*  63 */ { PKT_DCS, 2, { 0x34, 0x00 }, 0 },
	/*  64 */ { PKT_DCS, 2, { 0x35, 0x01 }, 0 },
	/*  65 */ { PKT_DCS, 2, { 0x36, 0x33 }, 0 },
	/*  66 */ { PKT_DCS, 2, { 0x37, 0x01 }, 0 },
	/*  67 */ { PKT_DCS, 2, { 0x38, 0x10 }, 0 },
	/*  68 */ { PKT_DCS, 2, { 0x3b, 0x04 }, 0 },
	/*  69 */ { PKT_DCS, 2, { 0x4e, 0x32 }, 0 },
	/*  70 */ { PKT_DCS, 2, { 0x4f, 0x32 }, 0 },
	/*  71 */ { PKT_DCS, 2, { 0x53, 0x32 }, 0 },
	/*  72 */ { PKT_DCS, 2, { 0x7a, 0x83 }, 0 },
	/*  73 */ { PKT_DCS, 2, { 0x7b, 0x8d }, 0 },
	/*  74 */ { PKT_DCS, 2, { 0x7d, 0x04 }, 0 },
	/*  75 */ { PKT_DCS, 2, { 0x80, 0x04 }, 0 },
	/*  76 */ { PKT_DCS, 2, { 0x81, 0x04 }, 0 },
	/*  77 */ { PKT_DCS, 2, { 0x82, 0x13 }, 0 },
	/*  78 */ { PKT_DCS, 2, { 0x84, 0x31 }, 0 },
	/*  79 */ { PKT_DCS, 2, { 0x85, 0x00 }, 0 },
	/*  80 */ { PKT_DCS, 2, { 0x86, 0x00 }, 0 },
	/*  81 */ { PKT_DCS, 2, { 0x87, 0x00 }, 0 },
	/*  82 */ { PKT_DCS, 2, { 0x90, 0x13 }, 0 },
	/*  83 */ { PKT_DCS, 2, { 0x92, 0x31 }, 0 },
	/*  84 */ { PKT_DCS, 2, { 0x93, 0x00 }, 0 },
	/*  85 */ { PKT_DCS, 2, { 0x94, 0x00 }, 0 },
	/*  86 */ { PKT_DCS, 2, { 0x95, 0x00 }, 0 },
	/*  87 */ { PKT_DCS, 2, { 0x9c, 0xf4 }, 0 },
	/*  88 */ { PKT_DCS, 2, { 0x9d, 0x01 }, 0 },
	/*  89 */ { PKT_DCS, 2, { 0xa0, 0x0d }, 0 },
	/*  90 */ { PKT_DCS, 2, { 0xa2, 0x0d }, 0 },
	/*  91 */ { PKT_DCS, 2, { 0xa3, 0x03 }, 0 },
	/*  92 */ { PKT_DCS, 2, { 0xa4, 0x04 }, 0 },
	/*  93 */ { PKT_DCS, 2, { 0xa5, 0x04 }, 0 },
	/*  94 */ { PKT_GENERIC, 2, { 0xc4, 0x80 }, 0 },
	/*  95 */ { PKT_GENERIC, 2, { 0xc6, 0xc0 }, 0 },
	/*  96 */ { PKT_GENERIC, 2, { 0xc9, 0x00 }, 0 },
	/*  97 */ { PKT_GENERIC, 2, { 0xd9, 0x80 }, 0 },
	/*  98 */ { PKT_GENERIC, 2, { 0xe9, 0x03 }, 0 },
	/*  99 */ { PKT_GENERIC, 2, { 0xff, 0x25 }, 0 },
	/* 100 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 101 */ { PKT_DCS, 2, { 0x0f, 0x1b }, 0 },
	/* 102 */ { PKT_DCS, 2, { 0x19, 0xe4 }, 0 },
	/* 103 */ { PKT_DCS, 2, { 0x21, 0x40 }, 0 },
	/* 104 */ { PKT_DCS, 2, { 0x58, 0x0c }, 0 },
	/* 105 */ { PKT_DCS, 2, { 0x59, 0x0a }, 0 },
	/* 106 */ { PKT_DCS, 2, { 0x5c, 0x05 }, 0 },
	/* 107 */ { PKT_DCS, 2, { 0x5f, 0x10 }, 0 },
	/* 108 */ { PKT_DCS, 2, { 0x66, 0xd8 }, 0 },
	/* 109 */ { PKT_DCS, 2, { 0x67, 0x01 }, 0 },
	/* 110 */ { PKT_DCS, 2, { 0x68, 0x58 }, 0 },
	/* 111 */ { PKT_DCS, 2, { 0x69, 0x10 }, 0 },
	/* 112 */ { PKT_DCS, 2, { 0x6b, 0x00 }, 0 },
	/* 113 */ { PKT_DCS, 2, { 0x6c, 0x1d }, 0 },
	/* 114 */ { PKT_DCS, 2, { 0x71, 0x1d }, 0 },
	/* 115 */ { PKT_DCS, 2, { 0x77, 0x62 }, 0 },
	/* 116 */ { PKT_DCS, 2, { 0x7e, 0x15 }, 0 },
	/* 117 */ { PKT_DCS, 2, { 0x7f, 0x00 }, 0 },
	/* 118 */ { PKT_DCS, 2, { 0x84, 0x6d }, 0 },
	/* 119 */ { PKT_DCS, 2, { 0x8d, 0x00 }, 0 },
	/* 120 */ { PKT_GENERIC, 2, { 0xc0, 0xd5 }, 0 },
	/* 121 */ { PKT_GENERIC, 2, { 0xc1, 0x11 }, 0 },
	/* 122 */ { PKT_GENERIC, 2, { 0xc3, 0x00 }, 0 },
	/* 123 */ { PKT_GENERIC, 2, { 0xc4, 0x11 }, 0 },
	/* 124 */ { PKT_GENERIC, 2, { 0xc5, 0x11 }, 0 },
	/* 125 */ { PKT_GENERIC, 2, { 0xc6, 0x11 }, 0 },
	/* 126 */ { PKT_GENERIC, 2, { 0xef, 0x00 }, 0 },
	/* 127 */ { PKT_GENERIC, 2, { 0xf0, 0x00 }, 0 },
	/* 128 */ { PKT_GENERIC, 2, { 0xf1, 0x04 }, 0 },
	/* 129 */ { PKT_GENERIC, 2, { 0xff, 0x26 }, 0 },
	/* 130 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 131 */ { PKT_DCS, 2, { 0x00, 0x00 }, 0 },
	/* 132 */ { PKT_DCS, 2, { 0x01, 0xf3 }, 0 },
	/* 133 */ { PKT_DCS, 2, { 0x02, 0xf3 }, 0 },
	/* 134 */ { PKT_DCS, 2, { 0x03, 0x00 }, 0 },
	/* 135 */ { PKT_DCS, 2, { 0x04, 0xf3 }, 0 },
	/* 136 */ { PKT_DCS, 2, { 0x05, 0x08 }, 0 },
	/* 137 */ { PKT_DCS, 2, { 0x06, 0x14 }, 0 },
	/* 138 */ { PKT_DCS, 2, { 0x07, 0x14 }, 0 },
	/* 139 */ { PKT_DCS, 2, { 0x08, 0x14 }, 0 },
	/* 140 */ { PKT_DCS, 2, { 0x14, 0x06 }, 0 },
	/* 141 */ { PKT_DCS, 2, { 0x15, 0x01 }, 0 },
	/* 142 */ { PKT_DCS, 2, { 0x74, 0xaf }, 0 },
	/* 143 */ { PKT_DCS, 2, { 0x81, 0x0d }, 0 },
	/* 144 */ { PKT_DCS, 2, { 0x83, 0x03 }, 0 },
	/* 145 */ { PKT_DCS, 2, { 0x84, 0x03 }, 0 },
	/* 146 */ { PKT_DCS, 2, { 0x85, 0x01 }, 0 },
	/* 147 */ { PKT_DCS, 2, { 0x86, 0x03 }, 0 },
	/* 148 */ { PKT_DCS, 2, { 0x87, 0x01 }, 0 },
	/* 149 */ { PKT_DCS, 2, { 0x88, 0x04 }, 0 },
	/* 150 */ { PKT_DCS, 2, { 0x8a, 0x1a }, 0 },
	/* 151 */ { PKT_DCS, 2, { 0x8b, 0x11 }, 0 },
	/* 152 */ { PKT_DCS, 2, { 0x8c, 0x24 }, 0 },
	/* 153 */ { PKT_DCS, 2, { 0x8e, 0x42 }, 0 },
	/* 154 */ { PKT_DCS, 2, { 0x8f, 0x11 }, 0 },
	/* 155 */ { PKT_DCS, 2, { 0x90, 0x11 }, 0 },
	/* 156 */ { PKT_DCS, 2, { 0x91, 0x11 }, 0 },
	/* 157 */ { PKT_DCS, 2, { 0x9a, 0x80 }, 0 },
	/* 158 */ { PKT_DCS, 2, { 0x9b, 0x08 }, 0 },
	/* 159 */ { PKT_DCS, 2, { 0x9c, 0x00 }, 0 },
	/* 160 */ { PKT_DCS, 2, { 0x9d, 0x00 }, 0 },
	/* 161 */ { PKT_DCS, 2, { 0x9e, 0x00 }, 0 },
	/* 162 */ { PKT_GENERIC, 2, { 0xff, 0x27 }, 0 },
	/* 163 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 164 */ { PKT_DCS, 2, { 0x01, 0x9c }, 0 },
	/* 165 */ { PKT_DCS, 2, { 0x20, 0x81 }, 0 },
	/* 166 */ { PKT_DCS, 2, { 0x21, 0x67 }, 0 },
	/* 167 */ { PKT_DCS, 2, { 0x25, 0x81 }, 0 },
	/* 168 */ { PKT_DCS, 2, { 0x26, 0x8e }, 0 },
	/* 169 */ { PKT_DCS, 2, { 0x6e, 0x9a }, 0 },
	/* 170 */ { PKT_DCS, 2, { 0x6f, 0x78 }, 0 },
	/* 171 */ { PKT_DCS, 2, { 0x70, 0x00 }, 0 },
	/* 172 */ { PKT_DCS, 2, { 0x71, 0x00 }, 0 },
	/* 173 */ { PKT_DCS, 2, { 0x72, 0x00 }, 0 },
	/* 174 */ { PKT_DCS, 2, { 0x73, 0x00 }, 0 },
	/* 175 */ { PKT_DCS, 2, { 0x74, 0x00 }, 0 },
	/* 176 */ { PKT_DCS, 2, { 0x75, 0x00 }, 0 },
	/* 177 */ { PKT_DCS, 2, { 0x76, 0x00 }, 0 },
	/* 178 */ { PKT_DCS, 2, { 0x77, 0x00 }, 0 },
	/* 179 */ { PKT_DCS, 2, { 0x7d, 0x09 }, 0 },
	/* 180 */ { PKT_DCS, 2, { 0x7e, 0xa5 }, 0 },
	/* 181 */ { PKT_DCS, 2, { 0x7f, 0x03 }, 0 },
	/* 182 */ { PKT_DCS, 2, { 0x80, 0x23 }, 0 },
	/* 183 */ { PKT_DCS, 2, { 0x82, 0x09 }, 0 },
	/* 184 */ { PKT_DCS, 2, { 0x83, 0xa5 }, 0 },
	/* 185 */ { PKT_DCS, 2, { 0x88, 0x02 }, 0 },
	/* 186 */ { PKT_GENERIC, 2, { 0xff, 0x2a }, 0 },
	/* 187 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 188 */ { PKT_DCS, 2, { 0x00, 0x91 }, 0 },
	/* 189 */ { PKT_DCS, 2, { 0x03, 0x20 }, 0 },
	/* 190 */ { PKT_DCS, 2, { 0x06, 0x0c }, 0 },
	/* 191 */ { PKT_DCS, 2, { 0x07, 0x64 }, 0 },
	/* 192 */ { PKT_DCS, 2, { 0x0a, 0x60 }, 0 },
	/* 193 */ { PKT_DCS, 2, { 0x0c, 0x07 }, 0 },
	/* 194 */ { PKT_DCS, 2, { 0x0d, 0x40 }, 0 },
	/* 195 */ { PKT_DCS, 2, { 0x0e, 0x02 }, 0 },
	/* 196 */ { PKT_DCS, 2, { 0x0f, 0x01 }, 0 },
	/* 197 */ { PKT_DCS, 2, { 0x11, 0x32 }, 0 },
	/* 198 */ { PKT_DCS, 2, { 0x15, 0x0f }, 0 },
	/* 199 */ { PKT_DCS, 2, { 0x16, 0x2e }, 0 },
	/* 200 */ { PKT_DCS, 2, { 0x19, 0x0f }, 0 },
	/* 201 */ { PKT_DCS, 2, { 0x1a, 0x02 }, 0 },
	/* 202 */ { PKT_DCS, 2, { 0x1b, 0x14 }, 0 },
	/* 203 */ { PKT_DCS, 2, { 0x1d, 0x36 }, 0 },
	/* 204 */ { PKT_DCS, 2, { 0x1e, 0x39 }, 0 },
	/* 205 */ { PKT_DCS, 2, { 0x1f, 0x49 }, 0 },
	/* 206 */ { PKT_DCS, 2, { 0x20, 0x39 }, 0 },
	/* 207 */ { PKT_DCS, 2, { 0x28, 0xfd }, 0 },
	/* 208 */ { PKT_DCS, 2, { 0x29, 0x0c }, 0 },
	/* 209 */ { PKT_DCS, 2, { 0x2a, 0x67 }, 0 },
	/* 210 */ { PKT_DCS, 2, { 0x2b, 0x04 }, 0 },
	/* 211 */ { PKT_DCS, 2, { 0x2d, 0x08 }, 0 },
	/* 212 */ { PKT_DCS, 2, { 0x2f, 0x01 }, 0 },
	/* 213 */ { PKT_DCS, 2, { 0x30, 0x47 }, 0 },
	/* 214 */ { PKT_DCS, 2, { 0x31, 0x20 }, 0 },
	/* 215 */ { PKT_DCS, 2, { 0x33, 0xb6 }, 0 },
	/* 216 */ { PKT_DCS, 2, { 0x34, 0xff }, 0 },
	/* 217 */ { PKT_DCS, 2, { 0x35, 0x43 }, 0 },
	/* 218 */ { PKT_DCS, 2, { 0x36, 0x85 }, 0 },
	/* 219 */ { PKT_DCS, 2, { 0x37, 0xf8 }, 0 },
	/* 220 */ { PKT_DCS, 2, { 0x38, 0x49 }, 0 },
	/* 221 */ { PKT_DCS, 2, { 0x39, 0x7f }, 0 },
	/* 222 */ { PKT_DCS, 2, { 0x3a, 0x47 }, 0 },
	/* 223 */ { PKT_GENERIC, 2, { 0xee, 0x01 }, 0 },
	/* 224 */ { PKT_GENERIC, 2, { 0xf0, 0x3b }, 0 },
	/* 225 */ { PKT_GENERIC, 2, { 0xf5, 0x01 }, 0 },
	/* 226 */ { PKT_GENERIC, 2, { 0xff, 0x2b }, 0 },
	/* 227 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 228 */ { PKT_GENERIC, 2, { 0xb7, 0x14 }, 0 },
	/* 229 */ { PKT_GENERIC, 2, { 0xb8, 0x10 }, 0 },
	/* 230 */ { PKT_GENERIC, 2, { 0xc0, 0x01 }, 0 },
	/* 231 */ { PKT_GENERIC, 2, { 0xff, 0xe0 }, 0 },
	/* 232 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 233 */ { PKT_DCS, 2, { 0x35, 0x82 }, 0 },
	/* 234 */ { PKT_GENERIC, 2, { 0xff, 0xf0 }, 0 },
	/* 235 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 236 */ { PKT_DCS, 2, { 0x1c, 0x01 }, 0 },
	/* 237 */ { PKT_DCS, 2, { 0x33, 0x01 }, 0 },
	/* 238 */ { PKT_DCS, 2, { 0x5a, 0x00 }, 0 },
	/* 239 */ { PKT_GENERIC, 2, { 0xd2, 0x52 }, 0 },
	/* 240 */ { PKT_GENERIC, 2, { 0xff, 0xd0 }, 0 },
	/* 241 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 242 */ { PKT_DCS, 2, { 0x53, 0x22 }, 0 },
	/* 243 */ { PKT_DCS, 2, { 0x54, 0x02 }, 0 },
	/* 244 */ { PKT_GENERIC, 2, { 0xff, 0xc0 }, 0 },
	/* 245 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 246 */ { PKT_DCS, 2, { 0x9c, 0x11 }, 0 },
	/* 247 */ { PKT_DCS, 2, { 0x9d, 0x11 }, 0 },
	/* 248 */ { PKT_GENERIC, 2, { 0xff, 0x2a }, 0 },
	/* 249 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 250 */ { PKT_DCS, 2, { 0x27, 0x80 }, 0 },
	/* 251 */ { PKT_GENERIC, 2, { 0xff, 0x25 }, 0 },
	/* 252 */ { PKT_GENERIC, 2, { 0xfb, 0x01 }, 0 },
	/* 253 */ { PKT_DCS, 2, { 0x79, 0x90 }, 0 },
	/* 254 */ { PKT_GENERIC, 2, { 0xff, 0x10 }, 0 },
	/* 255 */ { PKT_DCS, 1, { 0x11 }, 120 },
	/* 256: 0x29 (Display On) moved to lcm_enable() when video stream is active */
};


static int lcm_send_packets(struct lcm *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(authentic_init_cmds); i++) {
		const struct panel_init_cmd *cmd = &authentic_init_cmds[i];

		if (cmd->type == PKT_GENERIC)
			ret = mipi_dsi_generic_write(dsi, cmd->data, cmd->len);
		else
			ret = mipi_dsi_dcs_write_buffer(dsi, cmd->data, cmd->len);

		if (ret < 0) {
			pr_err("[LCM] Packet %d failed with error %d\n", i, ret);
			return ret;
		}

		if (cmd->delay_ms > 0)
			msleep(cmd->delay_ms);
	}
	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	static const u8 dcs_sleep_in  = 0x10;
	struct gpio_desc *bias_neg, *bias_pos;

	if (!ctx->prepared)
		return 0;

	/* 1. Send single-byte DCS Sleep In Command (0x10) */
	mipi_dsi_dcs_write_buffer(dsi, &dcs_sleep_in, 1);
	msleep(120);

	/* 2. Cut dual-rail bias power: VNEG (-5.5V) -> 2ms delay -> VPOS (+5.5V) */
	bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_LOW);
	if (!IS_ERR(bias_neg)) {
		gpiod_set_value(bias_neg, 0);
		devm_gpiod_put(ctx->dev, bias_neg);
	}
	usleep_range(2000, 2001);

	bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_LOW);
	if (!IS_ERR(bias_pos)) {
		gpiod_set_value(bias_pos, 0);
		devm_gpiod_put(ctx->dev, bias_pos);
	}

	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct gpio_desc *bias_pos, *bias_neg, *reset_gpio;
	int ret;

	if (ctx->prepared)
		return 0;

	/* 1. Sequence LCD Dual-Rail Bias Voltage: VPOS (+5.5V) -> 2ms -> VNEG (-5.5V) */
	bias_pos = devm_gpiod_get_index(ctx->dev, "bias", 0, GPIOD_OUT_LOW);
	if (!IS_ERR(bias_pos)) {
		gpiod_set_value(bias_pos, 1);
		devm_gpiod_put(ctx->dev, bias_pos);
	}
	msleep(10);

	bias_neg = devm_gpiod_get_index(ctx->dev, "bias", 1, GPIOD_OUT_LOW);
	if (!IS_ERR(bias_neg)) {
		gpiod_set_value(bias_neg, 1);
		devm_gpiod_put(ctx->dev, bias_neg);
	}
	msleep(10);

	/* 2. Double-Pulse Hardware Reset Sequence */
	reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	if (!IS_ERR(reset_gpio)) {
		usleep_range(10000, 15000);
		gpiod_set_value(reset_gpio, 0);
		udelay(11000);
		gpiod_set_value(reset_gpio, 1);
		udelay(5000);
		gpiod_set_value(reset_gpio, 0);
		udelay(5000);
		gpiod_set_value(reset_gpio, 1);
		udelay(13000);
		devm_gpiod_put(ctx->dev, reset_gpio);
	}

	/* 3. Wait for IC digital logic & charge pump stabilization */
	msleep(20);

	/* 4. Send 257 authentic configuration packets */
	ret = lcm_send_packets(ctx);
	if (ret < 0) {
		lcm_unprepare(panel);
		return ret;
	}

	ctx->prepared = true;
#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
	return 0;
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 dcs_display_off = 0x28;

	if (!ctx->enabled)
		return 0;

	/* Send Display Off while DSI video clock is still active */
	mipi_dsi_dcs_write_buffer(dsi, &dcs_display_off, 1);
	msleep(20);

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 dcs_display_on = 0x29;

	if (ctx->enabled)
		return 0;

	/* Send Display On when video stream is actively running (guarantees 1st wake success) */
	mipi_dsi_dcs_write_buffer(dsi, &dcs_display_on, 1);
	msleep(20);

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		if (ctx->backlight->props.brightness <= 0)
			ctx->backlight->props.brightness = 2048;
		backlight_update_status(ctx->backlight);
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

	ctx->enabled = true;
	return 0;
}

static const struct drm_display_mode lcm_modes[] = {
	/* 120Hz Mode (Preferred) */
	{
		.clock = 361519,
		.hdisplay = 1080,
		.hsync_start = 1080 + 49,
		.hsync_end = 1080 + 49 + 20,
		.htotal = 1080 + 49 + 20 + 20,
		.vdisplay = 2460,
		.vsync_start = 2460 + 54,
		.vsync_end = 2460 + 54 + 10,
		.vtotal = 2460 + 54 + 10 + 10,
		.vrefresh = 120,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	/* 90Hz Mode */
	{
		.clock = 361519,
		.hdisplay = 1080,
		.hsync_start = 1080 + 49,
		.hsync_end = 1080 + 49 + 20,
		.htotal = 1080 + 49 + 20 + 20,
		.vdisplay = 2460,
		.vsync_start = 2460 + 899,
		.vsync_end = 2460 + 899 + 10,
		.vtotal = 2460 + 899 + 10 + 10,
		.vrefresh = 90,
		.type = DRM_MODE_TYPE_DRIVER,
	},
	/* 60Hz Mode */
	{
		.clock = 361519,
		.hdisplay = 1080,
		.hsync_start = 1080 + 49,
		.hsync_end = 1080 + 49 + 20,
		.htotal = 1080 + 49 + 20 + 20,
		.vdisplay = 2460,
		.vsync_start = 2460 + 2588,
		.vsync_end = 2460 + 2588 + 10,
		.vtotal = 2460 + 2588 + 10 + 10,
		.vrefresh = 60,
		.type = DRM_MODE_TYPE_DRIVER,
	},
};

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;
	int i;

	for (i = 0; i < ARRAY_SIZE(lcm_modes); i++) {
		mode = drm_mode_duplicate(panel->drm, &lcm_modes[i]);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %d\n", i);
			return -ENOMEM;
		}
		drm_mode_set_name(mode);
		mode->type = lcm_modes[i].type;
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = 71;
	connector->display_info.height_mm = 162;
	return ARRAY_SIZE(lcm_modes);
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params_120hz = {
	.data_rate = 960,
	.vfp_low_power = 0,
	.dyn = {
		.switch_en = 1,
		.data_rate = 900,
		.vfp = 54,
		.vfp_lp_dyn = 0,
		.hfp = 49,
	},
	.dyn_fps = {
		.vact_timing_fps = 120,
	},
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.physical_width_um = 70956,
	.physical_height_um = 161622,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 488,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 1302,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.data_rate = 960,
	.vfp_low_power = 0,
	.dyn = {
		.switch_en = 1,
		.data_rate = 900,
		.vfp = 899,
		.vfp_lp_dyn = 0,
		.hfp = 49,
	},
	.dyn_fps = {
		.vact_timing_fps = 90,
	},
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.physical_width_um = 70956,
	.physical_height_um = 161622,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 488,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 1302,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static struct mtk_panel_params ext_params_60hz = {
	.data_rate = 960,
	.vfp_low_power = 0,
	.dyn = {
		.switch_en = 1,
		.data_rate = 900,
		.vfp = 2588,
		.vfp_lp_dyn = 0,
		.hfp = 49,
	},
	.dyn_fps = {
		.vact_timing_fps = 60,
	},
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A,
		.count = 1,
		.para_list[0] = 0x9C,
	},
	.physical_width_um = 70956,
	.physical_height_um = 161622,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2460,
		.pic_width = 1080,
		.slice_height = 20,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 512,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 488,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 1294,
		.slice_bpg_offset = 1302,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	},
};

static int lcm_set_backlight_cmdq(void *dsi_drv, dcs_write_gce cb,
				  void *handle, unsigned int level)
{
	u8 dcs[3];
	u32 val;

	if (level > 255)
		level = 255;

	val = (level * 4095) / 255;
	dcs[0] = 0x51;
	dcs[1] = (val >> 8) & 0x0f;
	dcs[2] = val & 0xff;

	if (cb)
		cb(dsi_drv, handle, dcs, sizeof(dcs));
	return 0;
}

static int lcm_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct gpio_desc *reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	if (!IS_ERR(reset_gpio)) {
		gpiod_set_value(reset_gpio, on);
		devm_gpiod_put(ctx->dev, reset_gpio);
	}
	return 0;
}

static int lcm_ata_check(struct drm_panel *panel)
{
	return 0;
}

static int lcm_ext_param_set(struct drm_panel *panel, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	if (!ext)
		return -EINVAL;

	if (mode == 0)
		ext->params = &ext_params_120hz;
	else if (mode == 1)
		ext->params = &ext_params_90hz;
	else if (mode == 2)
		ext->params = &ext_params_60hz;
	else
		return 1;

	return 0;
}

static struct mtk_panel_funcs lcm_ext_funcs = {
	.set_backlight_cmdq = lcm_set_backlight_cmdq,
	.reset = lcm_reset,
	.ata_check = lcm_ata_check,
	.ext_param_set = lcm_ext_param_set,
};
#endif

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;

	pr_info("[LCM] Infinix X698 NT36672C BOE 120Hz Panel probe start\n");

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE
			| MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			| MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n", PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	/* Continuous splash: LK bootloader already prepared and enabled panel */
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params_120hz, &lcm_ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("[LCM] Infinix X698 NT36672C BOE 120Hz Panel probe successful\n");
	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "nt36672c,fhdp,dsi,vdo,txd,boe,120hz,x697" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "nt36672c-fhdp-dsi-vdo-txd-boe-120hz-x697",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

static int __init lcm_init(void)
{
	return mipi_dsi_driver_register(&lcm_driver);
}

static void __exit lcm_exit(void)
{
	mipi_dsi_driver_unregister(&lcm_driver);
}

module_init(lcm_init);
module_exit(lcm_exit);

MODULE_AUTHOR("Transsion");
MODULE_DESCRIPTION("Transsion BOE TXD 120Hz NT36672C Panel Driver");
MODULE_LICENSE("GPL v2");
