/*
 * Google LWIS Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-device: " fmt

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "lwis_commands.h"
#include "lwis_device.h"
#include "lwis_dt.h"
#include "lwis_i2c.h"
#include "lwis_ioctl.h"

#define LWIS_CLASS_NAME "lwis"
#define LWIS_DEVICE_NAME "lwis"
#define LWIS_DRIVER_NAME "lwis-driver"
#define LWIS_MAX_DEVICES (1U << MINORBITS)

static struct lwis_core core;

static int lwis_open(struct inode *node, struct file *fp);
static int lwis_release(struct inode *node, struct file *fp);
static long lwis_ioctl(struct file *fp, unsigned int type, unsigned long param);

static struct file_operations lwis_fops = {
	.owner = THIS_MODULE,
	.open = lwis_open,
	.release = lwis_release,
	.unlocked_ioctl = lwis_ioctl,
};

/*
 *  lwis_open: Opening an instance of a LWIS device
 */
static int lwis_open(struct inode *node, struct file *fp)
{
	struct lwis_device *lwis_dev;
	struct lwis_client *lwis_client;

	pr_info("Opening instance %d\n", iminor(node));

	/* Making sure the minor number associated with fp exists */
	mutex_lock(&core.lock);
	lwis_dev = idr_find(core.idr, iminor(node));
	mutex_unlock(&core.lock);
	if (!lwis_dev) {
		pr_err("No device %d found\n", iminor(node));
		return -ENODEV;
	}

	lwis_client = kzalloc(sizeof(struct lwis_client), GFP_KERNEL);
	if (!lwis_client) {
		pr_err("Failed to allocate lwis client\n");
		return -ENOMEM;
	}

	lwis_client->lwis_dev = lwis_dev;
	mutex_init(&lwis_client->lock);

	/* Storing the client handle in fp private_data for easy access */
	fp->private_data = lwis_client;

	return 0;
}

/*
 *  lwis_release: Closing an instance of a LWIS device
 */
static int lwis_release(struct inode *node, struct file *fp)
{
	struct lwis_client *lwis_client = fp->private_data;

	pr_info("Closing instance %d\n", iminor(node));

	kfree(lwis_client);

	return 0;
}

/*
 *  lwis_ioctl: I/O control function on a LWIS device
 *
 *  List of IOCTL types are defined in lwis_commands.h
 */
static long lwis_ioctl(struct file *fp, unsigned int type, unsigned long param)
{
	int ret = 0;
	struct lwis_client *lwis_client;
	struct lwis_device *lwis_dev;

	lwis_client = fp->private_data;
	if (!lwis_client) {
		pr_err("Cannot find client instance\n");
		return -ENODEV;
	}

	lwis_dev = lwis_client->lwis_dev;
	if (!lwis_dev) {
		pr_err("Cannot find device instance\n");
		return -ENODEV;
	}

	mutex_lock(&lwis_client->lock);

	ret = lwis_ioctl_handler(lwis_dev, type, param);

	mutex_unlock(&lwis_client->lock);

	if (ret) {
		pr_err("Error processing IOCTL %d (%d)\n", _IOC_NR(type), ret);
	}

	return ret;
}

#ifdef CONFIG_OF

static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_TOP_DEVICE_COMPAT },
	{ .compatible = LWIS_I2C_DEVICE_COMPAT },
	{ .compatible = LWIS_IOREG_DEVICE_COMPAT },
	{},
};
MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct platform_driver lwis_driver = {
	.driver = {
		.name = LWIS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lwis_id_match,
	},
};

#else

static struct platform_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lwis_driver_id);

static struct platform_driver lwis_driver = { .id_table = lwis_driver_id,
					      .driver = {
						      .name = LWIS_DRIVER_NAME,
						      .owner = THIS_MODULE,
					      } };

#endif /* CONFIG_OF */

/*
 *  lwis_register_device: Create device class and device major number to the
 *  class of LWIS devices.
 *
 *  This is called once when the LWIS driver is registered as a platform device.
 */
