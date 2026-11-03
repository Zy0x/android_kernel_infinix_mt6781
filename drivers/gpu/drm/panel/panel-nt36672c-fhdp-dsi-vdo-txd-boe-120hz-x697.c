/*
 * Copyright (c) 2015-2023 MediaTek Inc. & Transsion Holdings.
 *
 * Driver for Novatek NT36672C 120Hz FHD+ DSI Video Mode Panel (BOE Glass / TXD)
 * Specifically configured for Infinix Note 11 Pro / Note 11S (X697 / X698 / sword).
 */

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;
	int error;
};

#define lcm_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio = devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(ctx->dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(10 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	/* Extracted Authentic Novatek NT36672C BOE Register Sequence */
	{
		static const u8 init_table[] = {
			0x28, 0x00, 0x10, 0x00, 0xff, 0x10, 0xfb, 0x01, 0xb0, 0x00, 0xc1, 0x89, 0x28, 0x00, 0x14, 0x02,
			0x00, 0x02, 0x0e, 0x01, 0xe8, 0x00, 0x07, 0x05, 0x0e, 0x05, 0x16, 0xc2, 0x10, 0xf0, 0xff, 0x20,
			0xfb, 0x01, 0x01, 0x66, 0x07, 0x3c, 0x1b, 0x01, 0x5c, 0x90, 0x5e, 0xe6, 0x69, 0xd0, 0x95, 0xef,
			0x96, 0xef, 0xf2, 0x64, 0xf4, 0x64, 0xf6, 0x64, 0xf8, 0x64, 0xff, 0x24, 0xfb, 0x01, 0x04, 0x22,
			0x05, 0x00, 0x06, 0xa3, 0x07, 0xa3, 0x08, 0x0f, 0x09, 0x0f, 0x0a, 0x17, 0x0b, 0x15, 0x0c, 0x13,
			0x0d, 0x2d, 0x0e, 0x2c, 0x0f, 0x2f, 0x10, 0x2e, 0x11, 0x29, 0x12, 0x24, 0x13, 0x24, 0x14, 0x0b,
			0x15, 0x0c, 0x16, 0x1c, 0x17, 0x01, 0x1c, 0x22, 0x1d, 0x00, 0x1e, 0xa3, 0x1f, 0xa3, 0x20, 0x0f,
			0x21, 0x0f, 0x22, 0x17, 0x23, 0x15, 0x24, 0x13, 0x25, 0x2d, 0x26, 0x2c, 0x27, 0x2f, 0x28, 0x2e,
			0x29, 0x29, 0x2a, 0x24, 0x2b, 0x24, 0x2d, 0x0b, 0x2f, 0x0c, 0x30, 0x1c, 0x31, 0x01, 0x32, 0x44,
			0x33, 0x02, 0x34, 0x00, 0x35, 0x01, 0x36, 0x33, 0x37, 0x01, 0x38, 0x10, 0x3b, 0x04, 0x4e, 0x32,
			0x4f, 0x32, 0x53, 0x32, 0x7a, 0x83, 0x7b, 0x8d, 0x7d, 0x04, 0x80, 0x04, 0x81, 0x04, 0x82, 0x13,
			0x84, 0x31, 0x85, 0x00, 0x86, 0x00, 0x87, 0x00, 0x90, 0x13, 0x92, 0x31, 0x93, 0x00, 0x94, 0x00,
			0x95, 0x00, 0x9c, 0xf4, 0x9d, 0x01, 0xa0, 0x0d, 0xa2, 0x0d, 0xa3, 0x03, 0xa4, 0x04, 0xa5, 0x04,
			0xc4, 0x80, 0xc6, 0xc0, 0xc9, 0x00, 0xd9, 0x80, 0xe9, 0x03, 0xff, 0x25, 0xfb, 0x01, 0x0f, 0x1b,
			0x19, 0xe4, 0x21, 0x40, 0x58, 0x0c, 0x59, 0x0a, 0x5c, 0x05, 0x5f, 0x10, 0x66, 0xd8, 0x67, 0x01,
			0x68, 0x58, 0x69, 0x10, 0x6b, 0x00, 0x6c, 0x1d, 0x71, 0x1d, 0x77, 0x62, 0x7e, 0x15, 0x7f, 0x00,
			0x84, 0x6d, 0x8d, 0x00, 0xc0, 0xd5, 0xc1, 0x11, 0xc3, 0x00, 0xc4, 0x11, 0xc5, 0x11, 0xc6, 0x11,
			0xef, 0x00, 0xf0, 0x00, 0xf1, 0x04, 0xff, 0x26, 0xfb, 0x01, 0x00, 0x00, 0x01, 0xf3, 0x02, 0xf3,
			0x03, 0x00, 0x04, 0xf3, 0x05, 0x08, 0x06, 0x14, 0x07, 0x14, 0x08, 0x14, 0x14, 0x06, 0x15, 0x01,
			0x74, 0xaf, 0x81, 0x0d, 0x83, 0x03, 0x84, 0x03, 0x85, 0x01, 0x86, 0x03, 0x87, 0x01, 0x88, 0x04,
			0x8a, 0x1a, 0x8b, 0x11, 0x8c, 0x24, 0x8e, 0x42, 0x8f, 0x11, 0x90, 0x11, 0x91, 0x11, 0x9a, 0x80,
			0x9b, 0x08, 0x9c, 0x00, 0x9d, 0x00, 0x9e, 0x00, 0xff, 0x27, 0xfb, 0x01, 0x01, 0x9c, 0x20, 0x81,
			0x21, 0x67, 0x25, 0x81, 0x26, 0x8e, 0x6e, 0x9a, 0x6f, 0x78, 0x70, 0x00, 0x71, 0x00, 0x72, 0x00,
			0x73, 0x00, 0x74, 0x00, 0x75, 0x00, 0x76, 0x00, 0x77, 0x00, 0x7d, 0x09, 0x7e, 0xa5, 0x7f, 0x03,
			0x80, 0x23, 0x82, 0x09, 0x83, 0xa5, 0x88, 0x02, 0xff, 0x2a, 0xfb, 0x01, 0x00, 0x91, 0x03, 0x20,
			0x06, 0x0c, 0x07, 0x64, 0x0a, 0x60, 0x0c, 0x07, 0x0d, 0x40, 0x0e, 0x02, 0x0f, 0x01, 0x11, 0x32,
			0x15, 0x0f, 0x16, 0x2e, 0x19, 0x0f, 0x1a, 0x02, 0x1b, 0x14, 0x1d, 0x36, 0x1e, 0x39, 0x1f, 0x49,
			0x20, 0x39, 0x28, 0xfd, 0x29, 0x0c, 0x2a, 0x67, 0x2b, 0x04, 0x2d, 0x08, 0x2f, 0x01, 0x30, 0x47,
			0x31, 0x20, 0x33, 0xb6, 0x34, 0xff, 0x35, 0x43, 0x36, 0x85, 0x37, 0xf8, 0x38, 0x49, 0x39, 0x7f,
			0x3a, 0x47, 0xee, 0x01, 0xf0, 0x3b, 0xf5, 0x01, 0xff, 0x2b, 0xfb, 0x01, 0xb7, 0x14, 0xb8, 0x10,
			0xc0, 0x01, 0xff, 0xe0, 0xfb, 0x01, 0x35, 0x82, 0xff, 0xf0, 0xfb, 0x01, 0x1c, 0x01, 0x33, 0x01,
			0x5a, 0x00, 0xd2, 0x52, 0xff, 0xd0, 0xfb, 0x01, 0x53, 0x22, 0x54, 0x02, 0xff, 0xc0, 0xfb, 0x01,
			0x9c, 0x11, 0x9d, 0x11, 0xff, 0x2a, 0xfb, 0x01, 0x27, 0x80, 0xff, 0x25, 0xfb, 0x01, 0x79, 0x90,
			0xff, 0x10, 0x11, 0x00, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};
		/* Parse DCS/Generic packets from init_table */
		size_t idx = 0;
		while (idx < sizeof(init_table)) {
			if (idx + 1 < sizeof(init_table) && init_table[idx] == 0xFF) {
				lcm_dcs_write_seq_static(ctx, init_table[idx], init_table[idx+1]);
				idx += 2;
			} else if (idx + 1 < sizeof(init_table) && init_table[idx] == 0xFB) {
				lcm_dcs_write_seq_static(ctx, init_table[idx], init_table[idx+1]);
				idx += 2;
			} else {
				/* Fallback sequential write */
				lcm_dcs_write(ctx, &init_table[idx], 2);
				idx += 2;
			}
		}
	}
	lcm_dcs_write_seq_static(ctx, 0x11); /* Sleep Out */
	msleep(120);
	lcm_dcs_write_seq_static(ctx, 0x29); /* Display On */
	msleep(20);
}

