// SPDX-License-Identifier: GPL-2.0
/*
 * Proprietary RFC (Richtek Fast Charge) Travel Adapter I2C driver
 * Reconstructed from stock X698 kernel binary to match hardware protocol
 */

#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <mt-plat/prop_ta_class.h>

#define RFC_TA_CRC8_POLY	0x07
#define RFC_TA_MAX_BUF		256

#define RFC_REG_VERINFO		0xA0
#define RFC_REG_FWCODE		0xA1
#define RFC_REG_MFINFO		0xA2
#define RFC_REG_DATECODE	0xA3
#define RFC_REG_PDP		0xA4
#define RFC_REG_VMAX_IMAX	0xA5
#define RFC_REG_VMIN_IMIN	0xA6
#define RFC_REG_VOUT_IOUT	0xA7
#define RFC_REG_STATUS		0xA8
#define RFC_REG_TEMP		0xA9
#define RFC_REG_AUTH_RDATA	0xAA
#define RFC_REG_OUTPUT_CTL	0xB0
#define RFC_REG_MODE		0xB1
#define RFC_REG_VSET_ISET	0xB2
#define RFC_REG_WDT		0xB3
#define RFC_REG_AUTH_WDATA	0xB4

#define RFC_MODE_WDT_EN		BIT(1)

struct rfc_ta_info {
	struct device *dev;
	struct i2c_client *i2c;
	struct prop_ta_device ta;
	struct mutex io_lock;
	struct mutex suspend_lock;
	u8 crc8_tbl[CRC8_TABLE_SIZE];
};

static int rfc_ta_read_device(struct i2c_client *i2c, u8 cmd, int len,
			      void *data)
{
	struct rfc_ta_info *ri = i2c_get_clientdata(i2c);
	u8 buf[RFC_TA_MAX_BUF];
	u8 crc;
	int ret = 0;
	int retry = 0;

	if (!ri || len <= 0 || len + 1 > sizeof(buf))
		return -EINVAL;

	pm_stay_awake(ri->dev);
	mutex_lock(&ri->suspend_lock);

	for (retry = 0; retry < 2; retry++) {
		buf[0] = cmd;
		buf[1] = crc8(ri->crc8_tbl, buf, 1, 0);

		ret = i2c_master_send(i2c, buf, 2);
		if (ret < 0) {
			mdelay(20);
			continue;
		}

		ret = i2c_master_recv(i2c, buf, len + 1);
		if (ret < 0) {
			mdelay(20);
			continue;
		}

		crc = crc8(ri->crc8_tbl, buf, len, 0);
		if (buf[len] == crc) {
			memcpy(data, buf, len);
			ret = 0;
			break;
		}
		ret = -EINVAL;
		mdelay(20);
	}

	usleep_range(800, 800);
	mutex_unlock(&ri->suspend_lock);
	pm_relax(ri->dev);
	return ret < 0 ? ret : 0;
}

static int rfc_ta_write_device(struct i2c_client *i2c, u8 cmd, int len,
			       const void *data)
{
	struct rfc_ta_info *ri = i2c_get_clientdata(i2c);
	u8 buf[RFC_TA_MAX_BUF];
	int ret = 0;
	int retry = 0;

	if (!ri || len < 0 || len + 3 > sizeof(buf))
		return -EINVAL;

	pm_stay_awake(ri->dev);
	mutex_lock(&ri->suspend_lock);

	for (retry = 0; retry < 2; retry++) {
		buf[0] = cmd;
		buf[1] = (u8)len;
		if (len > 0)
			memcpy(&buf[2], data, len);
		buf[len + 2] = crc8(ri->crc8_tbl, buf, len + 2, 0);

		ret = i2c_master_send(i2c, buf, len + 3);
		if (ret >= 0) {
			ret = 0;
			break;
		}
		mdelay(20);
	}

	usleep_range(800, 800);
	mutex_unlock(&ri->suspend_lock);
	pm_relax(ri->dev);
	return ret < 0 ? ret : 0;
}

static int rfc_ta_i2c_write8_locked(struct rfc_ta_info *ri, u8 reg, u8 val)
{
	u8 rval = 0;
	int ret = 0;
	int retry;

	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, reg, 1, &val);
		if (ret < 0)
			goto err_delay;

		ret = rfc_ta_read_device(ri->i2c, reg, 1, &rval);
		if (ret < 0)
			goto err_delay;

		if (rval == val)
			return 0;

		dev_err(ri->dev, "%s check fail(0x%02X,0x%02X)\n",
			"rfc_ta_i2c_write8", val, rval);
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}
	return ret;
}

