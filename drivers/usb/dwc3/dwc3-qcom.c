// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Inspired by dwc3-of-simple.c
 */

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/extcon.h>
#include <linux/interconnect.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/of.h>
#include <linux/reset.h>
#include <linux/suspend.h>
#include <linux/iopoll.h>
#include <linux/usb/hcd.h>
#include <linux/usb.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "core.h"
#include "io.h"

/* USB QSCRATCH Hardware registers */
#define QSCRATCH_HS_PHY_CTRL			0x10
#define UTMI_OTG_VBUS_VALID			BIT(20)
#define SW_SESSVLD_SEL				BIT(28)

#define QSCRATCH_SS_PHY_CTRL			0x30
#define LANE0_PWR_PRESENT			BIT(24)

#define QSCRATCH_GENERAL_CFG			0x08
#define PIPE_UTMI_CLK_SEL			BIT(0)
#define PIPE3_PHYSTATUS_SW			BIT(3)
#define PIPE_UTMI_CLK_DIS			BIT(8)

#define PWR_EVNT_IRQ_STAT_REG			0x58
#define PWR_EVNT_LPM_IN_L2_MASK			BIT(4)
#define PWR_EVNT_LPM_OUT_L2_MASK		BIT(5)

#define SDM845_QSCRATCH_BASE_OFFSET		0xf8800
#define SDM845_QSCRATCH_SIZE			0x400
#define SDM845_DWC3_CORE_SIZE			0xcd00

/* Interconnect path bandwidths in MBps */
#define USB_MEMORY_AVG_HS_BW MBps_to_icc(240)
#define USB_MEMORY_PEAK_HS_BW MBps_to_icc(700)
#define USB_MEMORY_AVG_SS_BW  MBps_to_icc(1000)
#define USB_MEMORY_PEAK_SS_BW MBps_to_icc(2500)
#define APPS_USB_AVG_BW 0
#define APPS_USB_PEAK_BW MBps_to_icc(40)

#define dwc3_to_qcom(dwc)	container_of(dwc, struct dwc3 *, dwc)

struct dwc3_acpi_pdata {
	int			hs_phy_irq_index;
	int			dp_hs_phy_irq_index;
	int			dm_hs_phy_irq_index;
	int			ss_phy_irq_index;
	bool			is_urs;
};

struct dwc3_qcom {
	struct device		*dev;
	void __iomem		*qscratch_base;
	struct platform_device	*dwc_dev; /* only used when core is separate device */
	struct dwc3		dwc; /* not used when core is separate device */
	struct clk		**clks;
	int			num_clocks;
	struct reset_control	*resets;

	int			hs_phy_irq;
	int			dp_hs_phy_irq;
	int			dm_hs_phy_irq;
	int			ss_phy_irq;
	enum usb_device_speed	usb2_speed;

	struct extcon_dev	*edev;
	struct extcon_dev	*host_edev;
	struct notifier_block	vbus_nb;
	struct notifier_block	host_nb;

	const struct dwc3_acpi_pdata *acpi_pdata;

	enum usb_dr_mode	mode;
	bool			is_suspended;
	bool			pm_suspended;
	struct icc_path		*icc_path_ddr;
	struct icc_path		*icc_path_apps;

	bool			enable_rt;
	enum usb_role		current_role;
	struct notifier_block	xhci_nb;
	struct regulator *vdda;

	struct notifier_block	pm_notifier;
};

static bool enable_usb_sleep = false;

static inline void dwc3_qcom_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static inline void dwc3_qcom_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static void dwc3_qcom_vbus_override_enable(struct dwc3_qcom *qcom, bool enable)
{
	if (enable) {
		dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_SS_PHY_CTRL,
				  LANE0_PWR_PRESENT);
		dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_HS_PHY_CTRL,
				  UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL);
	} else {
		dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_SS_PHY_CTRL,
				  LANE0_PWR_PRESENT);
		dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_HS_PHY_CTRL,
				  UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL);
	}
}

static int dwc3_qcom_vbus_notifier(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct dwc3_qcom *qcom = container_of(nb, struct dwc3_qcom, vbus_nb);

	/* enable vbus override for device mode */
	dwc3_qcom_vbus_override_enable(qcom, event);
	qcom->mode = event ? USB_DR_MODE_PERIPHERAL : USB_DR_MODE_HOST;

	return NOTIFY_DONE;
}

static int dwc3_qcom_host_notifier(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct dwc3_qcom *qcom = container_of(nb, struct dwc3_qcom, host_nb);

	/* disable vbus override in host mode */
	dwc3_qcom_vbus_override_enable(qcom, !event);
	qcom->mode = event ? USB_DR_MODE_HOST : USB_DR_MODE_PERIPHERAL;

	return NOTIFY_DONE;
}

static int dwc3_qcom_register_extcon(struct dwc3_qcom *qcom)
{
	struct device		*dev = qcom->dev;
	struct extcon_dev	*host_edev;
	int			ret;

	if (!of_property_read_bool(dev->of_node, "extcon"))
		return 0;

	qcom->edev = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(qcom->edev))
		return dev_err_probe(dev, PTR_ERR(qcom->edev),
				     "Failed to get extcon\n");

	qcom->vbus_nb.notifier_call = dwc3_qcom_vbus_notifier;

	qcom->host_edev = extcon_get_edev_by_phandle(dev, 1);
	if (IS_ERR(qcom->host_edev))
		qcom->host_edev = NULL;

	ret = devm_extcon_register_notifier(dev, qcom->edev, EXTCON_USB,
					    &qcom->vbus_nb);
	if (ret < 0) {
		dev_err(dev, "VBUS notifier register failed\n");
		return ret;
	}

	if (qcom->host_edev)
		host_edev = qcom->host_edev;
	else
		host_edev = qcom->edev;

	qcom->host_nb.notifier_call = dwc3_qcom_host_notifier;
	ret = devm_extcon_register_notifier(dev, host_edev, EXTCON_USB_HOST,
					    &qcom->host_nb);
	if (ret < 0) {
		dev_err(dev, "Host notifier register failed\n");
		return ret;
	}

	/* Update initial VBUS override based on extcon state */
	if (extcon_get_state(qcom->edev, EXTCON_USB) ||
	    !extcon_get_state(host_edev, EXTCON_USB_HOST))
		dwc3_qcom_vbus_notifier(&qcom->vbus_nb, true, qcom->edev);
	else
		dwc3_qcom_vbus_notifier(&qcom->vbus_nb, false, qcom->edev);

	return 0;
}

