// SPDX-License-Identifier: GPL-2.0
/*
 * Proprietary Charging Algorithm Travel Adapter Adapter for Richtek RFC TA
 * Reconstructed from stock X698 kernel binary (pca_rfc_ta 1.0.1_G)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mt-plat/charger_class.h>
#include <mt-plat/prop_chgalgo_class.h>
#include <mt-plat/prop_ta_class.h>

#define PCA_RFC_TA_VERSION	"1.0.1_G"

extern int dcp_get_ta_status(void);

struct pca_rfc_desc {
	bool support_cc;
	bool support_s65w;
	u32 vta_step;
	u32 ita_step;
	u32 ita_gap_per_vstep;
	bool pwr_lmt;
	bool support_meas_cap;
	bool support_status;
};

static const struct pca_rfc_desc pca_rfc_desc_default = {
	.support_cc = true,
	.support_s65w = false,
	.vta_step = 20,
	.ita_step = 30,
	.ita_gap_per_vstep = 20,
	.pwr_lmt = false,
	.support_meas_cap = true,
	.support_status = true,
};

struct pca_rfc_info {
	struct device *dev;
	struct pca_rfc_desc *desc;
	struct prop_chgalgo_device *pca;
	struct prop_ta_device *ta;
	struct charger_device *chg_dev;
	int hrst_cnt;
};

static int pca_rfc_enable_charging(struct prop_chgalgo_device *pca,
				   bool en, u32 mV, u32 mA)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_set_voltage_current(info->ta, mV, mA);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pca_rfc_set_cap(struct prop_chgalgo_device *pca, u32 mV, u32 mA)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_set_voltage_current(info->ta, mV, mA);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pca_rfc_get_measure_cap(struct prop_chgalgo_device *pca,
				   u32 *mV, u32 *mA)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_get_output_voltage_current(info->ta, mV, mA);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pca_rfc_get_temperature(struct prop_chgalgo_device *pca, int *degree)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_get_temp1(info->ta, degree);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pca_rfc_get_status(struct prop_chgalgo_device *pca,
			      struct prop_chgalgo_ta_status *status)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	u32 st = 0;
	int temp1 = 0, temp2 = 0;
	int ret;

	ret = prop_ta_device_get_status(info->ta, &st);
	if (ret < 0) {
		pr_err("%s get status fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = prop_ta_device_get_temp1(info->ta, &temp1);
	if (ret < 0) {
		pr_err("%s get temp1 fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = prop_ta_device_get_temp2(info->ta, &temp2);
	if (ret < 0) {
		pr_err("%s get temp2 fail(%d)\n", __func__, ret);
		return ret;
	}

	memset(status, 0, sizeof(*status));
	status->temp1 = temp1;
	status->temp2 = temp2;
	status->temp_level = (st >> 17) & 0x3;
	status->present_input = (st >> 3) & 0x1;
	status->ocp = (st >> 20) & 0x1;
	status->otp = !!(st & 0x60000);
	status->ovp = (st >> 16) & 0x1;
	status->cc_mode = st & 0x1;

	return 0;
}

static int pca_rfc_is_cc(struct prop_chgalgo_device *pca, bool *cc)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	u32 st = 0;
	int ret;

	ret = prop_ta_device_get_status(info->ta, &st);
	if (ret < 0) {
		pr_err("%s fail(%d)\n", __func__, ret);
		return ret;
	}

	*cc = st & 0x1;
	return 0;
}

static int pca_rfc_send_hardreset(struct prop_chgalgo_device *pca)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	pr_info("%s\n", __func__);
	info->hrst_cnt++;
	return prop_ta_device_set_voltage_current(info->ta, 5300, 3000);
}

static int pca_rfc_authenticate_ta(struct prop_chgalgo_device *pca,
				   struct prop_chgalgo_ta_auth_data *data)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	struct pca_rfc_desc *desc = info->desc;
	u32 vmin = 0, vmax = 0, imin = 0, imax = 0, pdp = 0;
	u8 mfinfo[9] = { 0 };
	int ret;

	if (!dcp_get_ta_status() || info->hrst_cnt >= 5) {
		pr_err("%s ta not ready or hardreset limit (%d)\n",
		       __func__, info->hrst_cnt);
		return -EINVAL;
	}

	ret = prop_ta_device_get_mfinfo(info->ta, mfinfo, 8);
	if (ret < 0) {
		pr_err("%s get mfinfo fail(%d)\n", __func__, ret);
		return ret;
	}

	if (strncmp(mfinfo, "ABCDIP33", 8) == 0) {
		ret = prop_ta_device_set_voltage_current(info->ta, 5500, 2200);
		if (ret < 0)
			pr_err("%s preset ABCDIP33 fail(%d)\n", __func__, ret);
	} else {
		ret = prop_ta_device_authentication(info->ta);
		if (ret < 0) {
			pr_err("%s ta auth fail(%d)\n", __func__, ret);
			return ret;
		}
	}

	ret = prop_ta_device_get_min_voltage(info->ta, &vmin);
	if (ret < 0)
		return ret;
	ret = prop_ta_device_get_max_voltage(info->ta, &vmax);
	if (ret < 0)
		return ret;
	ret = prop_ta_device_get_min_current(info->ta, &imin);
	if (ret < 0)
		return ret;
	ret = prop_ta_device_get_max_current(info->ta, &imax);
	if (ret < 0)
		return ret;
	ret = prop_ta_device_get_power_limit(info->ta, &pdp);
	if (ret < 0)
		return ret;

	if (desc->support_s65w && vmax < 12000) {
		pr_info("%s max volt %u < 12000 for 65W\n", __func__, vmax);
		return -ENETUNREACH;
	}

	if (vmin > data->vcap_min || vmax < data->vcap_max ||
	    imax < data->icap_min) {
		pr_err("%s cap mismatch: v(%u-%u vs %d-%d) i(%u vs %d)\n",
		       __func__, vmin, vmax, data->vcap_min, data->vcap_max,
		       imax, data->icap_min);
		return -EINVAL;
	}

	data->vta_min = vmin;
	data->vta_max = vmax;
	data->ita_max = imax;
	data->ita_min = imin;
	data->pdp = (u8)pdp;
	data->pwr_lmt = desc->pwr_lmt;
	data->support_cc = desc->support_cc;
	data->support_meas_cap = desc->support_meas_cap;
	data->support_status = desc->support_status;
	data->vta_step = desc->vta_step;
	data->ita_step = desc->ita_step;
	data->ita_gap_per_vstep = desc->ita_gap_per_vstep;
	info->hrst_cnt = 0;

	return 0;
}

static int pca_rfc_enable_wdt(struct prop_chgalgo_device *pca, bool en)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	return prop_ta_device_enable_wdt(info->ta, en);
}

static int pca_rfc_set_wdt(struct prop_chgalgo_device *pca, u32 ms)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);

	return prop_ta_device_set_wdt(info->ta, ms);
}

static int pca_rfc_get_ta_fw(struct prop_chgalgo_device *pca, u8 *buf, int len)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_get_fwcode(info->ta, buf, len);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static int pca_rfc_get_ta_mfinfo(struct prop_chgalgo_device *pca, u8 *buf,
				 int len)
{
	struct pca_rfc_info *info = prop_chgalgo_get_drvdata(pca);
	int ret;

	ret = prop_ta_device_get_mfinfo(info->ta, buf, len);
	if (ret < 0)
		pr_err("%s fail(%d)\n", __func__, ret);
	return ret;
}

static const struct prop_chgalgo_ta_ops pca_rfc_ta_ops = {
	.enable_charging = pca_rfc_enable_charging,
	.set_cap = pca_rfc_set_cap,
	.get_measure_cap = pca_rfc_get_measure_cap,
	.get_temperature = pca_rfc_get_temperature,
	.get_status = pca_rfc_get_status,
	.is_cc = pca_rfc_is_cc,
	.send_hardreset = pca_rfc_send_hardreset,
	.authenticate_ta = pca_rfc_authenticate_ta,
	.enable_wdt = pca_rfc_enable_wdt,
	.set_wdt = pca_rfc_set_wdt,
	.get_ta_fw = pca_rfc_get_ta_fw,
	.get_ta_mfinfo = pca_rfc_get_ta_mfinfo,
};

static SIMPLE_PCA_TA_DESC(pca_ta_rfc, pca_rfc_ta_ops);

static int pca_rfc_parse_dt(struct pca_rfc_info *info, struct device_node *np)
{
	struct pca_rfc_desc *desc;

	desc = devm_kzalloc(info->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	*desc = pca_rfc_desc_default;
	info->desc = desc;

	desc->support_cc = of_property_read_bool(np, "support_cc");
	dev_info(info->dev, "support_cc = %d\n", desc->support_cc);
	desc->support_s65w = of_property_read_bool(np, "support_s65w");
	desc->pwr_lmt = of_property_read_bool(np, "pwr_lmt");
	desc->support_meas_cap = of_property_read_bool(np, "support_meas_cap");
	desc->support_status = of_property_read_bool(np, "support_status");

	if (of_property_read_u32(np, "vta_step", &desc->vta_step) < 0)
		dev_err(info->dev, "Get vta_step failed\n");
	if (of_property_read_u32(np, "ita_step", &desc->ita_step) < 0)
		dev_err(info->dev, "Get ita_step failed\n");
	if (of_property_read_u32(np, "ita_gap_per_vstep",
				 &desc->ita_gap_per_vstep) < 0)
		dev_err(info->dev, "Get ita_gap_per_vstep failed\n");

	dev_err(info->dev, "%s:%d %d %d %d %d %d %d %d\n", __func__,
		desc->support_cc, desc->support_s65w, desc->vta_step,
		desc->ita_step, desc->ita_gap_per_vstep, desc->pwr_lmt,
		desc->support_meas_cap, desc->support_status);
	return 0;
}

static int pca_rfc_probe(struct platform_device *pdev)
{
	struct pca_rfc_info *info;
	int ret;

	dev_info(&pdev->dev, "%s(%s)\n", __func__, PCA_RFC_TA_VERSION);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	info->ta = prop_ta_device_get_by_name("rfc_ta");
	if (!info->ta) {
		dev_err(info->dev, "%s get rfc dev fail\n", __func__);
		return -EPROBE_DEFER;
	}

	ret = pca_rfc_parse_dt(info, pdev->dev.of_node);
	if (ret < 0) {
		dev_err(info->dev, "%s parse dt fail\n", __func__);
		return ret;
	}

	info->chg_dev = get_charger_by_name("primary_chg");
	if (!info->chg_dev) {
		dev_err(info->dev, "%s get chg dev fail\n", __func__);
		return -EPROBE_DEFER;
	}

	info->pca = prop_chgalgo_device_register(info->dev, &pca_ta_rfc_desc,
						 info);
	if (IS_ERR_OR_NULL(info->pca)) {
		dev_err(info->dev, "register pca_rfc_ta fail\n");
		return -EINVAL;
	}

	dev_info(info->dev, "%s successfully\n", __func__);
	return 0;
}

static int pca_rfc_remove(struct platform_device *pdev)
{
	struct pca_rfc_info *info = platform_get_drvdata(pdev);

	if (info && info->pca)
		prop_chgalgo_device_unregister(info->pca);
	return 0;
}

static const struct of_device_id pca_rfc_of_match[] = {
	{ .compatible = "richtek,pca_rfc_ta" },
	{},
};
MODULE_DEVICE_TABLE(of, pca_rfc_of_match);

static struct platform_driver pca_rfc_driver = {
	.driver = {
		.name = "pca_rfc_ta",
		.of_match_table = pca_rfc_of_match,
	},
	.probe = pca_rfc_probe,
	.remove = pca_rfc_remove,
};

module_platform_driver(pca_rfc_driver);

MODULE_DESCRIPTION("Prop ChgAlgo RFC TA Driver");
MODULE_VERSION(PCA_RFC_TA_VERSION);
MODULE_LICENSE("GPL v2");
