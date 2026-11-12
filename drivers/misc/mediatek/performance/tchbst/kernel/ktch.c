/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "[ktch]"fmt

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>

#include <mt-plat/eas_ctrl.h>
#include <mt-plat/mtk_gpu_utility.h>

#include "tchbst.h"
#include "boost_ctrl.h"
#include "mtk_perfmgr_internal.h"

#define MAX_CORE (8)
#define MAX_FREQ (20000000)
#define KTCH_MAX_CLSTRS (4)
#define TARGET_CORE (-1)
#define TARGET_FREQ (1500000)
#define TARGET_BIG_FREQ (1796000)
#define DEFAULT_TAIL_MS (280)
#define DEFAULT_EAS_BOOST (65)
#define DEFAULT_GPU_OPP_IDX (24)

struct boost {
	spinlock_t touch_lock;
	struct mutex apply_lock;
	wait_queue_head_t wq;
	struct task_struct *thread;
	struct hrtimer tail_timer;
	bool desired_boost;
	bool applied_boost;
	int touch_event;
	atomic_t event;
};

static struct boost ktchboost;

static int ktch_mgr_enable = 1;
static int ktch_mgr_core = 1;
static int ktch_mgr_freq = TARGET_FREQ;
static int ktch_mgr_big_freq = TARGET_BIG_FREQ;
static int ktch_mgr_clstr = 1;
static int ktch_mgr_duration_ms = DEFAULT_TAIL_MS;
static int ktch_mgr_eas_boost = DEFAULT_EAS_BOOST;
static int ktch_mgr_gpu_opp = DEFAULT_GPU_OPP_IDX;

int ktch_get_target_core(void)
{
	return TARGET_CORE;
}

int ktch_get_target_freq(void)
{
	return TARGET_FREQ;
}

static void apply_touch_boost(bool enable)
{
	struct ppm_limit_data freq_to_set[KTCH_MAX_CLSTRS];
	int nr_clstrs = perfmgr_clusters;
	int i;

	if (nr_clstrs > KTCH_MAX_CLSTRS)
		nr_clstrs = KTCH_MAX_CLSTRS;
	if (nr_clstrs <= 0)
		return;

	for (i = 0; i < nr_clstrs; i++) {
		freq_to_set[i].min = -1;
		freq_to_set[i].max = -1;
	}

	if (enable) {
		freq_to_set[0].min = ktch_mgr_freq;
		if (nr_clstrs >= 2)
			freq_to_set[1].min = ktch_mgr_big_freq;
	}

	update_eas_uclamp_min(EAS_UCLAMP_KIR_TOUCH, CGROUP_TA,
		enable ? ktch_mgr_eas_boost : 0);
	update_userlimit_cpu_freq(CPU_KIR_PERFTOUCH, nr_clstrs, freq_to_set);
	mtk_set_touch_boost_gpu_freq(enable ? ktch_mgr_gpu_opp : -1);
}

void set_freq(int enable, int core, int freq)
{
	(void)core;
	(void)freq;
	mutex_lock(&ktchboost.apply_lock);
	apply_touch_boost(enable != 0);
	ktchboost.applied_boost = (enable != 0);
	mutex_unlock(&ktchboost.apply_lock);
}

static enum hrtimer_restart ktch_tail_timer_cb(struct hrtimer *timer)
{
	unsigned long flags;
	bool need_wake = false;

	spin_lock_irqsave(&ktchboost.touch_lock, flags);
	if (ktchboost.desired_boost) {
		ktchboost.desired_boost = false;
		ktchboost.touch_event = 0;
		need_wake = true;
	}
	spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

	if (need_wake) {
		atomic_inc(&ktchboost.event);
		wake_up(&ktchboost.wq);
	}
	return HRTIMER_NORESTART;
}