static int dwc3_qcom_interconnect_enable(struct dwc3_qcom *qcom)
{
	int ret;

	ret = icc_enable(qcom->icc_path_ddr);
	if (ret)
		return ret;

	ret = icc_enable(qcom->icc_path_apps);
	if (ret)
		icc_disable(qcom->icc_path_ddr);

	return ret;
}

static int dwc3_qcom_interconnect_disable(struct dwc3_qcom *qcom)
{
	int ret;

	ret = icc_disable(qcom->icc_path_ddr);
	if (ret)
		return ret;

	ret = icc_disable(qcom->icc_path_apps);
	if (ret)
		icc_enable(qcom->icc_path_ddr);

	return ret;
}

static bool dwc3_qcom_has_separate_dwc3_of_node(struct device *dev)
{
	struct device_node *np;

	np = of_get_compatible_child(dev->of_node, "snps,dwc3");
	of_node_put(np);

	return !!np;
}

/**
 * dwc3_qcom_interconnect_init() - Get interconnect path handles
 * and set bandwidth.
 * @qcom:			Pointer to the concerned usb core.
 *
 */
static int dwc3_qcom_interconnect_init(struct dwc3_qcom *qcom)
{
	enum usb_device_speed max_speed;
	struct device *dev = qcom->dev;
	bool legacy_binding;
	int ret;

	if (has_acpi_companion(dev))
		return 0;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(qcom->dev);

	qcom->icc_path_ddr = of_icc_get(dev, "usb-ddr");
	if (IS_ERR(qcom->icc_path_ddr)) {
		return dev_err_probe(dev, PTR_ERR(qcom->icc_path_ddr),
				     "failed to get usb-ddr path\n");
	}

	qcom->icc_path_apps = of_icc_get(dev, "apps-usb");
	if (IS_ERR(qcom->icc_path_apps)) {
		ret = dev_err_probe(dev, PTR_ERR(qcom->icc_path_apps),
				    "failed to get apps-usb path\n");
		goto put_path_ddr;
	}

	if (legacy_binding)
		max_speed = usb_get_maximum_speed(&qcom->dwc_dev->dev);
	else
		max_speed = usb_get_maximum_speed(qcom->dev);

	if (max_speed >= USB_SPEED_SUPER || max_speed == USB_SPEED_UNKNOWN) {
		ret = icc_set_bw(qcom->icc_path_ddr,
				USB_MEMORY_AVG_SS_BW, USB_MEMORY_PEAK_SS_BW);
	} else {
		ret = icc_set_bw(qcom->icc_path_ddr,
				USB_MEMORY_AVG_HS_BW, USB_MEMORY_PEAK_HS_BW);
	}
	if (ret) {
		dev_err(dev, "failed to set bandwidth for usb-ddr path: %d\n", ret);
		goto put_path_apps;
	}

	ret = icc_set_bw(qcom->icc_path_apps, APPS_USB_AVG_BW, APPS_USB_PEAK_BW);
	if (ret) {
		dev_err(dev, "failed to set bandwidth for apps-usb path: %d\n", ret);
		goto put_path_apps;
	}

	return 0;

put_path_apps:
	icc_put(qcom->icc_path_apps);
put_path_ddr:
	icc_put(qcom->icc_path_ddr);
	return ret;
}

/**
 * dwc3_qcom_interconnect_exit() - Release interconnect path handles
 * @qcom:			Pointer to the concerned usb core.
 *
 * This function is used to release interconnect path handle.
 */
static void dwc3_qcom_interconnect_exit(struct dwc3_qcom *qcom)
{
	icc_put(qcom->icc_path_ddr);
	icc_put(qcom->icc_path_apps);
}

/* Only usable in contexts where the role can not change. */
static bool dwc3_qcom_is_host(struct dwc3_qcom *qcom)
{
	bool legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(qcom->dev);
	struct dwc3 *dwc;

	/*
	 * FIXME: Fix this layering violation.
	 */
	if (legacy_binding)
		dwc = platform_get_drvdata(qcom->dwc_dev);
	else
		dwc = &qcom->dwc;

	/* Core driver may not have probed yet. */
	if (!dwc)
		return false;

	return dwc->xhci;
}

static enum usb_device_speed dwc3_qcom_read_usb2_speed(struct dwc3_qcom *qcom)
{
	bool legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(qcom->dev);
	struct usb_hcd __maybe_unused *hcd;
	struct usb_device *udev;
	struct dwc3 *dwc;

	if (legacy_binding)
		dwc = platform_get_drvdata(qcom->dwc_dev);
	else
		dwc = &qcom->dwc;

	/*
	 * FIXME: Fix this layering violation.
	 */
	hcd = platform_get_drvdata(dwc->xhci);

