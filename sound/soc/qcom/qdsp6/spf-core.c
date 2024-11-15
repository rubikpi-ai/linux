// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/jiffies.h>
#include <linux/soc/qcom/apr.h>
#include <dt-bindings/soc/qcom,gpr.h>

#define APM_STATE_READY_TIMEOUT_MS    10000
#define Q6_READY_TIMEOUT_MS 1000
#define APM_CMD_GET_SPF_STATE 0x01001021
#define APM_CMD_RSP_GET_SPF_STATE 0x02001007
#define APM_MODULE_INSTANCE_ID   0x00000001
#define GPR_SVC_ADSP_CORE 0x3

struct spf_core {
	gpr_device_t *adev;
	wait_queue_head_t wait;
	struct mutex lock;
	bool resp_received;
	bool is_ready;
};

struct spf_core_private {
	struct device *dev;
	struct mutex lock;
	struct spf_core *spf_core_drv;
	bool is_initial_boot;
	struct work_struct add_chld_dev_work;
};

static struct spf_core_private *spf_core_priv;
struct apm_cmd_rsp_get_spf_status_t

{
	/* Spf status
	 * @values
	 * 0 -> Not ready
	 * 1 -> Ready
	 */
	uint32_t status;

};

static int spf_core_callback(struct gpr_resp_pkt *data, void *priv, int op)
{
	gpr_device_t *gdev = priv;
	struct spf_core *core = dev_get_drvdata(&gdev->dev);
	struct gpr_ibasic_rsp_result_t *result;
	struct gpr_hdr *hdr = &data->hdr;

	result = data->payload;

	switch (hdr->opcode) {
	case GPR_BASIC_RSP_RESULT:
		dev_err(&gdev->dev, "%s: Failed response received\n", __func__);
		core->resp_received = true;
		break;
	case APM_CMD_RSP_GET_SPF_STATE:
		core->is_ready = result->opcode;
		dev_err(&gdev->dev, "%s: success response received, core->is_ready=%d\n",
				__func__, core->is_ready);
		core->resp_received = true;
		break;
	default:
		dev_err(&gdev->dev, "Message ID from apm: 0x%x\n",
			hdr->opcode);
		break;
	}
	if (core->resp_received)
		wake_up(&core->wait);

	return 0;
}

static bool __spf_core_is_apm_ready(struct spf_core *core)
{
	gpr_device_t *adev = core->adev;
	struct gpr_pkt pkt;
	int rc;

	pkt.hdr.version = GPR_PKT_VER;
	pkt.hdr.hdr_size = GPR_PKT_HEADER_WORD_SIZE;
	pkt.hdr.pkt_size = GPR_HDR_SIZE;

	pkt.hdr.opcode = APM_CMD_GET_SPF_STATE;
	pkt.hdr.dest_port = APM_MODULE_INSTANCE_ID;
	pkt.hdr.src_port = adev->svc_id; //1
	pkt.hdr.dest_domain = GPR_DOMAIN_ID_ADSP;
	pkt.hdr.src_domain = GPR_DOMAIN_ID_APPS;
	pkt.hdr.opcode = APM_CMD_GET_SPF_STATE;

	rc = gpr_send_pkt(adev, &pkt);
	if (rc < 0)
		return false;

	rc = wait_event_timeout(core->wait,
			(core->resp_received), msecs_to_jiffies(Q6_READY_TIMEOUT_MS));

	if (rc > 0 && core->resp_received) {
		core->resp_received = false;

		if (core->is_ready)
			return true;
	} else {
		dev_err(spf_core_priv->dev, "%s: command timedout, ret\n",
			__func__);
	}

	return false;
}

/**
 * spf_core_is_apm_ready() - Get status of adsp
 *
 * Return: Will be an true if apm is ready and false if not.
 */
