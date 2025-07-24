// SPDX-License-Identifier: GPL-2.0
/*
 * camss-tpg-gen1.c
 *
 * Qualcomm MSM Camera Subsystem - TPG (Test Patter Generator) Module
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include "camss-tpg.h"
#include "camss.h"

#define TPG_HW_VERSION		0x0
#define		HW_VERSION_STEPPING		0
#define		HW_VERSION_REVISION		16
#define		HW_VERSION_GENERATION		28

#define TPG_HW_STATUS		0x4

#define TPG_VC_n_GAIN_CFG(n)	(0x60 + (n) * 0x60)

#define TPG_CTRL	0x64
#define		TPG_CTRL_TEST_EN		0
#define		TPG_CTRL_PHY_SEL		3
#define		TPG_CTRL_NUM_ACTIVE_LANES	4
#define		TPG_CTRL_VC_DT_PATTERN_ID	6
#define		TPG_CTRL_OVERLAP_SHDR_EN	10
#define		TPG_CTRL_NUM_ACTIVE_VC		30
#define			NUM_ACTIVE_VC_0_ENABLED		0
#define			NUM_ACTIVE_VC_0_1_ENABLED	1
#define			NUM_ACTIVE_VC_0_1_2_ENABLED	2
#define			NUM_ACTIVE_VC_0_1_3_ENABLED	3

#define TPG_VC_n_CFG0(n)	(0x68 + (n) * 0x60)
#define		TPG_VC_n_CFG0_VC_NUM		0
#define		TPG_VC_n_CFG0_NUM_ACTIVE_DT	8
#define			NUM_ACTIVE_SLOTS_0_ENABLED		0
#define			NUM_ACTIVE_SLOTS_0_1_ENABLED		1
#define			NUM_ACTIVE_SLOTS_0_1_2_ENABLED		2
#define			NUM_ACTIVE_SLOTS_0_1_3_ENABLED		3
#define		TPG_VC_n_CFG0_NUM_BATCH		12
#define		TPG_VC_n_CFG0_NUM_FRAMES	16

#define TPG_VC_n_LSFR_SEED(n)		(0x6C + (n) * 0x60)

#define TPG_VC_n_HBI_CFG(n)		(0x70 + (n) * 0x60)

#define TPG_VC_n_VBI_CFG(n)		(0x74 + (n) * 0x60)

#define TPG_VC_n_COLOR_BARS_CFG(n)		(0x78 + (n) * 0x60)
#define		TPG_VC_n_COLOR_BARS_CFG_PIX_PATTERN		0
#define		TPG_VC_n_COLOR_BARS_CFG_QCFA_EN			3
#define		TPG_VC_n_COLOR_BARS_CFG_SPLIT_EN		4
#define		TPG_VC_n_COLOR_BARS_CFG_NOISE_EN		5
#define		TPG_VC_n_COLOR_BARS_CFG_ROTATE_PERIOD		8
#define		TPG_VC_n_COLOR_BARS_CFG_XCFA_EN			16
#define		TPG_VC_n_COLOR_BARS_CFG_SIZE_X			24
#define		TPG_VC_n_COLOR_BARS_CFG_SIZE_Y			28

#define TPG_VC_m_DT_n_CFG_0(m, n)	(0x7C + (m) * 0x60 + (n) * 0xC)
#define		TPG_VC_m_DT_n_CFG_0_FRAME_HEIGHT	0
#define		TPG_VC_m_DT_n_CFG_0_FRAME_WIDTH		16

#define TPG_VC_m_DT_n_CFG_1(m, n)	(0x80 + (m) * 0x60 + (n) * 0xC)
#define		TPG_VC_m_DT_n_CFG_1_DATA_TYPE		0
#define		TPG_VC_m_DT_n_CFG_1_ECC_XOR_MASK	8
#define		TPG_VC_m_DT_n_CFG_1_CRC_XOR_MASK	16

#define TPG_VC_m_DT_n_CFG_2(m, n)	(0x84 + (m) * 0x60 + (n) * 0xC)
#define		TPG_VC_m_DT_n_CFG_2_PAYLOAD_MODE		0
#define		TPG_VC_m_DT_n_CFG_2_USER_SPECIFIED_PAYLOAD	4
#define		TPG_VC_m_DT_n_CFG_2_ENCODE_FORMAT		28

#define TPG_VC_n_COLOR_BAR_CFA_COLOR0(n)	(0xB0 + (n) * 0x60)
#define TPG_VC_n_COLOR_BAR_CFA_COLOR1(n)	(0xB4 + (n) * 0x60)
#define TPG_VC_n_COLOR_BAR_CFA_COLOR2(n)	(0xB8 + (n) * 0x60)
#define TPG_VC_n_COLOR_BAR_CFA_COLOR3(n)	(0xBC + (n) * 0x60)

/* Line offset between VC(n) and VC(n-1), n form 1 to 3 */
#define TPG_VC_n_SHDR_CFG	(0x84 + (n) * 0x60)