	/*
	 * It is possible to query the speed of all children of
	 * USB2.0 root hub via usb_hub_for_each_child(). DWC3 code
	 * currently supports only 1 port per controller. So
	 * this is sufficient.
	 */
#ifdef CONFIG_USB
	udev = usb_hub_find_child(hcd->self.root_hub, 1);
#else
	udev = NULL;
#endif
	if (!udev)
		return USB_SPEED_UNKNOWN;

	return udev->speed;
}

static void dwc3_qcom_enable_wakeup_irq(int irq, unsigned int polarity)
{
	if (!irq)
		return;

	if (polarity)
		irq_set_irq_type(irq, polarity);

	enable_irq(irq);
	enable_irq_wake(irq);
}

static void dwc3_qcom_disable_wakeup_irq(int irq)
{
	if (!irq)
		return;

	disable_irq_wake(irq);
	disable_irq_nosync(irq);
}

static void dwc3_qcom_disable_interrupts(struct dwc3_qcom *qcom)
{
	dwc3_qcom_disable_wakeup_irq(qcom->hs_phy_irq);

	if (qcom->usb2_speed == USB_SPEED_LOW) {
		dwc3_qcom_disable_wakeup_irq(qcom->dm_hs_phy_irq);
	} else if ((qcom->usb2_speed == USB_SPEED_HIGH) ||
			(qcom->usb2_speed == USB_SPEED_FULL)) {
		dwc3_qcom_disable_wakeup_irq(qcom->dp_hs_phy_irq);
	} else {
		dwc3_qcom_disable_wakeup_irq(qcom->dp_hs_phy_irq);
		dwc3_qcom_disable_wakeup_irq(qcom->dm_hs_phy_irq);
	}

	dwc3_qcom_disable_wakeup_irq(qcom->ss_phy_irq);
}

static void dwc3_qcom_enable_interrupts(struct dwc3_qcom *qcom)
{
	dwc3_qcom_enable_wakeup_irq(qcom->hs_phy_irq, 0);

	/*
	 * Configure DP/DM line interrupts based on the USB2 device attached to
	 * the root hub port. When HS/FS device is connected, configure the DP line
	 * as falling edge to detect both disconnect and remote wakeup scenarios. When
	 * LS device is connected, configure DM line as falling edge to detect both
	 * disconnect and remote wakeup. When no device is connected, configure both
	 * DP and DM lines as rising edge to detect HS/HS/LS device connect scenario.
	 */

	if (qcom->usb2_speed == USB_SPEED_LOW) {
		dwc3_qcom_enable_wakeup_irq(qcom->dm_hs_phy_irq,
						IRQ_TYPE_EDGE_FALLING);
	} else if ((qcom->usb2_speed == USB_SPEED_HIGH) ||
			(qcom->usb2_speed == USB_SPEED_FULL)) {
		dwc3_qcom_enable_wakeup_irq(qcom->dp_hs_phy_irq,
						IRQ_TYPE_EDGE_FALLING);
	} else {
		dwc3_qcom_enable_wakeup_irq(qcom->dp_hs_phy_irq,
						IRQ_TYPE_EDGE_RISING);
		dwc3_qcom_enable_wakeup_irq(qcom->dm_hs_phy_irq,
						IRQ_TYPE_EDGE_RISING);
	}

	dwc3_qcom_enable_wakeup_irq(qcom->ss_phy_irq, 0);
}

static int dwc3_qcom_suspend(struct dwc3_qcom *qcom, bool wakeup)
{
	u32 val;
	int i, ret;

	if (qcom->is_suspended)
		return 0;

	val = readl(qcom->qscratch_base + PWR_EVNT_IRQ_STAT_REG);
	if (!(val & PWR_EVNT_LPM_IN_L2_MASK))
		dev_err(qcom->dev, "HS-PHY not in L2\n");

	for (i = qcom->num_clocks - 1; i >= 0; i--)
		clk_disable_unprepare(qcom->clks[i]);

	ret = dwc3_qcom_interconnect_disable(qcom);
	if (ret)
		dev_warn(qcom->dev, "failed to disable interconnect: %d\n", ret);

	/*
	 * The role is stable during suspend as role switching is done from a
	 * freezable workqueue.
	 */
	if (dwc3_qcom_is_host(qcom) && wakeup) {
		qcom->usb2_speed = dwc3_qcom_read_usb2_speed(qcom);
		dwc3_qcom_enable_interrupts(qcom);
	}

	qcom->is_suspended = true;
	pm_relax(qcom->dev);

	return 0;
}

static int dwc3_qcom_resume(struct dwc3_qcom *qcom, bool wakeup)
{
	bool legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(qcom->dev);
	int ret;
	int i;

	if (!qcom->is_suspended)
		return 0;

	pm_stay_awake(qcom->dev);

	if (!legacy_binding) {
		ret = reset_control_deassert(qcom->dwc.reset);
		if (ret)
			return ret;
	}

	if (dwc3_qcom_is_host(qcom) && wakeup)
		dwc3_qcom_disable_interrupts(qcom);

	for (i = 0; i < qcom->num_clocks; i++) {
		ret = clk_prepare_enable(qcom->clks[i]);
		if (ret < 0) {
			while (--i >= 0)
				clk_disable_unprepare(qcom->clks[i]);
			return ret;
		}
	}

	ret = dwc3_qcom_interconnect_enable(qcom);
	if (ret)
		dev_warn(qcom->dev, "failed to enable interconnect: %d\n", ret);

	/* Clear existing events from PHY related to L2 in/out */
	dwc3_qcom_setbits(qcom->qscratch_base, PWR_EVNT_IRQ_STAT_REG,
			  PWR_EVNT_LPM_IN_L2_MASK | PWR_EVNT_LPM_OUT_L2_MASK);

	qcom->is_suspended = false;

	return 0;
}

