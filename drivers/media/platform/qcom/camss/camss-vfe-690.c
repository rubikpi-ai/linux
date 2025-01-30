// SPDX-License-Identifier: GPL-2.0
/*
 * camss-vfe-690.c
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module v690 (SA8755P)
 *
 * Copyright (C) 2020-2021 Linaro Ltd.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

#define VFE_HW_VERSION                  (0x0)
#define HW_VERSION_STEPPING		(0x0)
#define HW_VERSION_REVISION		(0x10)
#define HW_VERSION_GENERATION		(0x1C)
#define VFE_TOP_CORE_CFG		(0x24)

#define BUS_REG_BASE			(vfe_is_lite(vfe) ? 0x480 : 0x400)

#define VFE_BUS_WM_CGC_OVERRIDE		(BUS_REG_BASE + 0x08)
#define	WM_CGC_OVERRIDE_ALL		(0x7FFFFFF)

#define VFE_BUS_WM_TEST_BUS_CTRL	(BUS_REG_BASE + 0xFC)

#define VFE_BUS_WM_CFG(n)		(BUS_REG_BASE + 0x200 + (n) * 0x100)
#define		WM_CFG_EN			BIT(0)
#define		WM_CFG_MODE			BIT(16)
#define VFE_BUS_WM_IMAGE_ADDR(n)	(BUS_REG_BASE + 0x204 + (n) * 0x100)
#define VFE_BUS_WM_FRAME_INCR(n)	(BUS_REG_BASE + 0x208 + (n) * 0x100)
#define VFE_BUS_WM_IMAGE_CFG_0(n)	(BUS_REG_BASE + 0x20c + (n) * 0x100)
#define		WM_IMAGE_CFG_0_DEFAULT_WIDTH	(0xFFFF)
#define		WM_IMAGE_CFG_0_DEFAULT_HEIGHT	(0xFFFF)
#define VFE_BUS_WM_IMAGE_CFG_1(n)	(BUS_REG_BASE + 0x210 + (n) * 0x100)
#define VFE_BUS_WM_IMAGE_CFG_2(n)	(BUS_REG_BASE + 0x214 + (n) * 0x100)
#define		WM_IMAGE_CFG_2_DEFAULT_STRIDE	(0xFFFF)
#define VFE_BUS_WM_PACKER_CFG(n)	(BUS_REG_BASE + 0x218 + (n) * 0x100)
#define VFE_BUS_WM_HEADER_ADDR(n)	(BUS_REG_BASE + 0x220 + (n) * 0x100)
#define VFE_BUS_WM_HEADER_INCR(n)	(BUS_REG_BASE + 0x224 + (n) * 0x100)
#define VFE_BUS_WM_HEADER_CFG(n)	(BUS_REG_BASE + 0x228 + (n) * 0x100)

#define VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(n)	(BUS_REG_BASE + 0x230 + (n) * 0x100)
#define VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(n)	(BUS_REG_BASE + 0x234 + (n) * 0x100)
#define VFE_BUS_WM_FRAMEDROP_PERIOD(n)		(BUS_REG_BASE + 0x238 + (n) * 0x100)
#define VFE_BUS_WM_FRAMEDROP_PATTERN(n)		(BUS_REG_BASE + 0x23c + (n) * 0x100)

#define VFE_BUS_WM_MMU_PREFETCH_CFG(n)		(BUS_REG_BASE + 0x260 + (n) * 0x100)
#define VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(n)	(BUS_REG_BASE + 0x264 + (n) * 0x100)
#define VFE_BUS_WM_SYSTEM_CACHE_CFG(n)		(BUS_REG_BASE + 0x268 + (n) * 0x100)

#define VFE_BUS_WM_STATUS_DEBUG_CFG(n)		(BUS_REG_BASE + 0x280 + (n) * 0x100)

/* for titan 690, each bus client is hardcoded to a specific path */
#define RDI_WM(n)			((vfe_is_lite(vfe) ? 0x0 : 0x10) + (n))