static int rfc_ta_i2c_write8(struct rfc_ta_info *ri, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_i2c_write8_locked(ri, reg, val);
	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_ta_i2c_update8(struct rfc_ta_info *ri, u8 reg, u8 mask, u8 val)
{
	u8 old_val = 0, new_val, rval = 0;
	int ret = 0;
	int retry;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, reg, 1, &old_val);
	if (ret < 0)
		goto out;

	new_val = (old_val & ~mask) | (val & mask);
	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, reg, 1, &new_val);
		if (ret < 0)
			goto err_delay;

		ret = rfc_ta_read_device(ri->i2c, reg, 1, &rval);
		if (ret < 0)
			goto err_delay;

		if (rval == new_val) {
			ret = 0;
			goto out;
		}

		dev_err(ri->dev, "%s check fail(0x%02X,0x%02X)\n",
			"rfc_ta_i2c_update8", new_val, rval);
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}
out:
	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_ta_i2c_set8(struct rfc_ta_info *ri, u8 reg, u8 mask)
{
	return rfc_ta_i2c_update8(ri, reg, mask, mask);
}

static int rfc_ta_i2c_clr8(struct rfc_ta_info *ri, u8 reg, u8 mask)
{
	return rfc_ta_i2c_update8(ri, reg, mask, 0);
}

static int rfc_ta_i2c_write16_locked(struct rfc_ta_info *ri, u8 reg, u16 val)
{
	__le16 wbuf = cpu_to_le16(val);
	__le16 rbuf = 0;
	u16 rval;
	int ret = 0;
	int retry;

	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, reg, 2, &wbuf);
		if (ret < 0)
			goto err_delay;

		ret = rfc_ta_read_device(ri->i2c, reg, 2, &rbuf);
		if (ret < 0)
			goto err_delay;

		rval = le16_to_cpu(rbuf);
		if (rval == val)
			return 0;

		dev_err(ri->dev, "%s check fail(0x%04X,0x%04X)\n",
			"rfc_ta_i2c_write16", val, rval);
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}
	return ret;
}

static int rfc_ta_i2c_update32(struct rfc_ta_info *ri, u8 reg, u32 mask, u32 val)
{
	__le32 rbuf = 0, wbuf = 0;
	u32 old_val, new_val, rval;
	int ret = 0;
	int retry;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, reg, 4, &rbuf);
	if (ret < 0)
		goto out;

	old_val = le32_to_cpu(rbuf);
	new_val = (old_val & ~mask) | (val & mask);
	wbuf = cpu_to_le32(new_val);

	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, reg, 4, &wbuf);
		if (ret < 0)
			goto err_delay;

		ret = rfc_ta_read_device(ri->i2c, reg, 4, &rbuf);
		if (ret < 0)
			goto err_delay;

		rval = le32_to_cpu(rbuf);
		if (rval == new_val) {
			ret = 0;
			goto out;
		}

		dev_err(ri->dev, "%s check fail(0x%08X,0x%08X)\n",
			"rfc_ta_i2c_update32", new_val, rval);
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}
out:
	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_prop_ta_get_verinfo(struct prop_ta_device *ta, u8 *buf, int len)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 rdata[4];
	int ret;

	if (len != 4)
		return -EINVAL;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VERINFO, 4, rdata);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	memcpy(buf, rdata, 4);
	return 0;
}

static int rfc_prop_ta_get_fwcode(struct prop_ta_device *ta, u8 *buf, int len)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 rdata[4];
	int ret;

	if (len != 4)
		return -EINVAL;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_FWCODE, 4, rdata);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	memcpy(buf, rdata, 4);
	return 0;
}

static int rfc_prop_ta_get_mfinfo(struct prop_ta_device *ta, u8 *buf, int len)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 rdata[8];
	int ret;

	if (len != 8)
		return -EINVAL;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_MFINFO, 8, rdata);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	memcpy(buf, rdata, 8);
	return 0;
}

static int rfc_prop_ta_get_datecode(struct prop_ta_device *ta, u8 *buf, int len)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 rdata[4];
	int ret;

	if (len != 4)
		return -EINVAL;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_DATECODE, 4, rdata);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	memcpy(buf, rdata, 4);
	return 0;
}

static int rfc_prop_ta_get_min_voltage(struct prop_ta_device *ta, u32 *mV)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VMIN_IMIN, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mV = le32_to_cpu(val) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mV);
	return 0;
}

static int rfc_prop_ta_get_max_voltage(struct prop_ta_device *ta, u32 *mV)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VMAX_IMAX, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mV = le32_to_cpu(val) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mV);
	return 0;
}