#define TPG_TOP_IRQ_STATUS	0x1E0
#define TPG_TOP_IRQ_MASK	0x1E4
#define TPG_TOP_IRQ_CLEAR	0x1E8
#define TPG_TOP_IRQ_SET		0x1EC
#define TPG_IRQ_CMD		0x1F0
#define TPG_CLEAR		0x1F4

static int tpg_stream_on(struct tpg_device *tpg)
{
	struct tpg_testgen_config *tg = &tpg->testgen;
	struct v4l2_mbus_framefmt *input_format;
	const struct tpg_format_info *format;
	u8 lane_cnt = tpg->res->lane_cnt;
	u8 i;
	u8 dt_cnt = 0;
	u32 val;

	/* Loop through all enabled VCs and configure stream for each */
	for (i = 0; i < tpg->res->vc_cnt; i++) {
		input_format = &tpg->fmt[MSM_TPG_PAD_SRC + i];
		format = tpg_get_fmt_entry(tpg->res->formats->formats,
					   tpg->res->formats->nformats,
					   input_format->code);

		val = (input_format->height & 0xffff) << TPG_VC_m_DT_n_CFG_0_FRAME_HEIGHT;
		val |= (input_format->width & 0xffff) << TPG_VC_m_DT_n_CFG_0_FRAME_WIDTH;
		writel_relaxed(val, tpg->base + TPG_VC_m_DT_n_CFG_0(i, dt_cnt));

		val = format->data_type << TPG_VC_m_DT_n_CFG_1_DATA_TYPE;
		writel_relaxed(val, tpg->base + TPG_VC_m_DT_n_CFG_1(i, dt_cnt));

		val = (tg->mode - 1) << TPG_VC_m_DT_n_CFG_2_PAYLOAD_MODE;
		val |= 0xBE << TPG_VC_m_DT_n_CFG_2_USER_SPECIFIED_PAYLOAD;
		val |= format->encode_format << TPG_VC_m_DT_n_CFG_2_ENCODE_FORMAT;
		writel_relaxed(val, tpg->base + TPG_VC_m_DT_n_CFG_2(i, dt_cnt));

		writel_relaxed(0xA00, tpg->base + TPG_VC_n_COLOR_BARS_CFG(i));

		writel_relaxed(0x4701, tpg->base + TPG_VC_n_HBI_CFG(i));
		writel_relaxed(0x438, tpg->base + TPG_VC_n_VBI_CFG(i));

		writel_relaxed(0x12345678, tpg->base + TPG_VC_n_LSFR_SEED(i));

		/* configure one DT, infinite frames */
		val = i << TPG_VC_n_CFG0_VC_NUM;
		val |= 0 << TPG_VC_n_CFG0_NUM_FRAMES;
		writel_relaxed(val, tpg->base + TPG_VC_n_CFG0(i));
	}

	writel_relaxed(1, tpg->base + TPG_TOP_IRQ_MASK);

	val = 1 << TPG_CTRL_TEST_EN;
	val |= 0 << TPG_CTRL_PHY_SEL;
	val |= (lane_cnt - 1) << TPG_CTRL_NUM_ACTIVE_LANES;
	val |= 0 << TPG_CTRL_VC_DT_PATTERN_ID;
	val |= (tpg->res->vc_cnt - 1) << TPG_CTRL_NUM_ACTIVE_VC;
	writel_relaxed(val, tpg->base + TPG_CTRL);

	return 0;
}

