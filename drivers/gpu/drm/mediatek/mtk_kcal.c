/*
 * Copyright (C) 2026 Sol Kernel Project
 * Hardware KCAL Engine for MediaTek MT6781 (DISP_CCORR0)
 *
 * Implements the universal KCAL sysfs interface under /sys/devices/platform/kcal_ctrl.0/
 * mapping RGB gains, Saturation, Contrast, Value, and Hue to DISP_CCORR0 3x3 color matrix
 * and 26-bit color offsets with zero GPU overhead.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "mtk_disp_ccorr.h"
#include "mtk_kcal.h"

static DEFINE_MUTEX(kcal_lock);

static struct platform_device *kcal_pdev;

static int s_enabled;
static unsigned int s_r = KCAL_NOMINAL_VAL;
static unsigned int s_g = KCAL_NOMINAL_VAL;
static unsigned int s_b = KCAL_NOMINAL_VAL;
static unsigned int s_sat = KCAL_NOMINAL_VAL;
static unsigned int s_cont = KCAL_NOMINAL_VAL;
static unsigned int s_val = KCAL_NOMINAL_VAL;
static unsigned int s_hue;
static unsigned int s_min;
static int s_invert;

static int s_kcal_matrix[3][3] = {
	{ 1024, 0, 0 },
	{ 0, 1024, 0 },
	{ 0, 0, 1024 }
};
static int s_kcal_offset[3];

static const int sin_lut_1024[64] = {
        0,   100,   200,   297,   392,   483,   569,   650,
      724,   792,   851,   903,   946,   980,  1004,  1019,
     1024,  1019,  1004,   980,   946,   903,   851,   792,
      724,   650,   569,   483,   392,   297,   200,   100,
        0,  -100,  -200,  -297,  -392,  -483,  -569,  -650,
     -724,  -792,  -851,  -903,  -946,  -980, -1004, -1019,
    -1024, -1019, -1004,  -980,  -946,  -903,  -851,  -792,
     -724,  -650,  -569,  -483,  -392,  -297,  -200,  -100,
};

static void mtk_kcal_recalculate_locked(void)
{
	int s = (int)((s_sat * 1024) / KCAL_NOMINAL_VAL);
	int c = (int)((s_cont * 1024) / KCAL_NOMINAL_VAL);
	int v = (int)((s_val * 1024) / KCAL_NOMINAL_VAL);
	int gr = (int)((s_r * 1024) / KCAL_NOMINAL_VAL);
	int gg = (int)((s_g * 1024) / KCAL_NOMINAL_VAL);
	int gb = (int)((s_b * 1024) / KCAL_NOMINAL_VAL);
	int scales[3];
	int msat[3][3];
	int mhue[3][3];
	int msat_hue[3][3];
	int off_base;
	int i, j, k;

	/* Rec.709 weights scaled to 1024: 0.2126 -> 218, 0.7152 -> 732, 0.0722 -> 74 */
	const int RW = 218;
	const int GW = 732;
	const int BW = 74;

	msat[0][0] = ((1024 - s) * RW) / 1024 + s;
	msat[0][1] = ((1024 - s) * GW) / 1024;
	msat[0][2] = ((1024 - s) * BW) / 1024;

	msat[1][0] = ((1024 - s) * RW) / 1024;
	msat[1][1] = ((1024 - s) * GW) / 1024 + s;
	msat[1][2] = ((1024 - s) * BW) / 1024;

	msat[2][0] = ((1024 - s) * RW) / 1024;
	msat[2][1] = ((1024 - s) * GW) / 1024;
	msat[2][2] = ((1024 - s) * BW) / 1024 + s;

	if (s_hue != 0) {
		int idx = (int)((s_hue * 64) / 1536);
		int sin_val = sin_lut_1024[idx & 63];
		int cos_val = sin_lut_1024[(idx + 16) & 63];
		const int U = 341;
		const int W = 591;

		mhue[0][0] = cos_val + ((1024 - cos_val) * U) / 1024;
		mhue[0][1] = ((1024 - cos_val) * U) / 1024 - (sin_val * W) / 1024;
		mhue[0][2] = ((1024 - cos_val) * U) / 1024 + (sin_val * W) / 1024;

		mhue[1][0] = ((1024 - cos_val) * U) / 1024 + (sin_val * W) / 1024;
		mhue[1][1] = cos_val + ((1024 - cos_val) * U) / 1024;
		mhue[1][2] = ((1024 - cos_val) * U) / 1024 - (sin_val * W) / 1024;

		mhue[2][0] = ((1024 - cos_val) * U) / 1024 - (sin_val * W) / 1024;
		mhue[2][1] = ((1024 - cos_val) * U) / 1024 + (sin_val * W) / 1024;
		mhue[2][2] = cos_val + ((1024 - cos_val) * U) / 1024;

		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++) {
				s64 sum = 0;
				for (k = 0; k < 3; k++)
					sum += (s64)msat[i][k] * mhue[k][j];
				msat_hue[i][j] = (int)(sum / 1024);
			}
		}
	} else {
		for (i = 0; i < 3; i++)
			for (j = 0; j < 3; j++)
				msat_hue[i][j] = msat[i][j];
	}

	scales[0] = (int)(((s64)gr * v * c) / (1024LL * 1024LL));
	scales[1] = (int)(((s64)gg * v * c) / (1024LL * 1024LL));
	scales[2] = (int)(((s64)gb * v * c) / (1024LL * 1024LL));

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			s_kcal_matrix[i][j] = (int)(((s64)scales[i] * msat_hue[i][j]) / 1024);
		}
	}

	off_base = (1024 - c) / 2;
	s_kcal_offset[0] = (int)((((s64)gr * v) / (1024LL * 1024LL) * off_base) << 15);
	s_kcal_offset[1] = (int)((((s64)gg * v) / (1024LL * 1024LL) * off_base) << 15);
	s_kcal_offset[2] = (int)((((s64)gb * v) / (1024LL * 1024LL) * off_base) << 15);

	if (s_invert) {
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 3; j++)
				s_kcal_matrix[i][j] = -s_kcal_matrix[i][j];
			s_kcal_offset[i] += (1024 << 15);
		}
	}
}