static int ktchboost_thread(void *ptr)
{
	bool target;
	unsigned long flags;

	set_user_nice(current, -15);

	while (!kthread_should_stop()) {
		wait_event(ktchboost.wq,
			atomic_read(&ktchboost.event) || kthread_should_stop());
		if (kthread_should_stop())
			break;

		atomic_set(&ktchboost.event, 0);

		spin_lock_irqsave(&ktchboost.touch_lock, flags);
		target = ktchboost.desired_boost;
		spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

		mutex_lock(&ktchboost.apply_lock);
		if (ktchboost.applied_boost != target) {
			apply_touch_boost(target);
			ktchboost.applied_boost = target;
		}
		mutex_unlock(&ktchboost.apply_lock);
	}
	return 0;
}

static ssize_t perfmgr_tb_enable_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[64];
	unsigned long val;
	int ret;
	unsigned long flags;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, &val);
	if (ret < 0 || val > 1)
		return -EINVAL;

	spin_lock_irqsave(&ktchboost.touch_lock, flags);
	ktch_mgr_enable = val;
	if (!ktch_mgr_enable) {
		hrtimer_try_to_cancel(&ktchboost.tail_timer);
		ktchboost.desired_boost = false;
		atomic_inc(&ktchboost.event);
		wake_up(&ktchboost.wq);
	}
	spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

	return cnt;
}

static int perfmgr_tb_enable_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_enable);
	return 0;
}

static int perfmgr_tb_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_enable_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_enable_fops = {
	.open = perfmgr_tb_enable_open,
	.write = perfmgr_tb_enable_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t perfmgr_tb_core_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[64];
	unsigned long val;
	int ret;
	unsigned long flags;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, &val);
	if (ret < 0 || val > MAX_CORE)
		return -EINVAL;

	spin_lock_irqsave(&ktchboost.touch_lock, flags);
	ktch_mgr_core = val;
	spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

	return cnt;
}

static int perfmgr_tb_core_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_core);
	return 0;
}

static int perfmgr_tb_core_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_core_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_core_fops = {
	.open = perfmgr_tb_core_open,
	.write = perfmgr_tb_core_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t perfmgr_tb_freq_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[64];
	unsigned long val;
	int ret;
	unsigned long flags;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, &val);
	if (ret < 0 || val > MAX_FREQ)
		return -EINVAL;

	spin_lock_irqsave(&ktchboost.touch_lock, flags);
	ktch_mgr_freq = val;
	spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

	return cnt;
}

static int perfmgr_tb_freq_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_freq);
	return 0;
}

static int perfmgr_tb_freq_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_freq_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_freq_fops = {
	.open = perfmgr_tb_freq_open,
	.write = perfmgr_tb_freq_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t perfmgr_tb_duration_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[32];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, &val);
	if (ret < 0 || val > 2000)
		return -EINVAL;

	ktch_mgr_duration_ms = (int)val;
	return cnt;
}

static int perfmgr_tb_duration_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_duration_ms);
	return 0;
}

static int perfmgr_tb_duration_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_duration_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_duration_fops = {
	.open = perfmgr_tb_duration_open,
	.write = perfmgr_tb_duration_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t perfmgr_tb_eas_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[32];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtoul(buf, 10, &val);
	if (ret < 0 || val > 100)
		return -EINVAL;

	ktch_mgr_eas_boost = (int)val;
	return cnt;
}

static int perfmgr_tb_eas_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_eas_boost);
	return 0;
}

static int perfmgr_tb_eas_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_eas_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_eas_fops = {
	.open = perfmgr_tb_eas_open,
	.write = perfmgr_tb_eas_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t perfmgr_tb_gpu_opp_write(struct file *filp, const char *ubuf,
		size_t cnt, loff_t *data)
{
	char buf[32];
	long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = 0;
	ret = kstrtol(buf, 10, &val);
	if (ret < 0 || val < -1 || val > 48)
		return -EINVAL;

	ktch_mgr_gpu_opp = (int)val;
	return cnt;
}

static int perfmgr_tb_gpu_opp_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_gpu_opp);
	return 0;
}

