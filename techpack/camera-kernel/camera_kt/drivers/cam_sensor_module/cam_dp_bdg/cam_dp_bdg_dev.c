/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "cam_dp_bdg_core.h"
#include "camera_main.h"

#define DP_BDG_IRQ_HANDLER_DEVNAME             "dp_bdg_irq_handler"
#define DP_BDG_IRQ_HANDLER_MAGIC_NUM           0xff
#define DP_BDG_IRQ_HANDLER_IOCTL_GETRES        0x05
#define DP_BDG_IRQ_HANDLER_IOCTL_GETRES_DIR    0x06
#define DP_BDG_IRQ_HANDLER_IOCTL_RST           0x07
#define DP_BDG_IRQ_HANDLER_IOCTL_UPGRADE_FW    0x08

struct dp_bdg_res_info {
	int32_t width;
	int32_t height;
	bool have_dp_signal;
	int32_t id;
};

#define DP_BDG_IRQ_HANDLER_IOCTL_CMD_GETRES   \
	_IOR(DP_BDG_IRQ_HANDLER_MAGIC_NUM,        \
	DP_BDG_IRQ_HANDLER_IOCTL_GETRES, \
			struct dp_bdg_res_info)
#define DP_BDG_IRQ_HANDLER_IOCTL_CMD_GETRES_DIR   \
	_IOR(DP_BDG_IRQ_HANDLER_MAGIC_NUM,            \
	DP_BDG_IRQ_HANDLER_IOCTL_GETRES_DIR, \
			struct dp_bdg_res_info)
#define DP_BDG_IRQ_HANDLER_IOCTL_CMD_RST   \
	_IO(DP_BDG_IRQ_HANDLER_MAGIC_NUM,      \
	DP_BDG_IRQ_HANDLER_IOCTL_RST)
#define DP_BDG_IRQ_HANDLER_IOCTL_CMD_UPGRADE_FW   \
	_IO(DP_BDG_IRQ_HANDLER_MAGIC_NUM,      \
	DP_BDG_IRQ_HANDLER_IOCTL_UPGRADE_FW)


static const struct  of_device_id dp_bdg_irq_handler_dev_id[] = {
	{ .compatible = "dp_bdg_irq_handler" },
	{},
};

static bool is_irq_happen;
static int dp_bdg_irq;
static int dp_bdg_irq_gpio;
static wait_queue_head_t dp_bdg_read_wq;
static struct dp_bdg_res_info s_dp_bdg_res_info;
static struct cdev *dp_bdg_irq_handler_cdev;
static dev_t dp_bdg_irq_handler_dev;
static struct class *dp_bdg_irq_handler_class;
static struct device *dp_bdg_irq_handler_device;

MODULE_DEVICE_TABLE(of, dp_bdg_irq_handler_dev_id);

static int dp_bdg_irq_handler_dev_open(struct inode *inodp, struct file *filp)
{
	return 0;
}
static int dp_bdg_irq_handler_dev_release(struct inode *inodp,
		struct file *filp)
{
	wake_up_all(&dp_bdg_read_wq);
	return 0;
}
static ssize_t dp_bdg_irq_handler_dev_read(struct file *filp,
		char __user *buf, size_t n_read, loff_t *off)
{
	CAM_INFO(CAM_SENSOR, "dp_bdg_irq_handler: wait IRQ happened");
	wait_event_interruptible(dp_bdg_read_wq, is_irq_happen);
	CAM_INFO(CAM_SENSOR, "dp_bdg_irq_handler: IRQ happened");
	is_irq_happen = false;
	return 0;
}

static long dp_bdg_irq_handler_dev_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	int rc = 0;

	switch (cmd) {
	case DP_BDG_IRQ_HANDLER_IOCTL_CMD_GETRES:
		rc = copy_to_user((struct dp_bdg_res_info *)arg,
			&s_dp_bdg_res_info,
			sizeof(s_dp_bdg_res_info));
		if (rc != 0)
			CAM_ERR(CAM_SENSOR, "dp_bdg_irq_handler: IOCTL read error!");
		break;
	case DP_BDG_IRQ_HANDLER_IOCTL_CMD_GETRES_DIR:
		CAM_INFO(CAM_SENSOR, "LT7911 firmare version: 0x%08x", cam_dp_bdg_get_fw_version());
		rc = cam_dp_bdg_get_src_resolution(
			&s_dp_bdg_res_info.have_dp_signal,
			&s_dp_bdg_res_info.width,
			&s_dp_bdg_res_info.height,
			&s_dp_bdg_res_info.id);
		if (!rc) {
			CAM_INFO(CAM_SENSOR, "DP_BDG Input resolution = %d x %d",
				s_dp_bdg_res_info.width,
				s_dp_bdg_res_info.height);
		}
		rc = copy_to_user((struct dp_bdg_res_info *)arg,
			&s_dp_bdg_res_info,
			sizeof(s_dp_bdg_res_info));
		if (rc != 0)
			CAM_ERR(CAM_SENSOR, "dp_bdg_irq_handler: IOCTL read error!");
		break;
	case DP_BDG_IRQ_HANDLER_IOCTL_CMD_RST:
		s_dp_bdg_res_info.width = -1;
		s_dp_bdg_res_info.height = -1;
		s_dp_bdg_res_info.have_dp_signal = false;
		s_dp_bdg_res_info.id = 0;
		break;
	case DP_BDG_IRQ_HANDLER_IOCTL_CMD_UPGRADE_FW:
		CAM_INFO(CAM_SENSOR, "dp_bdg_irq_handler: Upgrading firmware...");
		rc = cam_dp_bdg_upgrade_firmware();
		CAM_INFO(CAM_SENSOR, "dp_bdg_irq_handler: rc %d", rc);
		break;
	default:
		CAM_ERR(CAM_SENSOR, "dp_bdg_irq_handler: IOCTL unknown command!");
		break;
	}
	return rc;
}