static irqreturn_t qcom_dwc3_resume_irq(int irq, void *data)
{
	struct dwc3_qcom *qcom = data;
	bool legacy_binding;
	struct dwc3 *dwc;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(qcom->dev);

	/* If pm_suspended then let pm_resume take care of resuming h/w */
	if (qcom->pm_suspended)
		return IRQ_HANDLED;

	if (legacy_binding)
		dwc = platform_get_drvdata(qcom->dwc_dev);
	else
		dwc = &qcom->dwc;

	/*
	 * This is safe as role switching is done from a freezable workqueue
	 * and the wakeup interrupts are disabled as part of resume.
	 */
	if (dwc3_qcom_is_host(qcom))
		pm_runtime_resume(&dwc->xhci->dev);

	return IRQ_HANDLED;
}

static void dwc3_qcom_select_utmi_clk(struct dwc3_qcom *qcom)
{
	/* Configure dwc3 to use UTMI clock as PIPE clock not present */
	dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_DIS);

	usleep_range(100, 1000);

	dwc3_qcom_setbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_SEL | PIPE3_PHYSTATUS_SW);

	usleep_range(100, 1000);

	dwc3_qcom_clrbits(qcom->qscratch_base, QSCRATCH_GENERAL_CFG,
			  PIPE_UTMI_CLK_DIS);
}

static int dwc3_qcom_get_irq(struct platform_device *pdev,
			     const char *name, int num)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (np)
		ret = platform_get_irq_byname_optional(pdev, name);
	else
		ret = platform_get_irq_optional(pdev, num);

	return ret;
}

static struct dwc3_qcom *get_dwc3_qcom(struct device *dev)
{
	bool legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	struct dwc3_qcom *qcom;
	struct dwc3 *dwc;

	if (!legacy_binding) {
		dwc = dev_get_drvdata(dev);
		qcom = container_of(dwc, struct dwc3_qcom, dwc);
	} else {
		qcom = dev_get_drvdata(dev);
	}

	return qcom;
}