static int perfmgr_tb_gpu_opp_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_gpu_opp_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_gpu_opp_fops = {
	.open = perfmgr_tb_gpu_opp_open,
	.write = perfmgr_tb_gpu_opp_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int perfmgr_tb_clstr_show(struct seq_file *m, void *v)
{
	if (m)
		seq_printf(m, "%d\n", ktch_mgr_clstr);
	return 0;
}

static int perfmgr_tb_clstr_open(struct inode *inode, struct file *file)
{
	return single_open(file, perfmgr_tb_clstr_show, inode->i_private);
}

static const struct file_operations perfmgr_tb_clstr_fops = {
	.open = perfmgr_tb_clstr_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void dbs_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	unsigned long flags;
	bool need_wake = false;

	if (!ktch_mgr_enable)
		return;

	if ((type == EV_KEY) && (code == BTN_TOUCH)) {
		spin_lock_irqsave(&ktchboost.touch_lock, flags);
		ktchboost.touch_event = value;
		if (value) {
			hrtimer_try_to_cancel(&ktchboost.tail_timer);
			if (!ktchboost.desired_boost) {
				ktchboost.desired_boost = true;
				need_wake = true;
			}
		} else {
			if (ktch_mgr_duration_ms > 0) {
				hrtimer_start(&ktchboost.tail_timer,
					ms_to_ktime(ktch_mgr_duration_ms),
					HRTIMER_MODE_REL);
			} else if (ktchboost.desired_boost) {
				ktchboost.desired_boost = false;
				need_wake = true;
			}
		}
		spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

		if (need_wake) {
			atomic_inc(&ktchboost.event);
			wake_up(&ktchboost.wq);
		}
	}
}

static int dbs_input_connect(struct input_handler *handler,
		struct input_dev *dev,
		const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	if (!test_bit(BTN_TOUCH, dev->keybit))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "perfmgr";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void dbs_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dbs_ids[] = {
	{.driver_info = 1},
	{},
};

static struct input_handler dbs_input_handler = {
	.event = dbs_input_event,
	.connect = dbs_input_connect,
	.disconnect = dbs_input_disconnect,
	.name = "cpufreq_ond",
	.id_table = dbs_ids,
};

int init_ktch(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ktch_root = NULL;
	int ret;

	ktch_mgr_core = ktch_get_target_core();
	ktch_mgr_freq = ktch_get_target_freq();
	ktch_mgr_clstr = perfmgr_clusters;

	ktch_root = proc_mkdir("kernel", parent);
	if (ktch_root) {
		proc_create("tb_enable", 0644, ktch_root, &perfmgr_tb_enable_fops);
		proc_create("tb_core", 0644, ktch_root, &perfmgr_tb_core_fops);
		proc_create("tb_freq", 0644, ktch_root, &perfmgr_tb_freq_fops);
		proc_create("tb_clstr", 0644, ktch_root, &perfmgr_tb_clstr_fops);
		proc_create("tb_duration_ms", 0644, ktch_root, &perfmgr_tb_duration_fops);
		proc_create("tb_eas_boost", 0644, ktch_root, &perfmgr_tb_eas_fops);
		proc_create("tb_gpu_opp", 0644, ktch_root, &perfmgr_tb_gpu_opp_fops);
	}

	spin_lock_init(&ktchboost.touch_lock);
	mutex_init(&ktchboost.apply_lock);
	init_waitqueue_head(&ktchboost.wq);
	hrtimer_init(&ktchboost.tail_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ktchboost.tail_timer.function = ktch_tail_timer_cb;
	ktchboost.desired_boost = false;
	ktchboost.applied_boost = false;
	atomic_set(&ktchboost.event, 0);

	ktchboost.thread = kthread_run(ktchboost_thread, &ktchboost, "touch_boost");
	if (IS_ERR(ktchboost.thread))
		return -EINVAL;

	ret = input_register_handler(&dbs_input_handler);
	if (ret)
		pr_err("input_register_handler failed: %d\n", ret);

	return 0;
}

int ktch_suspend(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ktchboost.touch_lock, flags);
	hrtimer_try_to_cancel(&ktchboost.tail_timer);
	ktchboost.desired_boost = false;
	ktchboost.touch_event = 0;
	spin_unlock_irqrestore(&ktchboost.touch_lock, flags);

	mutex_lock(&ktchboost.apply_lock);
	if (ktchboost.applied_boost) {
		apply_touch_boost(false);
		ktchboost.applied_boost = false;
	}
	mutex_unlock(&ktchboost.apply_lock);

	return 0;
}