#ifdef CONFIG_COMPAT
static long dp_bdg_irq_handler_dev_compat_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	return dp_bdg_irq_handler_dev_ioctl(filp, cmd,
		(unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations dp_bdg_irq_handler_dev_fops = {
	.owner = THIS_MODULE,
	.read = dp_bdg_irq_handler_dev_read,
	.open = dp_bdg_irq_handler_dev_open,
	.unlocked_ioctl = dp_bdg_irq_handler_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = dp_bdg_irq_handler_dev_compat_ioctl,
#endif
	.release = dp_bdg_irq_handler_dev_release,
};

static irqreturn_t dp_bdg_irq_handler(int irq, void *p)
{
	int rc = 0;

	CAM_INFO(CAM_SENSOR, "dp hotplug happened");
	rc = cam_dp_bdg_get_src_resolution(
			&s_dp_bdg_res_info.have_dp_signal,
			&s_dp_bdg_res_info.width,
			&s_dp_bdg_res_info.height,
			&s_dp_bdg_res_info.id);
	if (!rc) {
		CAM_INFO(CAM_SENSOR, "DP_BDG Input resolution = %d x %d",
				s_dp_bdg_res_info.width,
				s_dp_bdg_res_info.height);
		is_irq_happen = true;
		wake_up_all(&dp_bdg_read_wq);
	} else {
		CAM_ERR(CAM_SENSOR, "Get resolution failed!");
	}
	return IRQ_HANDLED;
}

static int dp_bdg_irq_handler_probe(struct platform_device *pdev)
{
	int ret = 0;

	dp_bdg_irq_gpio = of_get_named_gpio(pdev->dev.of_node,
		"dp_bdg_irq_pin", 0);
	ret = gpio_request(dp_bdg_irq_gpio, "dp_bdg_irq_pin");
	if (ret) {
		CAM_ERR(CAM_SENSOR, "Error! can't request irq pin %x", dp_bdg_irq_gpio);
		ret = -EINVAL;
		goto fail_exit;
	}
	dp_bdg_irq = gpio_to_irq(dp_bdg_irq_gpio);
	ret = request_threaded_irq(dp_bdg_irq, NULL,
			dp_bdg_irq_handler,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"dp_bdg_irq", NULL);
	if (ret) {
		CAM_ERR(CAM_SENSOR, "Failed to request irq");
		ret = -EINVAL;
		goto fail_exit;
	}

	dp_bdg_irq_handler_cdev = cdev_alloc();
	if (!dp_bdg_irq_handler_cdev) {
		ret = -ENOMEM;
		goto fail_exit;
	}
	dp_bdg_irq_handler_cdev->ops = &dp_bdg_irq_handler_dev_fops;
	dp_bdg_irq_handler_cdev->owner = THIS_MODULE;
	ret = alloc_chrdev_region(&dp_bdg_irq_handler_dev, 0, 1,
			DP_BDG_IRQ_HANDLER_DEVNAME);
	if (ret) {
		CAM_ERR(CAM_SENSOR, "alloc_chrdev_region failed with error code %d", ret);
		goto fail_cdev_del;
	}
	ret = cdev_add(dp_bdg_irq_handler_cdev, dp_bdg_irq_handler_dev, 1);
	if (ret)
		goto fail_cdev_del;
	dp_bdg_irq_handler_class = class_create(DP_BDG_IRQ_HANDLER_DEVNAME);
	if (IS_ERR(dp_bdg_irq_handler_class)) {
		ret = PTR_ERR(dp_bdg_irq_handler_class);
		goto fail_cdev_del;
	}
	dp_bdg_irq_handler_device = device_create(dp_bdg_irq_handler_class,
			NULL, dp_bdg_irq_handler_dev, NULL,
			DP_BDG_IRQ_HANDLER_DEVNAME);
	if (IS_ERR(dp_bdg_irq_handler_device)) {
		ret = PTR_ERR(dp_bdg_irq_handler_device);
		goto fail_class_destroy;
	}
	init_waitqueue_head(&dp_bdg_read_wq);
	return 0;

fail_class_destroy:
	class_destroy(dp_bdg_irq_handler_class);
fail_cdev_del:
	cdev_del(dp_bdg_irq_handler_cdev);
fail_exit:
	return ret;
}

static int dp_bdg_irq_handler_remove(struct platform_device *pdev)
{
	gpio_free(dp_bdg_irq_gpio);
	disable_irq(dp_bdg_irq);
	wake_up_all(&dp_bdg_read_wq);

	cdev_del(dp_bdg_irq_handler_cdev);
	device_destroy(dp_bdg_irq_handler_class, dp_bdg_irq_handler_dev);
	class_destroy(dp_bdg_irq_handler_class);
	unregister_chrdev_region(dp_bdg_irq_handler_dev, 1);
	return 0;
}

struct platform_driver dp_bdg_irq_handler_driver = {
	.probe = dp_bdg_irq_handler_probe,
	.remove = dp_bdg_irq_handler_remove,
	.suspend = NULL,
	.resume = NULL,
	.driver = {
		.name = DP_BDG_IRQ_HANDLER_DEVNAME,
		.of_match_table = of_match_ptr(dp_bdg_irq_handler_dev_id),
	},
};

int dp_bdg_irq_handler_init(void)
{
	return platform_driver_register(&dp_bdg_irq_handler_driver);
}

void dp_bdg_irq_handler_exit(void)
{
	platform_driver_unregister(&dp_bdg_irq_handler_driver);
}

MODULE_DESCRIPTION("dp driver");
MODULE_LICENSE("GPL v2");