static int dwc3_qcom_setup_irq(struct platform_device *pdev)
{
	const struct dwc3_acpi_pdata *pdata;
	struct dwc3_qcom *qcom;
	int irq;
	int ret;

	qcom = get_dwc3_qcom(&pdev->dev);
	pdata = qcom->acpi_pdata;

	irq = dwc3_qcom_get_irq(pdev, "hs_phy_irq",
				pdata ? pdata->hs_phy_irq_index : -1);
	if (irq > 0) {
		/* Keep wakeup interrupts disabled until suspend */
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_ONESHOT,
					"qcom_dwc3 HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "dp_hs_phy_irq",
				pdata ? pdata->dp_hs_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_ONESHOT,
					"qcom_dwc3 DP_HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "dp_hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->dp_hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "dm_hs_phy_irq",
				pdata ? pdata->dm_hs_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_ONESHOT,
					"qcom_dwc3 DM_HS", qcom);
		if (ret) {
			dev_err(qcom->dev, "dm_hs_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->dm_hs_phy_irq = irq;
	}

	irq = dwc3_qcom_get_irq(pdev, "ss_phy_irq",
				pdata ? pdata->ss_phy_irq_index : -1);
	if (irq > 0) {
		irq_set_status_flags(irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(qcom->dev, irq, NULL,
					qcom_dwc3_resume_irq,
					IRQF_ONESHOT,
					"qcom_dwc3 SS", qcom);
		if (ret) {
			dev_err(qcom->dev, "ss_phy_irq failed: %d\n", ret);
			return ret;
		}
		qcom->ss_phy_irq = irq;
	}

	return 0;
}

static int dwc3_qcom_clk_init(struct dwc3_qcom *qcom, int count)
{
	struct device		*dev = qcom->dev;
	struct device_node	*np = dev->of_node;
	int			i;

	if (!np || !count)
		return 0;

	if (count < 0)
		return count;

	qcom->num_clocks = count;

	qcom->clks = devm_kcalloc(dev, qcom->num_clocks,
				  sizeof(struct clk *), GFP_KERNEL);
	if (!qcom->clks)
		return -ENOMEM;

	for (i = 0; i < qcom->num_clocks; i++) {
		struct clk	*clk;
		int		ret;

		clk = of_clk_get(np, i);
		if (IS_ERR(clk)) {
			while (--i >= 0)
				clk_put(qcom->clks[i]);
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			while (--i >= 0) {
				clk_disable_unprepare(qcom->clks[i]);
				clk_put(qcom->clks[i]);
			}
			clk_put(clk);

			return ret;
		}

		qcom->clks[i] = clk;
	}

	return 0;
}

static const struct property_entry dwc3_qcom_acpi_properties[] = {
	PROPERTY_ENTRY_STRING("dr_mode", "host"),
	{}
};

static const struct software_node dwc3_qcom_swnode = {
	.properties = dwc3_qcom_acpi_properties,
};

static int dwc3_xhci_event_notifier(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct usb_device *udev = ptr;

	if (event != USB_DEVICE_ADD)
		return NOTIFY_DONE;

	/*
	 * If this is a roothub corresponding to this controller, enable autosuspend
	 */
	if (!udev->parent) {
		pm_runtime_use_autosuspend(&udev->dev);
		pm_runtime_set_autosuspend_delay(&udev->dev, 1000);
	}

	usb_mark_last_busy(udev);

	return NOTIFY_DONE;
}

static int dwc3_qcom_handle_cable_disconnect(void *data)
{
	struct dwc3_qcom *qcom = (struct dwc3_qcom *)data;
	int ret = 0;

	/*
	 * If we are in device mode and get a cable disconnect,
	 * handle it by clearing OTG_VBUS_VALID bit in wrapper.
	 * The next set_mode to default role can be ignored.
	 */
	if (qcom->current_role == USB_ROLE_DEVICE) {
		ret = pm_runtime_get_sync(qcom->dev);
		if ((ret < 0) || (qcom->is_suspended))
			return ret;
		dwc3_qcom_vbus_override_enable(qcom, false);
		pm_runtime_put_autosuspend(qcom->dev);
	} else if (qcom->current_role == USB_ROLE_HOST) {
		usb_unregister_notify(&qcom->xhci_nb);
	}

	pm_runtime_mark_last_busy(qcom->dev);
	qcom->current_role = USB_ROLE_NONE;

	return 0;
}

static int dwc3_qcom_pm_callback(struct notifier_block *nb, unsigned long action,
			void *data)
{
	bool suspend_sts = FALSE;
	struct dwc3_qcom *qcom = container_of(nb, struct dwc3_qcom, pm_notifier);
	int ret = NOTIFY_DONE;

	switch (action) {
		case PM_HIBERNATION_PREPARE:
		case PM_SUSPEND_PREPARE:
			suspend_sts = TRUE;
			break;

		case PM_POST_HIBERNATION:
		case PM_POST_SUSPEND:
			suspend_sts = FALSE;
			break;
	}

	if (suspend_sts) {
		if (!strncmp(dev_name(qcom->dwc.dev), "a600000.usb", 11)) {
			if ((!qcom->dwc.cable_disconnected) && (!enable_usb_sleep)) {
				dev_err(qcom->dev, "Abort PM suspend!! (USB is outside LPM)\n");
				ret = NOTIFY_BAD;
			}
		}
	}

	return ret;
}

static void dwc3_qcom_handle_set_mode(void *data, u32 desired_dr_role)
{
	struct dwc3_qcom *qcom = (struct dwc3_qcom *)data;

	/*
	 * If we are in device mode and get a cable disconnect,
	 * handle it by clearing OTG_VBUS_VALID bit in wrapper.
	 * The next set_mode to default role can be ignored and
	 * so the OTG_VBUS_VALID should be set iff the current role
	 * is NONE and we need to enter DEVICE mode.
	 */
	if ((qcom->current_role == USB_ROLE_NONE) &&
	    (desired_dr_role == DWC3_GCTL_PRTCAP_DEVICE)) {
		dwc3_qcom_vbus_override_enable(qcom, true);
		qcom->current_role = USB_ROLE_DEVICE;
	} else if ((desired_dr_role == DWC3_GCTL_PRTCAP_HOST) &&
		   (qcom->current_role != USB_ROLE_HOST)) {
		qcom->xhci_nb.notifier_call = dwc3_xhci_event_notifier;
		usb_register_notify(&qcom->xhci_nb);
		qcom->current_role = USB_ROLE_HOST;
	}

	pm_runtime_mark_last_busy(qcom->dev);
}

static void dwc3_qcom_handle_mode_changed(void *data, u32 current_dr_role)
{
	struct dwc3_qcom *qcom = (struct dwc3_qcom *)data;

	/*
	 * XHCI platform device is allocated upon host init.
	 * So ensure we are in host mode before enabling autosuspend.
	 */
	if ((current_dr_role == DWC3_GCTL_PRTCAP_HOST) &&
	    (qcom->current_role == USB_ROLE_HOST)) {
		pm_runtime_use_autosuspend(&qcom->dwc.xhci->dev);
		pm_runtime_set_autosuspend_delay(&qcom->dwc.xhci->dev, 0);
	}
}

static void dwc3_post_conndone_notification(void *data)
{
	struct dwc3_qcom *qcom = (struct dwc3_qcom *)data;

	qcom->dwc.cable_disconnected = false;
	qcom->current_role = USB_ROLE_DEVICE;
}

static void dwc3_qcom_handle_run_stop_notification(void *data, bool enable)
{
	struct dwc3_qcom *qcom = (struct dwc3_qcom *)data;

	if (enable)
		dwc3_qcom_vbus_override_enable(qcom, true);
}

struct dwc3_glue_ops dwc3_qcom_glue_hooks = {
	.notify_cable_disconnect = dwc3_qcom_handle_cable_disconnect,
	.set_mode = dwc3_qcom_handle_set_mode,
	.mode_changed = dwc3_qcom_handle_mode_changed,
	.notify_run_stop = dwc3_qcom_handle_run_stop_notification,
	.post_conndone = dwc3_post_conndone_notification,
};

static int dwc3_qcom_probe_core(struct platform_device *pdev, struct dwc3_qcom *qcom)
{
	int ret;

	struct dwc3_glue_data qcom_glue_data = {
		.glue_data	= qcom,
		.ops		= &dwc3_qcom_glue_hooks,
	};

	ret = dwc3_probe(&qcom->dwc,
			 qcom->enable_rt ? &qcom_glue_data : NULL);
	if (ret)
		return ret;

	return 0;
}

static int dwc3_qcom_of_register_core(struct platform_device *pdev)
{
	struct dwc3_qcom	*qcom = platform_get_drvdata(pdev);
	struct device_node	*np = pdev->dev.of_node, *dwc3_np;
	struct device		*dev = &pdev->dev;
	int			ret;

	dwc3_np = of_get_compatible_child(np, "snps,dwc3");
	if (!dwc3_np) {
		dev_err(dev, "failed to find dwc3 core child\n");
		return -ENODEV;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to register dwc3 core - %d\n", ret);
		goto node_put;
	}

	qcom->dwc_dev = of_find_device_by_node(dwc3_np);
	if (!qcom->dwc_dev) {
		ret = -ENODEV;
		dev_err(dev, "failed to get dwc3 platform device\n");
		of_platform_depopulate(dev);
	}

node_put:
	of_node_put(dwc3_np);

	return ret;
}

static int dwc3_qcom_acpi_merge_urs_resources(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct list_head resource_list;
	struct resource_entry *rentry;
	struct resource *resources;
	struct fwnode_handle *fwh;
	struct acpi_device *adev;
	char name[8];
	int count;
	int ret;
	int id;
	int i;

	/* Figure out device id */
	ret = sscanf(fwnode_get_name(dev->fwnode), "URS%d", &id);
	if (!ret)
		return -EINVAL;

	/* Find the child using name */
	snprintf(name, sizeof(name), "USB%d", id);
	fwh = fwnode_get_named_child_node(dev->fwnode, name);
	if (!fwh)
		return 0;

	adev = to_acpi_device_node(fwh);
	if (!adev)
		return -EINVAL;

	INIT_LIST_HEAD(&resource_list);

	count = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (count <= 0)
		return count;

	count += pdev->num_resources;

	resources = kcalloc(count, sizeof(*resources), GFP_KERNEL);
	if (!resources) {
		acpi_dev_free_resource_list(&resource_list);
		return -ENOMEM;
	}

	memcpy(resources, pdev->resource, sizeof(struct resource) * pdev->num_resources);
	count = pdev->num_resources;
	list_for_each_entry(rentry, &resource_list, node) {
		/* Avoid inserting duplicate entries, in case this is called more than once */
		for (i = 0; i < count; i++) {
			if (resource_type(&resources[i]) == resource_type(rentry->res) &&
			    resources[i].start == rentry->res->start &&
			    resources[i].end == rentry->res->end)
				break;
		}

		if (i == count)
			resources[count++] = *rentry->res;
	}

	ret = platform_device_add_resources(pdev, resources, count);
	if (ret)
		dev_err(&pdev->dev, "failed to add resources\n");

	acpi_dev_free_resource_list(&resource_list);
	kfree(resources);

	return ret;
}

static int dwc3_mode_show(struct seq_file *s, void *unused)
{
	struct dwc3_qcom	*qcom = s->private;
	struct dwc3		*dwc = &qcom->dwc;
	unsigned long		flags;
	u32			reg;
	int			ret;

	ret = pm_runtime_resume_and_get(dwc->dev);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&dwc->lock, flags);
	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	spin_unlock_irqrestore(&dwc->lock, flags);

	switch (DWC3_GCTL_PRTCAP(reg)) {
	case DWC3_GCTL_PRTCAP_HOST:
		seq_puts(s, "host\n");
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		seq_puts(s, "device\n");
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		seq_puts(s, "otg\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %08x\n", DWC3_GCTL_PRTCAP(reg));
	}

	pm_runtime_put_sync(dwc->dev);

	return 0;

}

static int dwc3_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwc3_mode_show, inode->i_private);
}