bool spf_core_is_apm_ready(void)
{
	unsigned long  timeout;
	bool ret = false;
	struct spf_core *core;

	if (!spf_core_priv)
		return ret;

	core = spf_core_priv->spf_core_drv;
	if (!core)
		return ret;

	mutex_lock(&core->lock);
	timeout = jiffies + msecs_to_jiffies(APM_STATE_READY_TIMEOUT_MS);
	for (;;) {
		if (__spf_core_is_apm_ready(core)) {
			ret = true;
			break;
		}
		usleep_range(300000, 300050);
		if (!time_after(timeout, jiffies)) {
			ret = false;
			break;
		}
	}

	mutex_unlock(&core->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(spf_core_is_apm_ready);

static int spf_core_probe(gpr_device_t *adev)
{
	struct spf_core *core;

	if (!spf_core_priv) {
		pr_err("%s: spf_core platform probe not yet done\n", __func__);
		return -EPROBE_DEFER;
	}
	mutex_lock(&spf_core_priv->lock);
	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core) {
		mutex_unlock(&spf_core_priv->lock);
		return -ENOMEM;
	}
	dev_set_drvdata(&adev->dev, core);

	mutex_init(&core->lock);
	core->adev = adev;
	init_waitqueue_head(&core->wait);
	spf_core_priv->spf_core_drv = core;
	if (spf_core_priv->is_initial_boot)
		schedule_work(&spf_core_priv->add_chld_dev_work);
	mutex_unlock(&spf_core_priv->lock);

	return 0;
}

static void spf_core_exit(gpr_device_t *adev)
{
	struct spf_core *core = dev_get_drvdata(&adev->dev);

	if (!spf_core_priv) {
		pr_err("%s: spf_core platform probe not yet done\n", __func__);
		return;
	}
	mutex_lock(&spf_core_priv->lock);
	spf_core_priv->spf_core_drv = NULL;
	kfree(core);
	mutex_unlock(&spf_core_priv->lock);
}

static const struct of_device_id spf_core_device_id[]  = {
	{ .compatible = "qcom,spf_core" },
	{},
};
MODULE_DEVICE_TABLE(of, spf_core_device_id);

static gpr_driver_t ar_spf_core_driver = {
	.probe = spf_core_probe,
	.remove = spf_core_exit,
	.gpr_callback = spf_core_callback,
	.driver = {
		.name = "qcom-spf_core",
		.of_match_table = of_match_ptr(spf_core_device_id),
	},
};

static void spf_core_add_child_devices(struct work_struct *work)
{
	int ret;

	if (spf_core_is_apm_ready()) {
		dev_err(spf_core_priv->dev, "%s: apm is up\n",
			__func__);
	} else {
		dev_err(spf_core_priv->dev, "%s: apm is not up\n",
			__func__);
		return;
	}

	ret = of_platform_populate(spf_core_priv->dev->of_node,
			NULL, NULL, spf_core_priv->dev);
	if (ret)
		dev_err(spf_core_priv->dev, "%s: failed to add child nodes, ret=%d\n",
			__func__, ret);

	spf_core_priv->is_initial_boot = false;
}

static int spf_core_platform_driver_probe(struct platform_device *pdev)
{
	int ret = 0;

	spf_core_priv = devm_kzalloc(&pdev->dev, sizeof(struct spf_core_private), GFP_KERNEL);
	if (!spf_core_priv)
		return -ENOMEM;

	spf_core_priv->dev = &pdev->dev;

	mutex_init(&spf_core_priv->lock);

	INIT_WORK(&spf_core_priv->add_chld_dev_work, spf_core_add_child_devices);

	spf_core_priv->is_initial_boot = true;
	ret = apr_driver_register(&ar_spf_core_driver);
	if (ret) {
		pr_err("%s: gpr driver register failed = %d\n",
			__func__, ret);
		ret = 0;
	}

	return ret;
}

static int spf_core_platform_driver_remove(struct platform_device *pdev)
{
	apr_driver_unregister(&ar_spf_core_driver);
	spf_core_priv = NULL;
	return 0;
}

static const struct of_device_id spf_core_of_match[]  = {
	{ .compatible = "qcom,spf-core-platform", },
	{},
};

static struct platform_driver spf_core_driver = {
	.probe = spf_core_platform_driver_probe,
	.remove = spf_core_platform_driver_remove,
	.driver = {
		.name = "spf-core-platform",
		.of_match_table = spf_core_of_match,
	}
};

module_platform_driver(spf_core_driver);

MODULE_DESCRIPTION("qcom spf core");
MODULE_LICENSE("GPL");
