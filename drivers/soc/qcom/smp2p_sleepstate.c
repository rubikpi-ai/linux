// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>

#define PROC_AWAKE_ID 12 /* 12th bit */
#define AWAKE_BIT BIT(PROC_AWAKE_ID)
static struct wakeup_source *notify_ws;

struct smp2p_state {
	struct qcom_smem_state *state;
	struct notifier_block nb;
};

/**
 * sleepstate_pm_notifier() - PM notifier callback function.
 * @nb:		Pointer to the notifier block.
 * @event:	Suspend state event from PM module.
 * @unused:	Null pointer from PM module.
 *
 * This function is register as callback function to get notifications
 * from the PM module on the system suspend state.
 */
static int sleepstate_pm_notifier(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	struct smp2p_state *smp2p_info = container_of(nb, struct smp2p_state, nb);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		qcom_smem_state_update_bits(smp2p_info->state, AWAKE_BIT, 0);
		break;

	case PM_POST_SUSPEND:
		qcom_smem_state_update_bits(smp2p_info->state, AWAKE_BIT, AWAKE_BIT);
		break;
	}

	return NOTIFY_DONE;
}

static irqreturn_t smp2p_sleepstate_handler(int irq, void *ctxt)
{
	__pm_wakeup_event(notify_ws, 200);
	return IRQ_HANDLED;
}

static int smp2p_sleepstate_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct smp2p_state *smp2p_info;

	smp2p_info = devm_kzalloc(dev, sizeof(*smp2p_info), GFP_KERNEL);
	if (!smp2p_info)
		return PTR_ERR(smp2p_info);

	smp2p_info->state = qcom_smem_state_get(&pdev->dev, NULL, &ret);
	if (IS_ERR(smp2p_info->state))
		return PTR_ERR(smp2p_info->state);
	qcom_smem_state_update_bits(smp2p_info->state, AWAKE_BIT, AWAKE_BIT);

	smp2p_info->nb.notifier_call = sleepstate_pm_notifier;
	smp2p_info->nb.priority = INT_MAX;

	ret = register_pm_notifier(&smp2p_info->nb);
	if (ret) {
		dev_err(dev, "%s: power state notif error %d\n", __func__, ret);
		return ret;
	}

	notify_ws = wakeup_source_register(&pdev->dev, "smp2p-sleepstate");
	if (!notify_ws) {
		return -ENOMEM;
		goto err_ws;
	}

	irq = of_irq_get_byname(node, "smp2p-sleepstate-in");
	if (irq <= 0) {
		dev_err(dev, "failed to get irq for smp2p_sleep_state\n");
		ret = -EPROBE_DEFER;
		goto err;
	}
	dev_dbg(dev, "got smp2p-sleepstate-in irq %d\n", irq);
	ret = devm_request_threaded_irq(dev, irq, NULL,
					smp2p_sleepstate_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"smp2p_sleepstate", dev);
	if (ret) {
		dev_err(dev, "fail to register smp2p threaded_irq=%d\n", irq);
		goto err;
	}
	return 0;
err:
	wakeup_source_unregister(notify_ws);
err_ws:
	unregister_pm_notifier(&smp2p_info->nb);
	return ret;
}

static const struct of_device_id smp2p_slst_match_table[] = {
	{.compatible = "qcom,smp2p-sleepstate"},
	{},
};

static struct platform_driver smp2p_sleepstate_driver = {
	.probe = smp2p_sleepstate_probe,
	.driver = {
		.name = "smp2p_sleepstate",
		.of_match_table = smp2p_slst_match_table,
	},
};

static int __init smp2p_sleepstate_init(void)
{
	int ret;

	ret = platform_driver_register(&smp2p_sleepstate_driver);
	if (ret) {
		pr_err("%s: register failed %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

module_init(smp2p_sleepstate_init);
MODULE_DESCRIPTION("SMP2P SLEEP STATE");
MODULE_LICENSE("GPL");