static int rfc_prop_ta_get_min_current(struct prop_ta_device *ta, u32 *mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VMIN_IMIN, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mA = (le32_to_cpu(val) >> 16) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mA);
	return 0;
}

static int rfc_prop_ta_get_max_current(struct prop_ta_device *ta, u32 *mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VMAX_IMAX, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mA = (le32_to_cpu(val) >> 16) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mA);
	return 0;
}

static int rfc_prop_ta_get_output_voltage(struct prop_ta_device *ta, u32 *mV)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VOUT_IOUT, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mV = le32_to_cpu(val) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mV);
	return 0;
}

static int rfc_prop_ta_get_output_current(struct prop_ta_device *ta, u32 *mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VOUT_IOUT, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*mA = (le32_to_cpu(val) >> 16) & 0xffff;
	dev_info(ri->dev, "%s %d\n", __func__, *mA);
	return 0;
}

static int rfc_prop_ta_get_status(struct prop_ta_device *ta, u32 *status)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_STATUS, 4, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*status = le32_to_cpu(val);
	dev_info(ri->dev, "%s 0x%08X\n", __func__, *status);
	return 0;
}

static int rfc_prop_ta_get_temp1(struct prop_ta_device *ta, int *temp)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 buf[2] = { 0 };
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_TEMP, 2, buf);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*temp = buf[0];
	dev_info(ri->dev, "%s %d\n", __func__, *temp);
	return 0;
}

static int rfc_prop_ta_get_temp2(struct prop_ta_device *ta, int *temp)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 buf[2] = { 0 };
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_TEMP, 2, buf);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*temp = buf[1];
	dev_info(ri->dev, "%s %d\n", __func__, *temp);
	return 0;
}

static int rfc_prop_ta_set_output_control(struct prop_ta_device *ta, u8 val)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s 0x%02X\n", __func__, val);
	return rfc_ta_i2c_write8(ri, RFC_REG_OUTPUT_CTL, val);
}

static int rfc_prop_ta_set_mode(struct prop_ta_device *ta, u8 val)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s 0x%02X\n", __func__, val);
	return rfc_ta_i2c_write8(ri, RFC_REG_MODE, val);
}

static int rfc_prop_ta_set_voltage(struct prop_ta_device *ta, u32 mV)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s %d\n", __func__, mV);
	return rfc_ta_i2c_update32(ri, RFC_REG_VSET_ISET, 0x0000ffff,
				   mV & 0xffff);
}

static int rfc_prop_ta_set_current(struct prop_ta_device *ta, u32 mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s %d\n", __func__, mA);
	return rfc_ta_i2c_update32(ri, RFC_REG_VSET_ISET, 0xffff0000,
				   (mA & 0xffff) << 16);
}

static int rfc_prop_ta_suspend(struct prop_ta_device *ta)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s\n", __func__);
	return 0;
}

static int rfc_prop_ta_resume(struct prop_ta_device *ta)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s\n", __func__);
	return 0;
}

static int rfc_prop_ta_set_voltage_current(struct prop_ta_device *ta,
					   u32 mV, u32 mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 wbuf, rbuf = 0;
	u32 val, rval;
	u8 mode = 0;
	int ret = 0;
	int retry;

	val = (mV & 0xffff) | ((mA & 0xffff) << 16);
	wbuf = cpu_to_le32(val);

	mutex_lock(&ri->io_lock);
	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, RFC_REG_VSET_ISET, 4, &wbuf);
		if (ret < 0)
			goto err_delay;

		ret = rfc_ta_read_device(ri->i2c, RFC_REG_VSET_ISET, 4, &rbuf);
		if (ret < 0)
			goto err_delay;

		rval = le32_to_cpu(rbuf);
		if (rval == val) {
			ret = 0;
			break;
		}
		dev_err(ri->dev, "%s check fail(0x%08X,0x%08X)\n",
			"rfc_ta_i2c_write32", val, rval);
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}

	if (ret == 0 &&
	    rfc_ta_read_device(ri->i2c, RFC_REG_MODE, 1, &mode) >= 0)
		dev_info(ri->dev, "%s mode = 0x%02X\n", __func__, mode);

	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_prop_ta_get_output_voltage_current(struct prop_ta_device *ta,
						  u32 *mV, u32 *mA)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le32 rbuf = 0;
	u32 val;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_VOUT_IOUT, 4, &rbuf);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	val = le32_to_cpu(rbuf);
	*mV = val & 0xffff;
	*mA = (val >> 16) & 0xffff;
	dev_info(ri->dev, "%s %d %d\n", __func__, *mV, *mA);
	return 0;
}