#define HAC (1080)
#define VAC (2460)

/* 60 Hz Standard Mode */
static struct drm_display_mode mode_60hz = {
	.clock = 361519,
	.hdisplay = HAC,
	.hsync_start = HAC + 49,
	.hsync_end = HAC + 49 + 20,
	.htotal = HAC + 49 + 20 + 20,
	.vdisplay = VAC,
	.vsync_start = VAC + 2588,
	.vsync_end = VAC + 2588 + 10,
	.vtotal = VAC + 2588 + 10 + 10,
	.vrefresh = 60,
};

/* 90 Hz Esports Mode */
static struct drm_display_mode mode_90hz = {
	.clock = 361519,
	.hdisplay = HAC,
	.hsync_start = HAC + 49,
	.hsync_end = HAC + 49 + 20,
	.htotal = HAC + 49 + 20 + 20,
	.vdisplay = VAC,
	.vsync_start = VAC + 899,
	.vsync_end = VAC + 899 + 10,
	.vtotal = VAC + 899 + 10 + 10,
	.vrefresh = 90,
};

/* 120 Hz Native Ultra High Refresh Rate Mode */
static struct drm_display_mode mode_120hz = {
	.clock = 361519,
	.hdisplay = HAC,
	.hsync_start = HAC + 49,
	.hsync_end = HAC + 49 + 20,
	.htotal = HAC + 49 + 20 + 20,
	.vdisplay = VAC,
	.vsync_start = VAC + 54,
	.vsync_end = VAC + 54 + 10,
	.vtotal = VAC + 54 + 10 + 10,
	.vrefresh = 120,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 588,
	.vfp_low_power = 4178,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
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
		.increment_interval = 295,
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
	.data_rate = 1176,
};
#endif

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28); /* Display Off */
	msleep(20);
	lcm_dcs_write_seq_static(ctx, 0x10); /* Sleep In */
	msleep(120);

	ctx->enabled = false;
	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	ctx->error = 0;
	ctx->prepared = false;
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->prepared)
		return 0;

	lcm_panel_init(ctx);
	ctx->prepared = true;
	return 0;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	ctx->enabled = true;
	return 0;
}

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *m120 = drm_mode_duplicate(panel->drm, &mode_120hz);
	struct drm_display_mode *m90 = drm_mode_duplicate(panel->drm, &mode_90hz);
	struct drm_display_mode *m60 = drm_mode_duplicate(panel->drm, &mode_60hz);

	if (!m120 || !m90 || !m60) {
		dev_err(panel->dev, "failed to add modes\n");
		return -ENOMEM;
	}

	drm_mode_set_name(m120, "1080x2460@120Hz");
	m120->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, m120);

	drm_mode_set_name(m90, "1080x2460@90Hz");
	m90->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, m90);

	drm_mode_set_name(m60, "1080x2460@60Hz");
	m60->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, m60);

	panel->connector->display_info.width_mm = 69;
	panel->connector->display_info.height_mm = 157;

	return 3;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET;

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
	ret = mtk_panel_ext_create(dev, &ext_params, NULL, &ctx->panel);
	if (ret < 0)
		return ret;
#endif

	pr_info("Infinix X698 NT36672C BOE 120Hz Panel probe successful\n");
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
	{ .compatible = "nt36672c,fhdp,dsi,vdo,txd,boe,120hz,x697", },
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

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Infinix & MediaTek Custom Kernel Port");
MODULE_DESCRIPTION("Novatek NT36672C BOE 120Hz FHD+ DSI Panel Driver for Infinix Note 11 Pro X698");
MODULE_LICENSE("GPL v2");
