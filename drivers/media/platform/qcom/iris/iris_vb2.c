// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_ctrls.h"
#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_vb2.h"
#include "iris_vdec.h"
#include "iris_venc.h"

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[])
{
	enum iris_buffer_type buffer_type = 0;
	struct iris_buffers *buffers;
	struct iris_inst *inst;
	struct iris_core *core;
	struct v4l2_format *f;
	int ret;

	if (!q || !num_buffers || !num_planes || !sizes)
		return -EINVAL;

	inst = vb2_get_drv_priv(q);
	if (!inst || !inst->core)
		return -EINVAL;

	core = inst->core;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		f = inst->fmt_src;
	else
		f = inst->fmt_dst;

	if (inst->state == IRIS_INST_STREAMING)
		return -EINVAL;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage)
			return -EINVAL;
	}

	buffer_type = v4l2_type_to_driver(q->type);
	if (!buffer_type)
		return -EINVAL;

	if (list_empty(&inst->caps_list)) {
		ret = prepare_dependency_list(inst);
		if (ret)
			return ret;
	}

	if ((inst->domain == DECODER && q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ||
	    (inst->domain == ENCODER && q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) {
		ret = adjust_v4l2_properties(inst);
		if (ret)
			return ret;
	}

	ret = check_session_supported(inst);
	if (ret)
		return ret;

	ret = iris_free_buffers(inst, buffer_type);
	if (ret)
		return ret;

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	buffers->min_count = iris_get_buf_min_count(inst, buffer_type);
	if (*num_buffers < buffers->min_count)
		*num_buffers = buffers->min_count;
	buffers->actual_count = *num_buffers;
	*num_planes = 1;

	buffers->size = iris_get_buffer_size(inst, buffer_type);

	f->fmt.pix_mp.plane_fmt[0].sizeimage = buffers->size;
	sizes[0] = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	ret = iris_allocate_buffers(inst, buffer_type, *num_buffers);

	q->dev = core->dev;

	return ret;
}

int iris_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	enum iris_buffer_type buf_type;
	struct iris_inst *inst;
	int ret = 0;

	if (!q)
		return -EINVAL;

	inst = vb2_get_drv_priv(q);
	if (!inst || !inst->core)
		return -EINVAL;

	if (q->type != INPUT_MPLANE && q->type != OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto error;
	}

	if (inst->domain != DECODER && inst->domain != ENCODER) {
		ret = -EINVAL;
		goto error;
	}

	ret = iris_pm_get(inst->core);
	if (ret)
		goto error;

	if (!inst->once_per_session_set) {
		inst->once_per_session_set = true;
		ret = iris_hfi_session_set_codec(inst);
		if (ret)
			goto err_pm_get;

		if (inst->domain == ENCODER)  {
			ret = iris_alloc_and_queue_session_int_bufs(inst, BUF_ARP);
			if (ret)
				goto err_pm_get;
		} else if (inst->domain == DECODER) {
			ret = iris_hfi_session_set_default_header(inst);
			if (ret)
				goto err_pm_get;

			ret = iris_alloc_and_queue_session_int_bufs(inst, BUF_PERSIST);
			if (ret)
				goto err_pm_get;
		}
	}

	iris_scale_power(inst);

	if (q->type == INPUT_MPLANE) {
		if (inst->domain == DECODER)
			ret = vdec_streamon_input(inst);
		else if (inst->domain == ENCODER)
			ret = venc_streamon_input(inst);
	} else if (q->type == OUTPUT_MPLANE) {
		if (inst->domain == DECODER)
			ret = vdec_streamon_output(inst);
		else if (inst->domain == ENCODER)
			ret = venc_streamon_output(inst);
	}
	if (ret)
		goto err_pm_get;

	buf_type = v4l2_type_to_driver(q->type);
	if (!buf_type) {
		ret = -EINVAL;
		goto err_pm_get;
	}

	ret = queue_deferred_buffers(inst, buf_type);
	if (ret)
		goto err_pm_get;

	ret = iris_pm_put(inst->core, true);
	if (ret)
		goto error;

	return ret;

err_pm_get:
	iris_pm_put(inst->core, false);
error:
	iris_inst_change_state(inst, IRIS_INST_ERROR);

	return ret;
}

void iris_vb2_stop_streaming(struct vb2_queue *q)
{
	struct iris_inst *inst;
	int ret = 0;

	if (!q)
		return;

	inst = vb2_get_drv_priv(q);
	if (!inst)
		return;

	if (q->type != INPUT_MPLANE && q->type != OUTPUT_MPLANE)
		goto error;

	if (inst->domain != DECODER && inst->domain != ENCODER) {
		ret = -EINVAL;
		goto error;
	}

	ret = iris_pm_get_put(inst->core);
	if (ret)
		goto error;

	if (q->type == INPUT_MPLANE)
		ret = session_streamoff(inst, INPUT_MPLANE);
	else if (q->type == OUTPUT_MPLANE)
		ret = session_streamoff(inst, OUTPUT_MPLANE);

	if (ret)
		goto error;

	return;

error:
	iris_inst_change_state(inst, IRIS_INST_ERROR);
}