static int __init lwis_register_device(void)
{
	int ret = 0;
	dev_t lwis_devt;

	/* Allocate ID management instance for device minor numbers */
	core.idr = kzalloc(sizeof(struct idr), GFP_KERNEL);
	if (!core.idr) {
		pr_err("Cannot allocate idr instance\n");
		return -ENOMEM;
	}

	mutex_lock(&core.lock);

	idr_init(core.idr);

	/* Acquire device major number and allocate the range to minor numbers
	   to the device */
	ret = alloc_chrdev_region(&lwis_devt, 0, LWIS_MAX_DEVICES,
				  LWIS_DEVICE_NAME);
	if (ret) {
		pr_err("Error in allocating chrdev region\n");
		goto error_chrdev_alloc;
	}

	core.device_major = MAJOR(lwis_devt);

	/* Create a device class*/
	core.dev_class = class_create(THIS_MODULE, LWIS_CLASS_NAME);
	if (IS_ERR(core.dev_class)) {
		pr_err("Failed to create device class\n");
		ret = PTR_ERR(core.dev_class);
		goto error_class_create;
	}

	/* Allocate a character device */
	core.chr_dev = cdev_alloc();
	if (!core.chr_dev) {
		pr_err("Failed to allocate cdev\n");
		ret = -ENOMEM;
		goto error_cdev_alloc;
	}

	core.chr_dev->ops = &lwis_fops;

	ret = cdev_add(core.chr_dev, lwis_devt, LWIS_MAX_DEVICES);
	if (ret) {
		pr_err("Failed to add cdev\n");
		goto error_cdev_alloc;
	}

	INIT_LIST_HEAD(&core.lwis_dev_list);

	mutex_unlock(&core.lock);

	return ret;

	/* Error conditions */
error_cdev_alloc:
	class_destroy(core.dev_class);
	core.dev_class = NULL;
error_class_create:
	unregister_chrdev_region(lwis_devt, LWIS_MAX_DEVICES);
error_chrdev_alloc:
	mutex_unlock(&core.lock);
	kfree(core.idr);
	core.idr = NULL;

	return ret;
}

static int lwis_device_initialize(struct lwis_device *lwis_dev)
{
	int ret = 0;

#ifdef CONFIG_OF
	/* Parse device tree for device configurations */
	ret = lwis_device_parse_dt(lwis_dev);
	if (ret) {
		pr_err("Failed to parse device tree\n");
	}
#else
	/* Non-device-tree init: Save for future implementation */
	ret = -ENOSYS;
#endif

	return ret;
}

/*
 *  lwis_probe: Create a device instance for each of the LWIS device.
 */
static int __init lwis_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_device *lwis_dev;

	pr_info("Probe: Begin\n");

	/* Create LWIS device instance */
	lwis_dev = kzalloc(sizeof(struct lwis_device), GFP_KERNEL);
	if (!lwis_dev) {
		pr_err("Failed to allocate lwis_device struct\n");
		return -ENOMEM;
	}

	/* Allocate a minor number to this device */
	mutex_lock(&core.lock);
	ret = idr_alloc(core.idr, lwis_dev, 0, LWIS_MAX_DEVICES, GFP_KERNEL);
	mutex_unlock(&core.lock);
	if (ret >= 0) {
		lwis_dev->id = ret;
		ret = 0;
	} else {
		pr_err("Unable to allocate minor ID (%d)\n", ret);
		goto error_minor_alloc;
	}

	lwis_dev->plat_dev = plat_dev;
	ret = lwis_device_initialize(lwis_dev);
	if (ret) {
		pr_err("Error initializing LWIS device\n");
		goto error_init;
	}

	/* Upon success initialization, create device for this instance */
	lwis_dev->dev = device_create(
		core.dev_class, NULL, MKDEV(core.device_major, lwis_dev->id),
		lwis_dev, LWIS_DEVICE_NAME "-%s", lwis_dev->name);
	if (IS_ERR(lwis_dev->dev)) {
		pr_err("Failed to create device\n");
		ret = PTR_ERR(lwis_dev->dev);
		goto error_init;
	}

	/* Add this instance to the device list */
	INIT_LIST_HEAD(&lwis_dev->dev_list);
	list_add(&lwis_dev->dev_list, &core.lwis_dev_list);

	platform_set_drvdata(plat_dev, lwis_dev);

	pr_info("Probe: Done\n");

	return ret;

	/* Error conditions */
error_init:
	mutex_lock(&core.lock);
	idr_remove(core.idr, lwis_dev->id);
	mutex_unlock(&core.lock);
error_minor_alloc:
	kfree(lwis_dev);
	return ret;
}

/*
 *  lwis_device_init: Called during device_initcall_sync routines.
 */
static int __init lwis_device_init(void)
{
	int ret = 0;

	pr_info("Device initialization\n");

	/* Initialize the core struct */
	memset(&core, 0, sizeof(struct lwis_core));
	mutex_init(&core.lock);

	lwis_register_device();

	ret = platform_driver_probe(&lwis_driver, lwis_probe);
	if (ret) {
		pr_err("platform_driver_probe failed - %d", ret);
	}

	return ret;
}

device_initcall_sync(lwis_device_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google-ACMA");
MODULE_DESCRIPTION("LWIS Driver");
