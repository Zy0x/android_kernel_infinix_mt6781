/*
 * Copyright (C) 2011-2014 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "<HWMSEN> " fmt

#include "sensor_attr.h"
#include "sensor_event.h"
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernfs.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/uidgid.h>

#define SENSOR_SYSTEM_UID	1000
#define SENSOR_SYSTEM_GID	1000

static void sensor_attr_release(struct device *dev)
{
	kfree(dev);
}

static char *sensor_attr_devnode(struct device *dev, umode_t *mode,
				 kuid_t *uid, kgid_t *gid)
{
	if (mode)
		*mode = 0660;
	if (uid)
		*uid = make_kuid(&init_user_ns, SENSOR_SYSTEM_UID);
	if (gid)
		*gid = make_kgid(&init_user_ns, SENSOR_SYSTEM_GID);
	return NULL;
}

static const struct device_type sensor_attr_dev_type = {
	.name = "sensor_attr",
	.devnode = sensor_attr_devnode,
	.release = sensor_attr_release,
};

static void sensor_attr_chown_kn(struct kernfs_node *kn, umode_t mode)
{
	struct iattr iattr;

	if (!kn)
		return;

	memset(&iattr, 0, sizeof(iattr));
	iattr.ia_valid = ATTR_UID | ATTR_GID;
	iattr.ia_uid = make_kuid(&init_user_ns, SENSOR_SYSTEM_UID);
	iattr.ia_gid = make_kgid(&init_user_ns, SENSOR_SYSTEM_GID);
	if (mode) {
		iattr.ia_valid |= ATTR_MODE;
		iattr.ia_mode = (kn->mode & S_IFMT) | (mode & 07777);
	}
	kernfs_setattr(kn, &iattr);
}

static int sensor_attr_major = -1;
static struct class *sensor_attr_class;

static LIST_HEAD(sensor_attr_list);
static DEFINE_MUTEX(sensor_attr_mtx);

static int sensor_attr_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct sensor_attr_t *c;
	int err = -ENODEV;
	const struct file_operations *new_fops = NULL;

	mutex_lock(&sensor_attr_mtx);

	list_for_each_entry(c, &sensor_attr_list, list) {
		if (c->minor == minor) {
			new_fops = fops_get(c->fops);
			break;
		}
	}

	if (!new_fops) {
		mutex_unlock(&sensor_attr_mtx);
		request_module("char-major-%d-%d", sensor_attr_major, minor);
		mutex_lock(&sensor_attr_mtx);

		list_for_each_entry(c, &sensor_attr_list, list) {
			if (c->minor == minor) {
				new_fops = fops_get(c->fops);
				break;
			}
		}
		if (!new_fops)
			goto fail;
	}

	err = 0;
	replace_fops(file, new_fops);
	if (file->f_op->open) {
		file->private_data = c;
		err = file->f_op->open(inode, file);
	}
fail:
	mutex_unlock(&sensor_attr_mtx);
	return err;
}

static const struct file_operations sensor_attr_fops = {
	.owner = THIS_MODULE,
	.open = sensor_attr_open,
};

int sensor_attr_register(struct sensor_attr_t *misc)
{
	dev_t devt;
	int err = 0;
	struct device *dev;
	struct sensor_attr_t *c;
	struct kernfs_node *dev_kn, *sub_kn, *kn;

	mutex_lock(&sensor_attr_mtx);
	list_for_each_entry(c, &sensor_attr_list, list) {
		if (c->minor == misc->minor) {
			err = -EBUSY;
			goto out;
		}
	}
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto out;
	}
	devt = MKDEV(sensor_attr_major, misc->minor);
	device_initialize(dev);
	dev->devt = devt;
	dev->class = sensor_attr_class;
	dev->parent = misc->parent;
	dev->type = &sensor_attr_dev_type;
	dev_set_drvdata(dev, misc);
	err = kobject_set_name(&dev->kobj, "%s", misc->name);
	if (err) {
		put_device(dev);
		misc->this_device = ERR_PTR(err);
		goto out;
	}
	err = device_add(dev);
	if (err) {
		put_device(dev);
		misc->this_device = ERR_PTR(err);
		goto out;
	}
	misc->this_device = dev;

	dev_kn = dev->kobj.sd;
	if (dev_kn) {
		sensor_attr_chown_kn(dev_kn->parent, 0755);
		sensor_attr_chown_kn(dev_kn, 0755);
		sub_kn = kernfs_find_and_get(dev_kn, "subsystem");
		if (sub_kn) {
			if (kernfs_type(sub_kn) == KERNFS_LINK &&
			    sub_kn->symlink.target_kn)
				sensor_attr_chown_kn(sub_kn->symlink.target_kn,
						     0755);
			kernfs_put(sub_kn);
		}
		kn = kernfs_find_and_get(dev_kn, "dev");
		if (kn) {
			sensor_attr_chown_kn(kn, 0444);
			kernfs_put(kn);
		}
		kn = kernfs_find_and_get(dev_kn, "uevent");
		if (kn) {
			sensor_attr_chown_kn(kn, 0644);
			kernfs_put(kn);
		}
	}

	list_add(&misc->list, &sensor_attr_list);
	mutex_unlock(&sensor_attr_mtx);
	err = sensor_event_register(misc->minor);
	return err;
out:
	mutex_unlock(&sensor_attr_mtx);
	return err;
}

int sensor_attr_create_group(struct sensor_attr_t *misc,
			     const struct attribute_group *grp)
{
	int err;
	struct kernfs_node *dev_kn, *base_kn, *kn;
	struct attribute * const *attr;

	if (!misc || IS_ERR_OR_NULL(misc->this_device) || !grp)
		return -EINVAL;

	err = sysfs_create_group(&misc->this_device->kobj, grp);
	if (err < 0)
		return err;

	dev_kn = misc->this_device->kobj.sd;
	if (!dev_kn || !grp->attrs)
		return 0;

	if (grp->name) {
		base_kn = kernfs_find_and_get(dev_kn, grp->name);
		if (!base_kn)
			return 0;
		sensor_attr_chown_kn(base_kn, 0755);
	} else {
		kernfs_get(dev_kn);
		base_kn = dev_kn;
	}

	for (attr = grp->attrs; *attr; attr++) {
		kn = kernfs_find_and_get(base_kn, (*attr)->name);
		if (kn) {
			umode_t m = (kn->mode & 0222) ? 0660 : 0440;

			sensor_attr_chown_kn(kn, m);
			kernfs_put(kn);
		}
	}
	kernfs_put(base_kn);
	return 0;
}
int sensor_attr_deregister(struct sensor_attr_t *misc)
{
	if (WARN_ON(list_empty(&misc->list)))
		return -EINVAL;

	mutex_lock(&sensor_attr_mtx);
	list_del(&misc->list);
	device_destroy(sensor_attr_class,
		       MKDEV(sensor_attr_major, misc->minor));
	mutex_unlock(&sensor_attr_mtx);
	sensor_event_deregister(misc->minor);
	return 0;
}
#if 0
static char *sensor_attr_devnode(struct device *dev, umode_t *mode)
{
	pr_debug("sensor_attr: name :%s\n", dev_name(dev));
	return kasprintf(GFP_KERNEL, "sensor/%s", dev_name(dev));
}
#endif
static int __init sensor_attr_init(void)
{
	int err;

	sensor_attr_class = class_create(THIS_MODULE, "sensor");
	err = PTR_ERR(sensor_attr_class);
	if (IS_ERR(sensor_attr_class)) {
		err = -EIO;
		return err;
	}
	sensor_attr_major = register_chrdev(0, "sensor", &sensor_attr_fops);
	if (sensor_attr_major < 0)
		goto fail_printk;
	/* sensor_attr_class->devnode = sensor_attr_devnode; */
	return 0;

fail_printk:
	pr_err("unable to get major %d for misc devices\n", sensor_attr_major);
	class_destroy(sensor_attr_class);
	return err;
}
subsys_initcall(sensor_attr_init);