static void vfe_wm_start(struct vfe_device *vfe, u8 wm, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix =
		&line->video_out.active_fmt.fmt.pix_mp;

	wm = RDI_WM(wm);

	writel(WM_CGC_OVERRIDE_ALL, vfe->base + VFE_BUS_WM_CGC_OVERRIDE);
	/* no clock gating at bus input */
	writel(0x0, vfe->base + VFE_BUS_WM_TEST_BUS_CTRL);

	writel(ALIGN(pix->plane_fmt[0].bytesperline, 16) * pix->height,
	       vfe->base + VFE_BUS_WM_FRAME_INCR(wm));
	writel(WM_IMAGE_CFG_0_DEFAULT_WIDTH & 0xFFFF,
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_0(wm));
	writel(WM_IMAGE_CFG_2_DEFAULT_STRIDE,
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_2(wm));

	writel(0x0, vfe->base + VFE_BUS_WM_PACKER_CFG(wm));

	/* TOP CORE CFG */
	writel(0x600000, vfe->base + VFE_TOP_CORE_CFG);

	/* Enable WM in frame based mode */
	writel(WM_CFG_EN | WM_CFG_MODE, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_stop(struct vfe_device *vfe, u8 wm)
{
	wm = RDI_WM(wm); /* map to actual WM used (from wm=RDI index) */
	writel(0, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_update(struct vfe_device *vfe, u8 wm, u32 addr,
			  struct vfe_line *line)
{
	wm = RDI_WM(wm); /* map to actual WM used (from wm=RDI index) */
	writel(addr, vfe->base + VFE_BUS_WM_IMAGE_ADDR(wm));
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	/* RUP(register update) registers has beem moved to CSID in Titan 690.
	 * Notify the event of trigger RUP.
	 */
	camss_reg_update(vfe->camss, vfe->id, port_id, false);
}

static inline void vfe_reg_update_clear(struct vfe_device *vfe,
					enum vfe_line_id line_id)
{
	int port_id = line_id;

	/* RUP(register update) registers has beem moved to CSID in Titan 690.
	 * Notify the event of trigger RUP clear.
	 */
	camss_reg_update(vfe->camss, vfe->id, port_id, true);
}


/*
 * vfe_hw_version - Process write master done interrupt
 * @vfe: VFE Device
 *
 * Return vfe hw version
 */
static u32 vfe_hw_version(struct vfe_device *vfe)
{
	u32 hw_version = readl_relaxed(vfe->base + VFE_HW_VERSION);

	u32 gen = (hw_version >> HW_VERSION_GENERATION) & 0xF;
	u32 rev = (hw_version >> HW_VERSION_REVISION) & 0xFFF;
	u32 step = (hw_version >> HW_VERSION_STEPPING) & 0xFFFF;

	dev_info(vfe->camss->dev, "VFE:%d HW Version = %u.%u.%u\n",
		 vfe->id, gen, rev, step);

	return hw_version;
}

/*
 * vfe_buf_done - Process write master done interrupt
 * @vfe: VFE Device
 * @wm: Write master id
 */
static void vfe_buf_done(struct vfe_device *vfe, int wm)
{
	struct vfe_line *line = &vfe->line[vfe->wm_output_map[wm]];
	struct camss_buffer *ready_buf;
	struct vfe_output *output;
	unsigned long flags;
	u32 index;
	u64 ts = ktime_get_ns();

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (vfe->wm_output_map[wm] == VFE_LINE_NONE) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Received wm done for unmapped index\n");
		goto out_unlock;
	}
	output = &vfe->line[vfe->wm_output_map[wm]].output;

	ready_buf = output->buf[0];
	if (!ready_buf) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Missing ready buf %d!\n", output->state);
		goto out_unlock;
	}

	ready_buf->vb.vb2_buf.timestamp = ts;
	ready_buf->vb.sequence = output->sequence++;

	index = 0;
	output->buf[0] = output->buf[1];
	if (output->buf[0])
		index = 1;

	output->buf[index] = vfe_buf_get_pending(output);

	if (output->buf[index]) {
		vfe->res->hw_ops->vfe_wm_update(vfe, output->wm_idx[0],
						output->buf[index]->addr[0],
						line);
		vfe->res->hw_ops->reg_update(vfe, line->id);
	} else
		output->gen2.active_num--;

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	vb2_buffer_done(&ready_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	return;

out_unlock:
	spin_unlock_irqrestore(&vfe->output_lock, flags);
}

static int vfe_enable_output_v2(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output = &line->output;
	const struct vfe_hw_ops *ops = vfe->res->hw_ops;
	struct media_entity *sensor;
	unsigned long flags;
	unsigned int frame_skip = 0;
	unsigned int i;

	sensor = camss_find_sensor(&line->subdev.entity);
	if (sensor) {
		struct v4l2_subdev *subdev = media_entity_to_v4l2_subdev(sensor);

		v4l2_subdev_call(subdev, sensor, g_skip_frames, &frame_skip);
		/* Max frame skip is 29 frames */
		if (frame_skip > VFE_FRAME_DROP_VAL - 1)
			frame_skip = VFE_FRAME_DROP_VAL - 1;
	}

	spin_lock_irqsave(&vfe->output_lock, flags);

	ops->reg_update_clear(vfe, line->id);

	if (output->state > VFE_OUTPUT_RESERVED) {
		dev_err(vfe->camss->dev,
			"Output is not in reserved state %d\n",
			output->state);
		spin_unlock_irqrestore(&vfe->output_lock, flags);
		return -EINVAL;
	}

	WARN_ON(output->gen2.active_num);

	output->state = VFE_OUTPUT_ON;

	output->sequence = 0;
	output->wait_reg_update = 0;
	reinit_completion(&output->reg_update);

	ops->vfe_wm_start(vfe, output->wm_idx[0], line);

	for (i = 0; i < 2; i++) {
		output->buf[i] = vfe_buf_get_pending(output);
		if (!output->buf[i])
			break;
		output->gen2.active_num++;
		ops->vfe_wm_update(vfe, output->wm_idx[0],
				   output->buf[i]->addr[0], line);
		ops->reg_update(vfe, line->id);
	}

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

/*
 * vfe_queue_buffer_v2 - Add empty buffer
 * @vid: Video device structure
 * @buf: Buffer to be enqueued
 *
 * Add an empty buffer - depending on the current number of buffers it will be
 * put in pending buffer queue or directly given to the hardware to be filled.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_queue_buffer_v2(struct camss_video *vid,
			struct camss_buffer *buf)
{
	struct vfe_line *line = container_of(vid, struct vfe_line, video_out);
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	unsigned long flags;

	output = &line->output;

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (output->state == VFE_OUTPUT_ON &&
		output->gen2.active_num < 2) {
		output->buf[output->gen2.active_num++] = buf;
		vfe->res->hw_ops->vfe_wm_update(vfe, output->wm_idx[0],
						buf->addr[0], line);
		vfe->res->hw_ops->reg_update(vfe, line->id);
	} else {
		vfe_buf_add_pending(output, buf);
	}

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

/*
 * vfe_get_output_v2 - Get vfe output port for corresponding VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_get_output_v2(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	unsigned long flags;

	spin_lock_irqsave(&vfe->output_lock, flags);

	output = &line->output;
	if (output->state > VFE_OUTPUT_RESERVED) {
		dev_err(vfe->camss->dev, "Output is running\n");
		goto error;
	}

	output->wm_num = 1;

	/* Correspondence between VFE line number and WM number.
	 * line 0 -> RDI 0, line 1 -> RDI1, line 2 -> RDI2, line 3 -> PIX/RDI3
	 * Note this 1:1 mapping will not work for PIX streams.
	 */
	output->wm_idx[0] = line->id;
	vfe->wm_output_map[line->id] = line->id;

	output->drop_update_idx = 0;

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;

error:
	spin_unlock_irqrestore(&vfe->output_lock, flags);
	output->state = VFE_OUTPUT_OFF;

	return -EINVAL;
}

/*
 * vfe_enable_v2 - Enable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_enable_v2(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	int ret;

	mutex_lock(&vfe->stream_lock);

	if (vfe->res->hw_ops->enable_irq_common)
		vfe->res->hw_ops->enable_irq_common(vfe);

	vfe->stream_count++;

	mutex_unlock(&vfe->stream_lock);

	ret = vfe_get_output_v2(line);
	if (ret < 0)
		goto error_get_output;

	ret = vfe_enable_output_v2(line);
	if (ret < 0)
		goto error_enable_output;

	vfe->was_streaming = 1;

	return 0;

error_enable_output:
	vfe_put_output(line);

error_get_output:
	mutex_lock(&vfe->stream_lock);

	vfe->stream_count--;

	mutex_unlock(&vfe->stream_lock);

	return ret;
}


static const struct camss_video_ops vfe_video_ops_690 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_690;
}

const struct vfe_hw_ops vfe_ops_690 = {
	.enable_irq_common = NULL,
	.global_reset = NULL,
	.hw_version = vfe_hw_version,
	.isr = NULL,
	.pm_domain_off = vfe_pm_domain_off,
	.pm_domain_on = vfe_pm_domain_on,
	.reg_update = vfe_reg_update,
	.reg_update_clear = vfe_reg_update_clear,
	.subdev_init = vfe_subdev_init,
	.vfe_disable = vfe_disable,
	.vfe_enable = vfe_enable_v2,
	.vfe_halt = NULL,
	.vfe_wm_start = vfe_wm_start,
	.vfe_wm_stop = vfe_wm_stop,
	.vfe_buf_done = vfe_buf_done,
	.vfe_wm_update = vfe_wm_update,
};