bool mtk_kcal_is_enabled(void)
{
	return (s_enabled != 0);
}
EXPORT_SYMBOL(mtk_kcal_is_enabled);

void mtk_kcal_get_matrix_and_offset(int matrix[3][3], int offset[3])
{
	int i, j;

	mutex_lock(&kcal_lock);
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++)
			matrix[i][j] = s_kcal_matrix[i][j];
		offset[i] = s_kcal_offset[i];
	}
	mutex_unlock(&kcal_lock);
}
EXPORT_SYMBOL(mtk_kcal_get_matrix_and_offset);

void mtk_kcal_notify_display_power(bool is_on)
{
	if (is_on && s_enabled)
		mtk_disp_ccorr_refresh_kcal();
}
EXPORT_SYMBOL(mtk_kcal_notify_display_power);

static ssize_t kcal_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", s_enabled);
}

static ssize_t kcal_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	if (kstrtoint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_enabled = val ? 1 : 0;
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u %u %u\n", s_r, s_g, s_b);
}

static ssize_t kcal_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int r, g, b;
	if (sscanf(buf, "%u %u %u", &r, &g, &b) != 3)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_r = clamp_val(r, s_min, 256U);
	s_g = clamp_val(g, s_min, 256U);
	s_b = clamp_val(b, s_min, 256U);
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_sat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", s_sat);
}

static ssize_t kcal_sat_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_sat = clamp_val(val, 128U, 384U);
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_cont_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", s_cont);
}

static ssize_t kcal_cont_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_cont = clamp_val(val, 128U, 384U);
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_val_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", s_val);
}

static ssize_t kcal_val_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_val = clamp_val(val, 128U, 384U);
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_hue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", s_hue);
}

static ssize_t kcal_hue_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_hue = clamp_val(val, 0U, 1536U);
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_min_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", s_min);
}

static ssize_t kcal_min_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_min = clamp_val(val, 0U, 256U);
	if (s_r < s_min) s_r = s_min;
	if (s_g < s_min) s_g = s_min;
	if (s_b < s_min) s_b = s_min;
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static ssize_t kcal_invert_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", s_invert);
}

static ssize_t kcal_invert_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	if (kstrtoint(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&kcal_lock);
	s_invert = val ? 1 : 0;
	mtk_kcal_recalculate_locked();
	mutex_unlock(&kcal_lock);

	if (s_enabled)
		mtk_disp_ccorr_refresh_kcal();
	return count;
}

static DEVICE_ATTR_RW(kcal_enable);
static DEVICE_ATTR_RW(kcal);
static DEVICE_ATTR_RW(kcal_sat);
static DEVICE_ATTR_RW(kcal_cont);
static DEVICE_ATTR_RW(kcal_val);
static DEVICE_ATTR_RW(kcal_hue);
static DEVICE_ATTR_RW(kcal_min);
static DEVICE_ATTR_RW(kcal_invert);

static struct attribute *kcal_attrs[] = {
	&dev_attr_kcal_enable.attr,
	&dev_attr_kcal.attr,
	&dev_attr_kcal_sat.attr,
	&dev_attr_kcal_cont.attr,
	&dev_attr_kcal_val.attr,
	&dev_attr_kcal_hue.attr,
	&dev_attr_kcal_min.attr,
	&dev_attr_kcal_invert.attr,
	NULL,
};

static const struct attribute_group kcal_attr_group = {
	.attrs = kcal_attrs,
};

int mtk_kcal_init(void)
{
	int ret;

	if (kcal_pdev)
		return 0;

	kcal_pdev = platform_device_register_simple("kcal_ctrl", 0, NULL, 0);
	if (IS_ERR(kcal_pdev)) {
		ret = PTR_ERR(kcal_pdev);
		pr_err("[KCAL] Failed to register platform device kcal_ctrl.0: %d\n", ret);
		kcal_pdev = NULL;
		return ret;
	}

	ret = sysfs_create_group(&kcal_pdev->dev.kobj, &kcal_attr_group);
	if (ret) {
		pr_err("[KCAL] Failed to create sysfs group: %d\n", ret);
		platform_device_unregister(kcal_pdev);
		kcal_pdev = NULL;
		return ret;
	}

	pr_info("[KCAL] Hardware KCAL Engine initialized successfully at /sys/devices/platform/kcal_ctrl.0/\n");
	return 0;
}
EXPORT_SYMBOL(mtk_kcal_init);

void mtk_kcal_exit(void)
{
	if (kcal_pdev) {
		sysfs_remove_group(&kcal_pdev->dev.kobj, &kcal_attr_group);
		platform_device_unregister(kcal_pdev);
		kcal_pdev = NULL;
		pr_info("[KCAL] Hardware KCAL Engine removed\n");
	}
}
EXPORT_SYMBOL(mtk_kcal_exit);