static ssize_t dwc3_qcom_usb2_0_mode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct dwc3_qcom	*qcom = s->private;
	struct dwc3		*dwc = &qcom->dwc;
	u32			mode = 0;
	char			buf[32];
	int			ret;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (dwc->dr_mode != USB_DR_MODE_OTG)
		return count;

	if (!strncmp(buf, "host", 4)) {
		ret = regulator_enable(qcom->vdda);
		if (ret)
			dev_err(qcom->dev, "cannot enable vdda regulator\n");
		mode = DWC3_GCTL_PRTCAP_HOST;
	}

	if (!strncmp(buf, "device", 6)) {
		ret = regulator_disable(qcom->vdda);
		if (ret)
			dev_err(qcom->dev, "cannot disable vdda regulator\n");
		mode = DWC3_GCTL_PRTCAP_DEVICE;
	}

	dwc3_set_mode(dwc, mode);

	return count;
}

static const struct file_operations dwc3_qcom_usb2_0_mode_fops = {
	.open			= dwc3_mode_open,
	.write			= dwc3_qcom_usb2_0_mode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static ssize_t dwc3_usb_sleep_enbale_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);

	return scnprintf(buf, 2, "%d\n", (enable_usb_sleep)? 1 : 0);
};

static ssize_t dwc3_usb_sleep_enbale_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct dwc3_qcom *qcom = dev_get_drvdata(dev);
	int ret;
	bool val;

	if (!qcom) {
		pr_err("qcom is NULL\n");
		return -EINVAL;
	}

	ret = kstrtobool(buf, &val);
	if (ret) {
		pr_err("Parse buf failed keep previous val\n");
		return count;
	}

	if (val) {
		pr_err("Enable Type-C connection to enter sleep mode\n");
		enable_usb_sleep = true;
	} else {
		pr_err("Disable Type-C connection to enter sleep mode\n");
		enable_usb_sleep = false;
	}

	return count;

}

static DEVICE_ATTR_RW(dwc3_usb_sleep_enbale);

static struct attribute *dwc3_attrs[] = {
	&dev_attr_dwc3_usb_sleep_enbale.attr,
	NULL,
};

static const struct attribute_group dwc3_attr_group = {
	.attrs = dwc3_attrs,
};

static const struct attribute_group *dwc3_attr_groups[] = {
	&dwc3_attr_group,
	NULL,
};