static const u32 rfc_sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static const u32 rfc_auth_secret[8] = {
	0x1aaaaaaa, 0x2bbbbbbb, 0x3ccccccc, 0x4ddddddd,
	0x5eeeeeee, 0x6fffffff, 0x7aaaaaaa, 0x8bbbbbbb,
};

static void rfc_sha256_transform_auth(const u8 salt[16], u32 h_out[4])
{
	u32 w[64];
	u32 h[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	u32 a, b, c, d, e, f, g, hh;
	int i;

	w[0] = 0x61626380;
	w[1] = 0;
	w[2] = 0;
	w[3] = 0;
	for (i = 0; i < 8; i++)
		w[4 + i] = rfc_auth_secret[i];
	for (i = 0; i < 4; i++)
		w[12 + i] = get_unaligned_le32(&salt[i * 4]);

	for (i = 16; i < 64; i++) {
		u32 s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^
			 (w[i - 15] >> 3);
		u32 s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^
			 (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = h[0];
	b = h[1];
	c = h[2];
	d = h[3];
	e = h[4];
	f = h[5];
	g = h[6];
	hh = h[7];

	for (i = 0; i < 64; i++) {
		u32 s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
		u32 ch = (e & f) ^ ((~e) & g);
		u32 temp1 = hh + s1 + ch + rfc_sha256_k[i] + w[i];
		u32 s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
		u32 maj = (a & b) ^ (a & c) ^ (b & c);
		u32 temp2 = s0 + maj;

		hh = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	h_out[0] = h[0] + a;
	h_out[1] = h[1] + b;
	h_out[2] = h[2] + c;
	h_out[3] = h[3] + d;
}

static int rfc_prop_ta_authentication(struct prop_ta_device *ta)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	u8 wdata[16] = { 0x80, 0x63, 0x62, 0x61 };
	u8 rcheck[16];
	u8 rdata[32];
	u32 hash[4];
	int ret = 0;
	int i, retry;

	mutex_lock(&ri->io_lock);

	for (retry = 0; retry < 2; retry++) {
		ret = rfc_ta_write_device(ri->i2c, RFC_REG_AUTH_WDATA, 16,
					  wdata);
		if (ret < 0)
			goto err_delay;

		usleep_range(5000, 6000);
		ret = rfc_ta_read_device(ri->i2c, RFC_REG_AUTH_WDATA, 16,
					 rcheck);
		if (ret < 0)
			goto err_delay;

		if (memcmp(wdata, rcheck, 16) == 0) {
			ret = 0;
			break;
		}
		ret = -EINVAL;
err_delay:
		mdelay(20);
	}
	if (ret < 0)
		goto out;

	usleep_range(5000, 6000);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_AUTH_RDATA, 32, rdata);
	if (ret < 0)
		goto out;

	rfc_sha256_transform_auth(rdata, hash);
	for (i = 0; i < 4; i++) {
		u32 got = get_unaligned_le32(&rdata[16 + i * 4]);

		if (got != hash[i]) {
			dev_err(ri->dev,
				"%s auth mismatch idx%d (got=0x%08X exp=0x%08X)\n",
				__func__, i, got, hash[i]);
			ret = -EINVAL;
			goto out;
		}
	}
	dev_info(ri->dev, "%s pass\n", __func__);
	ret = 0;

out:
	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_prop_ta_enable_wdt(struct prop_ta_device *ta, bool en)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);

	dev_info(ri->dev, "%s %d\n", __func__, en ? 1 : 0);
	if (en)
		return rfc_ta_i2c_set8(ri, RFC_REG_MODE, RFC_MODE_WDT_EN);
	return rfc_ta_i2c_clr8(ri, RFC_REG_MODE, RFC_MODE_WDT_EN);
}

static int rfc_prop_ta_set_wdt(struct prop_ta_device *ta, u32 ms)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	int ret;

	dev_info(ri->dev, "%s %d\n", __func__, ms);
	mutex_lock(&ri->io_lock);
	ret = rfc_ta_i2c_write16_locked(ri, RFC_REG_WDT, (u16)(ms & 0xffff));
	mutex_unlock(&ri->io_lock);
	return ret;
}

static int rfc_prop_ta_get_power_limit(struct prop_ta_device *ta, u32 *pdp)
{
	struct rfc_ta_info *ri = container_of(ta, struct rfc_ta_info, ta);
	__le16 val = 0;
	int ret;

	mutex_lock(&ri->io_lock);
	ret = rfc_ta_read_device(ri->i2c, RFC_REG_PDP, 2, &val);
	mutex_unlock(&ri->io_lock);
	if (ret < 0)
		return ret;

	*pdp = le16_to_cpu(val) / 10;
	dev_info(ri->dev, "%s %d\n", __func__, *pdp);
	return 0;
}