void iris_vb2_buf_queue(struct vb2_buffer *vb2)
{
	u64 ktime_ns = ktime_get_ns();
	struct iris_core *core;
	struct iris_inst *inst;
	int ret;

	inst = vb2_get_drv_priv(vb2->vb2_queue);
	if (!inst || !inst->core)
		return;

	core = inst->core;

	if (!vb2->planes[0].bytesused && vb2->type == INPUT_MPLANE) {
		ret = -EINVAL;
		goto exit;
	}

	if (vb2->type == INPUT_MPLANE) {
		ret = iris_update_input_rate(inst, div_u64(ktime_ns, 1000));
		if (ret)
			goto exit;
	}

	ret = iris_pm_get_put(core);
	if (ret)
		goto exit;

	if (inst->domain == DECODER)
		ret = vdec_qbuf(inst, vb2);
	else if (inst->domain == ENCODER)
		ret = venc_qbuf(inst, vb2);

exit:
	if (ret) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		vb2_buffer_done(vb2, VB2_BUF_STATE_ERROR);
	}
}

void *iris_vb2_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
			     struct dma_buf *dbuf, unsigned long size)
{
	struct iris_buffer *ro_buf, *dummy;
	enum iris_buffer_type buf_type;
	struct iris_buffers *buffers;
	struct iris_buffer *iter;
	struct iris_buffer *buf;
	struct iris_inst *inst;
	bool found = false;

	if (!vb || !dev || !dbuf || !vb->vb2_queue)
		return ERR_PTR(-EINVAL);

	inst = vb->vb2_queue->drv_priv;

	buf_type = v4l2_type_to_driver(vb->type);

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return NULL;

	list_for_each_entry(iter, &buffers->list, list) {
		if (iter->index == vb->index) {
			found = true;
			buf = iter;
			break;
		}
	}

	if (!found)
		return NULL;

	buf->inst = inst;
	buf->dmabuf = dbuf;

	if (inst->domain == DECODER && buf->type == BUF_OUTPUT) {
		list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
			if (ro_buf->dmabuf != buf->dmabuf)
				continue;
			buf->attach = ro_buf->attach;
			ro_buf->attach = NULL;
			return buf;
		}
	}

	buf->attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR(buf->attach)) {
		buf->attach = NULL;
		return NULL;
	}

	return buf;
}

int iris_vb2_map_dmabuf(void *buf_priv)
{
	struct iris_buffer *ro_buf, *dummy;
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return -EINVAL;

	inst = buf->inst;
	core = inst->core;

	if (!buf->attach) {
		dev_err(core->dev, "trying to map a non attached buffer\n");
		return -EINVAL;
	}

	if (inst->domain == DECODER && buf->type == BUF_OUTPUT) {
		list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
			if (ro_buf->dmabuf != buf->dmabuf)
				continue;
			buf->sg_table = ro_buf->sg_table;
			buf->device_addr = ro_buf->device_addr;
			ro_buf->sg_table = NULL;
			return 0;
		}
	}

	buf->sg_table = dma_buf_map_attachment(buf->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(buf->sg_table))
		return -EINVAL;

	if (!buf->sg_table->sgl) {
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
		return -EINVAL;
	}

	buf->device_addr = sg_dma_address(buf->sg_table->sgl);

	return 0;
}

void iris_vb2_unmap_dmabuf(void *buf_priv)
{
	struct iris_buffer *ro_buf, *dummy;
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return;

	inst = buf->inst;
	core = inst->core;

	if (!buf->attach) {
		dev_err(core->dev, "trying to unmap a non attached buffer\n");
		return;
	}

	if (!buf->sg_table) {
		dev_err(core->dev, "dmabuf buffer is already unmapped\n");
		return;
	}

	if (inst->domain == DECODER && buf->type == BUF_OUTPUT) {
		list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
			if (ro_buf->dmabuf != buf->dmabuf)
				continue;
			ro_buf->sg_table = buf->sg_table;
			buf->sg_table = NULL;
			buf->device_addr = 0x0;
			return;
		}
	}

	if (buf->attach && buf->sg_table) {
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
		buf->device_addr = 0x0;
	}
}

void iris_vb2_detach_dmabuf(void *buf_priv)
{
	struct iris_buffer *ro_buf, *dummy;
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return;

	inst = buf->inst;
	core = inst->core;

	if (buf->sg_table) {
		dev_err(core->dev, "trying to detach an unmapped buffer\n");
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
	}

	if (inst->domain == DECODER && buf->type == BUF_OUTPUT) {
		list_for_each_entry_safe(ro_buf, dummy, &inst->buffers.read_only.list, list) {
			if (ro_buf->dmabuf != buf->dmabuf)
				continue;
			ro_buf->attach = buf->attach;
			buf->attach = NULL;
			goto exit;
		}
	}

	if (buf->attach && buf->dmabuf) {
		dma_buf_detach(buf->dmabuf, buf->attach);
		buf->attach = NULL;
	}

exit:
	buf->dmabuf = NULL;
	buf->inst = NULL;
}

void *iris_vb2_alloc(struct vb2_buffer *vb, struct device *dev,
		     unsigned long size)
{
	return (void *)0xdeadbeef;
}

void iris_vb2_put(void *buf_priv)
{
}

int iris_vb2_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	return 0;
}