static int dwc3_qcom_probe(struct platform_device *pdev)
{
	struct device_node	*np = pdev->dev.of_node;
	struct device		*dev = &pdev->dev;
	struct dwc3_qcom	*qcom;
	struct resource		*res, *parent_res = NULL;
	struct resource		local_res;
	int			ret, i;
	bool			ignore_pipe_clk;
	bool			wakeup_source;
	bool			legacy_binding;

	qcom = devm_kzalloc(&pdev->dev, sizeof(*qcom), GFP_KERNEL);
	if (!qcom)
		return -ENOMEM;

	qcom->vdda = devm_regulator_get(&pdev->dev, "vdda");
	ret = regulator_enable(qcom->vdda);
	if (ret)
		dev_err(&pdev->dev, "cannot enable vdda regulator\n");

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);

	if (!legacy_binding)
		platform_set_drvdata(pdev, &qcom->dwc);
	else
		platform_set_drvdata(pdev, qcom);

	qcom->dev = &pdev->dev;
	qcom->dwc.dev = qcom->dev;

	if (has_acpi_companion(dev)) {
		qcom->acpi_pdata = acpi_device_get_match_data(dev);
		if (!qcom->acpi_pdata) {
			dev_err(&pdev->dev, "no supporting ACPI device data\n");
			return -EINVAL;
		}

		ret = device_add_software_node(&pdev->dev, &dwc3_qcom_swnode);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to add properties\n");
			return ret;
		}

		if (qcom->acpi_pdata->is_urs) {
			ret = dwc3_qcom_acpi_merge_urs_resources(pdev);
			if (ret < 0)
				goto clk_disable;
		}
	}

	if (legacy_binding) {
		qcom->resets = devm_reset_control_array_get_optional_exclusive(dev);
		if (IS_ERR(qcom->resets)) {
			return dev_err_probe(&pdev->dev, PTR_ERR(qcom->resets),
					     "failed to get resets\n");
		}

		ret = reset_control_assert(qcom->resets);
		if (ret) {
			dev_err(&pdev->dev, "failed to assert resets, err=%d\n", ret);
			return ret;
		}

		usleep_range(10, 1000);

		ret = reset_control_deassert(qcom->resets);
		if (ret) {
			dev_err(&pdev->dev, "failed to deassert resets, err=%d\n", ret);
			goto reset_assert;
		}
	}

	ret = dwc3_qcom_clk_init(qcom, of_clk_get_parent_count(np));
	if (ret) {
		dev_err_probe(dev, ret, "failed to get clocks\n");
		goto reset_assert;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (legacy_binding) {
		parent_res = res;
	} else {
		memcpy(&local_res, res, sizeof(struct resource));
		parent_res = &local_res;

		parent_res->start = res->start + SDM845_QSCRATCH_BASE_OFFSET;
		parent_res->end = parent_res->start + SDM845_QSCRATCH_SIZE;
	}

	qcom->qscratch_base = devm_ioremap_resource(dev, parent_res);
	if (IS_ERR(qcom->qscratch_base)) {
		ret = PTR_ERR(qcom->qscratch_base);
		goto clk_disable;
	}

	ret = dwc3_qcom_setup_irq(pdev);
	if (ret) {
		dev_err(dev, "failed to setup IRQs, err=%d\n", ret);
		goto clk_disable;
	}

	/*
	 * Disable pipe_clk requirement if specified. Used when dwc3
	 * operates without SSPHY and only HS/FS/LS modes are supported.
	 */
	ignore_pipe_clk = device_property_read_bool(dev,
				"qcom,select-utmi-as-pipe-clk");
	if (ignore_pipe_clk)
		dwc3_qcom_select_utmi_clk(qcom);

	qcom->enable_rt = device_property_read_bool(dev,
				"qcom,enable-rt");
	if (!legacy_binding) {
		/*
		 * If we are enabling runtime, then we are using flattened
		 * device implementation.
		 */
		qcom->mode = usb_get_dr_mode(dev);

		if (qcom->mode == USB_DR_MODE_HOST)
			qcom->current_role = USB_ROLE_HOST;
		else if (qcom->mode == USB_DR_MODE_PERIPHERAL)
			qcom->current_role = USB_ROLE_DEVICE;
		else
			qcom->current_role = USB_ROLE_NONE;
	}

	if (legacy_binding)
		ret = dwc3_qcom_of_register_core(pdev);
	else
		ret = dwc3_qcom_probe_core(pdev, qcom);

	if (ret) {
		dev_err(dev, "failed to register DWC3 Core, err=%d\n", ret);
		goto depopulate;
	}

	ret = dwc3_qcom_interconnect_init(qcom);
	if (ret)
		goto depopulate;

	if (legacy_binding) {
		qcom->mode = usb_get_dr_mode(&qcom->dwc_dev->dev);

		/* enable vbus override for device mode */
		if (qcom->mode != USB_DR_MODE_HOST)
			dwc3_qcom_vbus_override_enable(qcom, true);

		/* register extcon to override sw_vbus on Vbus change later */
		ret = dwc3_qcom_register_extcon(qcom);
		if (ret)
			goto interconnect_exit;
	}

	wakeup_source = of_property_read_bool(dev->of_node, "wakeup-source");
	device_init_wakeup(&pdev->dev, wakeup_source);

	qcom->is_suspended = false;

	if (legacy_binding) {
		device_init_wakeup(&qcom->dwc_dev->dev, wakeup_source);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
		pm_runtime_forbid(dev);
	}

	/*Create a special debugging file for mode switching of USBA2.0 port on RubikPi*/
	if (!strncmp(dev_name(qcom->dwc.dev), "8c00000.usb", 11)) {
		debugfs_create_file("qcom_usb2_0_mode", 0644, qcom->dwc.debug_root, qcom,
					&dwc3_qcom_usb2_0_mode_fops);
	}

	// type c
	if (!strncmp(dev_name(qcom->dwc.dev), "a600000.usb", 11)) {
		qcom->pm_notifier.notifier_call = dwc3_qcom_pm_callback;
		qcom->pm_notifier.priority = INT_MAX;
		register_pm_notifier(&qcom->pm_notifier);
	}

	return 0;

interconnect_exit:
	dwc3_qcom_interconnect_exit(qcom);
depopulate:
	platform_set_drvdata(pdev, NULL);
	if (legacy_binding)
		of_platform_depopulate(&pdev->dev);
	else
		dwc3_remove(&qcom->dwc);
clk_disable:
	for (i = qcom->num_clocks - 1; i >= 0; i--) {
		clk_disable_unprepare(qcom->clks[i]);
		clk_put(qcom->clks[i]);
	}
reset_assert:
	reset_control_assert(qcom->resets);

	return ret;
}