static const struct prop_ta_ops rfc_prop_ta_ops = {
	.get_verinfo = rfc_prop_ta_get_verinfo,
	.get_fwcode = rfc_prop_ta_get_fwcode,
	.get_mfinfo = rfc_prop_ta_get_mfinfo,
	.get_datecode = rfc_prop_ta_get_datecode,
	.get_min_voltage = rfc_prop_ta_get_min_voltage,
	.get_max_voltage = rfc_prop_ta_get_max_voltage,
	.get_min_current = rfc_prop_ta_get_min_current,
	.get_max_current = rfc_prop_ta_get_max_current,
	.get_output_voltage = rfc_prop_ta_get_output_voltage,
	.get_output_current = rfc_prop_ta_get_output_current,
	.get_status = rfc_prop_ta_get_status,
	.get_temp1 = rfc_prop_ta_get_temp1,
	.get_temp2 = rfc_prop_ta_get_temp2,
	.set_output_control = rfc_prop_ta_set_output_control,
	.set_mode = rfc_prop_ta_set_mode,
	.set_voltage = rfc_prop_ta_set_voltage,
	.set_current = rfc_prop_ta_set_current,
	.suspend = rfc_prop_ta_suspend,
	.resume = rfc_prop_ta_resume,
	.set_voltage_current = rfc_prop_ta_set_voltage_current,
	.get_output_voltage_current = rfc_prop_ta_get_output_voltage_current,
	.authentication = rfc_prop_ta_authentication,
	.enable_wdt = rfc_prop_ta_enable_wdt,
	.set_wdt = rfc_prop_ta_set_wdt,
	.get_power_limit = rfc_prop_ta_get_power_limit,
};

static int rfc_ta_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct rfc_ta_info *ri;
	int ret;

	dev_info(&i2c->dev, "%s\n", __func__);
	ri = devm_kzalloc(&i2c->dev, sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return -ENOMEM;

	ri->dev = &i2c->dev;
	ri->i2c = i2c;
	mutex_init(&ri->io_lock);
	mutex_init(&ri->suspend_lock);
	ri->ta.name = "rfc_ta";
	ri->ta.ops = &rfc_prop_ta_ops;
	i2c_set_clientdata(i2c, ri);

	crc8_populate_msb(ri->crc8_tbl, RFC_TA_CRC8_POLY);

	ret = devm_prop_ta_classdev_register(ri->dev, &ri->ta);
	if (ret < 0) {
		dev_err(ri->dev, "%s add prop ta classdev fail(%d)\n",
			__func__, ret);
		return ret;
	}

	dev_info(ri->dev, "%s successfully\n", __func__);
	return 0;
}

static int rfc_ta_i2c_remove(struct i2c_client *i2c)
{
	struct rfc_ta_info *ri = i2c_get_clientdata(i2c);

	if (ri) {
		mutex_destroy(&ri->suspend_lock);
		mutex_destroy(&ri->io_lock);
	}
	return 0;
}

static int __maybe_unused rfc_ta_i2c_suspend(struct device *dev)
{
	struct rfc_ta_info *ri = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	if (ri)
		mutex_lock(&ri->suspend_lock);
	return 0;
}

static int __maybe_unused rfc_ta_i2c_resume(struct device *dev)
{
	struct rfc_ta_info *ri = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	if (ri)
		mutex_unlock(&ri->suspend_lock);
	return 0;
}

static SIMPLE_DEV_PM_OPS(rfc_ta_pm_ops, rfc_ta_i2c_suspend, rfc_ta_i2c_resume);

static const struct of_device_id rfc_ta_of_match[] = {
	{ .compatible = "richtek,rfc_ta" },
	{},
};
MODULE_DEVICE_TABLE(of, rfc_ta_of_match);

static const struct i2c_device_id rfc_ta_i2c_id[] = {
	{ "rfc_ta", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, rfc_ta_i2c_id);

static struct i2c_driver rfc_ta_i2c_driver = {
	.driver = {
		.name = "rfc_ta",
		.of_match_table = rfc_ta_of_match,
		.pm = &rfc_ta_pm_ops,
	},
	.probe = rfc_ta_i2c_probe,
	.remove = rfc_ta_i2c_remove,
	.id_table = rfc_ta_i2c_id,
};

module_i2c_driver(rfc_ta_i2c_driver);

MODULE_DESCRIPTION("Richtek RFC Travel Adapter Driver");
MODULE_LICENSE("GPL v2");
