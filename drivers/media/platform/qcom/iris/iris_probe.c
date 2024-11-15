// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_queue.h"
#include "iris_vidc.h"
#include "resources.h"

static int init_iris_isr(struct iris_core *core)
{
	int ret;

	ret = devm_request_threaded_irq(core->dev, core->irq, iris_hfi_isr,
					iris_hfi_isr_handler, IRQF_TRIGGER_HIGH, "iris", core);
	if (ret) {
		dev_err(core->dev, "%s: Failed to allocate iris IRQ\n", __func__);
		return ret;
	}
	disable_irq_nosync(core->irq);

	return ret;
}

static void iris_unregister_video_device(struct iris_core *core, enum domain_type type)
{
	if (type == DECODER)
		video_unregister_device(core->vdev_dec);
	else if (type == ENCODER)
		video_unregister_device(core->vdev_enc);
}

static int iris_register_video_device(struct iris_core *core, enum domain_type type)
{
	struct video_device *vdev;
	int ret = 0;

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	vdev->release = video_device_release;
	vdev->fops = core->v4l2_file_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	if (type == DECODER) {
		strscpy(vdev->name, "qcom-iris-decoder", sizeof(vdev->name));
		vdev->ioctl_ops = core->v4l2_ioctl_ops_dec;

		ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
		if (ret)
			goto err_vdev_release;

		core->vdev_dec = vdev;
	} else if (type == ENCODER) {
		strscpy(vdev->name, "qcom-iris-encoder", sizeof(vdev->name));
		vdev->ioctl_ops = core->v4l2_ioctl_ops_enc;

		ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
		if (ret)
			goto err_vdev_release;

		core->vdev_enc = vdev;
	}

	video_set_drvdata(vdev, core);

	return ret;

err_vdev_release:
	video_device_release(vdev);

	return ret;
}

static void iris_remove(struct platform_device *pdev)
{
	struct iris_core *core;

	core = platform_get_drvdata(pdev);
	if (!core)
		return;

	iris_pm_get(core);

	iris_core_deinit(core);
	iris_hfi_queue_deinit(core);

	iris_unregister_video_device(core, DECODER);

	iris_unregister_video_device(core, ENCODER);

	v4l2_device_unregister(&core->v4l2_dev);

	iris_pm_put(core, false);

	mutex_destroy(&core->lock);
	core->state = IRIS_CORE_DEINIT;
}

static int iris_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iris_core *core;
	u64 dma_mask;
	int ret;

	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = dev;

	core->state = IRIS_CORE_DEINIT;
	mutex_init(&core->lock);

	core->packet_size = IFACEQ_CORE_PKT_SIZE;
	core->packet = devm_kzalloc(core->dev, core->packet_size, GFP_KERNEL);
	if (!core->packet)
		return -ENOMEM;

	core->response_packet = devm_kzalloc(core->dev, core->packet_size, GFP_KERNEL);
	if (!core->response_packet)
		return -ENOMEM;

	INIT_LIST_HEAD(&core->instances);

	core->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->reg_base))
		return PTR_ERR(core->reg_base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	pm_runtime_set_autosuspend_delay(core->dev, AUTOSUSPEND_DELAY_VALUE);
	pm_runtime_use_autosuspend(core->dev);
	ret = devm_pm_runtime_enable(core->dev);
	if (ret) {
		dev_err(core->dev, "failed to enable runtime pm\n");
		goto err_runtime_disable;
	}

	ret = init_iris_isr(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: Failed to init isr with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = init_platform(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init platform failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = init_vpu(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init vpu failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = init_ops(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init ops failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = init_resources(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init resource failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = iris_init_core_caps(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init core caps failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = iris_init_instance_caps(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init inst caps failed with %d\n", __func__, ret);
		goto err_runtime_disable;
	}

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		goto err_runtime_disable;

	ret = iris_register_video_device(core, DECODER);
	if (ret)
		goto err_v4l2_unreg;

	ret = iris_register_video_device(core, ENCODER);
	if (ret)
		goto err_vdev_unreg_dec;

	platform_set_drvdata(pdev, core);

	dma_mask = core->cap[DMA_MASK].value;

	ret = dma_set_mask_and_coherent(dev, dma_mask);
	if (ret)
		goto err_vdev_unreg_enc;

	dma_set_max_seg_size(&pdev->dev, (unsigned int)DMA_BIT_MASK(32));
	dma_set_seg_boundary(&pdev->dev, (unsigned long)DMA_BIT_MASK(64));

	ret = iris_hfi_queue_init(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: interface queues init failed\n", __func__);
		goto err_vdev_unreg_enc;
	}

	ret = iris_pm_get(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "%s: failed to get runtime pm\n", __func__);
		goto err_queue_deinit;
	}

	ret = iris_core_init(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "%s: core init failed\n", __func__);
		goto err_queue_deinit;
	}

	ret = iris_pm_put(core, false);
	if (ret) {
		pm_runtime_get_noresume(core->dev);
		dev_err_probe(core->dev, ret, "%s: failed to put runtime pm\n", __func__);
		goto err_core_deinit;
	}

	return ret;

err_core_deinit:
	iris_core_deinit(core);
err_queue_deinit:
	iris_hfi_queue_deinit(core);
err_vdev_unreg_enc:
	iris_unregister_video_device(core, ENCODER);
err_vdev_unreg_dec:
	iris_unregister_video_device(core, DECODER);
err_v4l2_unreg:
	v4l2_device_unregister(&core->v4l2_dev);
err_runtime_disable:
	pm_runtime_put_noidle(core->dev);
	pm_runtime_set_suspended(core->dev);

	return ret;
}

static int iris_pm_suspend(struct device *dev)
{
	struct iris_core *core;
	int ret;

	if (!dev || !dev->driver)
		return 0;

	core = dev_get_drvdata(dev);

	mutex_lock(&core->lock);

	if (core->num_pending_requests > 0) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!core_in_valid_state(core)) {
		ret = 0;
		goto unlock;
	}

	if (!core->power_enabled) {
		ret = 0;
		goto unlock;
	}

	ret = iris_hfi_pm_suspend(core);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

static int iris_pm_resume(struct device *dev)
{
	struct iris_core *core;
	int ret;

	if (!dev || !dev->driver)
		return 0;

	core = dev_get_drvdata(dev);

	if (!core_in_valid_state(core)) {
		ret = 0;
		goto exit;
	}

	if (core->power_enabled) {
		ret = 0;
		goto exit;
	}

	ret = iris_hfi_pm_resume(core);

	iris_pm_touch_unlocked(core);

exit:

	return ret;
}

static const struct dev_pm_ops iris_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iris_pm_suspend, iris_pm_resume, NULL)
};

static const struct of_device_id iris_dt_match[] = {
	{
		.compatible = "qcom,sm8550-iris",
		.data = &sm8550_data,
	},
	{
		.compatible = "qcom,qcm6490-iris",
		.data = &qcm6490_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, iris_dt_match);

static struct platform_driver qcom_iris_driver = {
	.probe = iris_probe,
	.remove_new = iris_remove,
	.driver = {
		.name = "qcom-iris",
		.of_match_table = iris_dt_match,
		.pm = &iris_pm_ops,
	},
};

module_platform_driver(qcom_iris_driver);
MODULE_IMPORT_NS(DMA_BUF);
MODULE_DESCRIPTION("Qualcomm Iris video driver");
MODULE_LICENSE("GPL");