static void tpg_stream_off(struct tpg_device *tpg)
{
	writel_relaxed(0, tpg->base + TPG_CTRL);
	writel_relaxed(0, tpg->base + TPG_TOP_IRQ_MASK);
	writel_relaxed(1, tpg->base + TPG_TOP_IRQ_CLEAR);
	writel_relaxed(1, tpg->base + TPG_IRQ_CMD);
	writel_relaxed(1, tpg->base + TPG_CLEAR);
}

static void tpg_configure_stream(struct tpg_device *tpg, u8 enable)
{
	if (enable)
		tpg_stream_on(tpg);
	else
		tpg_stream_off(tpg);
}

static int tpg_configure_testgen_pattern(struct tpg_device *tpg, s32 val)
{
	if (val > 0 && val <= TPG_PAYLOAD_MODE_COLOR_BARS)
		tpg->testgen.mode = val;

	return 0;
}

/*
 * tpg_hw_version - tpg hardware version query
 * @tpg: tpg device
 *
 * Return HW version or error
 */
static u32 tpg_hw_version(struct tpg_device *tpg)
{
	u32 hw_version;
	u32 hw_gen;
	u32 hw_rev;
	u32 hw_step;

	hw_version = readl_relaxed(tpg->base + TPG_HW_VERSION);
	hw_gen = (hw_version >> HW_VERSION_GENERATION) & 0xF;
	hw_rev = (hw_version >> HW_VERSION_REVISION) & 0xFFF;
	hw_step = (hw_version >> HW_VERSION_STEPPING) & 0xFFFF;
	dev_dbg(tpg->camss->dev, "tpg HW Version = %u.%u.%u\n",
		hw_gen, hw_rev, hw_step);

	return hw_version;
}

/*
 * tpg_isr - tpg module interrupt service routine
 * @irq: Interrupt line
 * @dev: tpg device
 *
 * Return IRQ_HANDLED on success
 */
static irqreturn_t tpg_isr(int irq, void *dev)
{
	struct tpg_device *tpg = dev;
	u32 val;

	val = readl_relaxed(tpg->base + TPG_TOP_IRQ_STATUS);
	writel_relaxed(val, tpg->base + TPG_TOP_IRQ_CLEAR);
	writel_relaxed(1, tpg->base + TPG_IRQ_CMD);

	return IRQ_HANDLED;
}

/*
 * tpg_reset - Trigger reset on tpg module and wait to complete
 * @tpg: tpg device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_reset(struct tpg_device *tpg)
{
	writel_relaxed(0, tpg->base + TPG_CTRL);
	writel_relaxed(0, tpg->base + TPG_TOP_IRQ_MASK);
	writel_relaxed(1, tpg->base + TPG_TOP_IRQ_CLEAR);
	writel_relaxed(1, tpg->base + TPG_IRQ_CMD);
	writel_relaxed(1, tpg->base + TPG_CLEAR);

	return 0;
}

static void tpg_subdev_init(struct tpg_device *tpg)
{
	tpg->testgen.modes = testgen_payload_modes;
	tpg->testgen.nmodes = TPG_PAYLOAD_MODE_DISABLED;
}

const struct tpg_hw_ops tpg_ops_gen1 = {
	.configure_stream = tpg_configure_stream,
	.configure_testgen_pattern = tpg_configure_testgen_pattern,
	.hw_version = tpg_hw_version,
	.isr = tpg_isr,
	.reset = tpg_reset,
	.subdev_init = tpg_subdev_init,
};