static void dwc3_qcom_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct dwc3_qcom *qcom;
	bool legacy_binding;
	int i;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	if (!legacy_binding)
		dwc3_remove(&qcom->dwc);
	else
		device_remove_software_node(&qcom->dwc_dev->dev);

	if (np)
		of_platform_depopulate(&pdev->dev);
	else
		platform_device_put(pdev);

	for (i = qcom->num_clocks - 1; i >= 0; i--) {
		clk_disable_unprepare(qcom->clks[i]);
		clk_put(qcom->clks[i]);
	}
	qcom->num_clocks = 0;

	dwc3_qcom_interconnect_exit(qcom);
	reset_control_assert(qcom->resets);

	if (qcom->dwc_dev) {
		pm_runtime_allow(dev);
		pm_runtime_disable(dev);
	}
}

static int __maybe_unused dwc3_qcom_pm_suspend(struct device *dev)
{
	bool wakeup = device_may_wakeup(dev);
	struct dwc3_qcom *qcom;
	bool legacy_binding;
	int ret;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	if (!legacy_binding) {
		ret = dwc3_suspend(&qcom->dwc);
		if (ret)
			return ret;
	}

	ret = dwc3_qcom_suspend(qcom, wakeup);
	if (ret)
		return ret;

	qcom->pm_suspended = true;

	return 0;
}

static int __maybe_unused dwc3_qcom_pm_resume(struct device *dev)
{
	bool wakeup = device_may_wakeup(dev);
	struct dwc3_qcom *qcom;
	bool legacy_binding;
	int ret;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	if ((!legacy_binding) && (pm_runtime_suspended(qcom->dev))) {
		qcom->pm_suspended = false;
		return 0;
	}

	ret = dwc3_qcom_resume(qcom, wakeup);
	if (ret)
		return ret;

	qcom->pm_suspended = false;

	if (!legacy_binding) {
		ret = dwc3_resume(&qcom->dwc);
		if (ret)
			return ret;
	}

	return 0;
}

static void dwc3_qcom_complete(struct device *dev)
{
	struct dwc3_qcom *qcom;
	bool legacy_binding;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	if (!legacy_binding)
		dwc3_complete(&qcom->dwc);
}

static int __maybe_unused dwc3_qcom_runtime_suspend(struct device *dev)
{
	struct dwc3_qcom *qcom;
	bool legacy_binding;
	int ret;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	if (!legacy_binding) {
		ret = dwc3_runtime_suspend(&qcom->dwc);
		if (ret)
			return ret;
	}

	return dwc3_qcom_suspend(qcom, true);
}

static int __maybe_unused dwc3_qcom_runtime_resume(struct device *dev)
{
	struct dwc3_qcom *qcom;
	bool legacy_binding;
	int ret;

	legacy_binding = dwc3_qcom_has_separate_dwc3_of_node(dev);
	qcom = get_dwc3_qcom(dev);

	ret = dwc3_qcom_resume(qcom, true);
	if (ret)
		return ret;

	if (!legacy_binding)
		return dwc3_runtime_resume(&qcom->dwc);

	return 0;
}

static const struct dev_pm_ops dwc3_qcom_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_qcom_pm_suspend, dwc3_qcom_pm_resume)
	.complete = dwc3_qcom_complete,
	SET_RUNTIME_PM_OPS(dwc3_qcom_runtime_suspend, dwc3_qcom_runtime_resume,
			   NULL)
};

static const struct of_device_id dwc3_qcom_of_match[] = {
	{ .compatible = "qcom,dwc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, dwc3_qcom_of_match);

#ifdef CONFIG_ACPI
static const struct dwc3_acpi_pdata sdm845_acpi_pdata = {
	.hs_phy_irq_index = 1,
	.dp_hs_phy_irq_index = 4,
	.dm_hs_phy_irq_index = 3,
	.ss_phy_irq_index = 2
};

static const struct dwc3_acpi_pdata sdm845_acpi_urs_pdata = {
	.hs_phy_irq_index = 1,
	.dp_hs_phy_irq_index = 4,
	.dm_hs_phy_irq_index = 3,
	.ss_phy_irq_index = 2,
	.is_urs = true,
};

static const struct acpi_device_id dwc3_qcom_acpi_match[] = {
	{ "QCOM2430", (unsigned long)&sdm845_acpi_pdata },
	{ "QCOM0304", (unsigned long)&sdm845_acpi_urs_pdata },
	{ "QCOM0497", (unsigned long)&sdm845_acpi_urs_pdata },
	{ "QCOM04A6", (unsigned long)&sdm845_acpi_pdata },
	{ },
};
MODULE_DEVICE_TABLE(acpi, dwc3_qcom_acpi_match);
#endif

static struct platform_driver dwc3_qcom_driver = {
	.probe		= dwc3_qcom_probe,
	.remove_new	= dwc3_qcom_remove,
	.driver		= {
		.name	= "dwc3-qcom",
		.pm	= &dwc3_qcom_dev_pm_ops,
		.of_match_table	= dwc3_qcom_of_match,
		.acpi_match_table = ACPI_PTR(dwc3_qcom_acpi_match),
		.dev_groups = dwc3_attr_groups,
	},
};

module_platform_driver(dwc3_qcom_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 QCOM Glue Driver");
