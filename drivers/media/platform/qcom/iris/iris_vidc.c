// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/videodev2.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_vdec.h"
#include "iris_venc.h"
#include "iris_vidc.h"
#include "iris_vb2.h"
#include "memory.h"

#define VIDC_DRV_NAME "iris_driver"
#define VIDC_BUS_NAME "platform:iris_bus"

static int vidc_v4l2_fh_init(struct iris_inst *inst)
{
	struct iris_core *core;

	core = inst->core;

	if (inst->fh.vdev)
		return -EINVAL;

	if (inst->domain == ENCODER)
		v4l2_fh_init(&inst->fh, core->vdev_enc);
	else if (inst->domain == DECODER)
		v4l2_fh_init(&inst->fh, core->vdev_dec);

	inst->fh.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->fh);

	return 0;
}

static int vidc_v4l2_fh_deinit(struct iris_inst *inst)
{
	if (!inst->fh.vdev)
		return 0;

	v4l2_fh_del(&inst->fh);
	inst->fh.ctrl_handler = NULL;
	v4l2_fh_exit(&inst->fh);

	return 0;
}

static int vb2q_init(struct iris_inst *inst,
		     struct vb2_queue *q, enum v4l2_buf_type type)
{
	struct iris_core *core;

	core = inst->core;

	q->lock = &inst->ctx_q_lock;
	q->type = type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	q->ops = core->vb2_ops;
	q->mem_ops = core->vb2_mem_ops;
	q->drv_priv = inst;
	q->copy_timestamp = 1;
	q->min_buffers_needed = 0;
	return vb2_queue_init(q);
}

static int vidc_vb2_queue_init(struct iris_inst *inst)
{
	int ret;

	ret = vb2q_init(inst, inst->vb2q_src, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = vb2q_init(inst, inst->vb2q_dst, OUTPUT_MPLANE);
	if (ret)
		goto fail_vb2q_src_deinit;

	return ret;

fail_vb2q_src_deinit:
	vb2_queue_release(inst->vb2q_src);

	return ret;
}

static int vidc_vb2_queue_deinit(struct iris_inst *inst)
{
	vb2_queue_release(inst->vb2q_src);
	kfree(inst->vb2q_src);
	inst->vb2q_src = NULL;

	vb2_queue_release(inst->vb2q_dst);
	kfree(inst->vb2q_dst);
	inst->vb2q_dst = NULL;

	return 0;
}

static int vidc_add_session(struct iris_inst *inst)
{
	struct iris_core *core;
	struct iris_inst *i;
	u32 count = 0;
	int ret = 0;

	core = inst->core;

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT) {
		ret = -EINVAL;
		goto unlock;
	}
	list_for_each_entry(i, &core->instances, list)
		count++;

	if (count < core->cap[MAX_SESSION_COUNT].value)
		list_add_tail(&inst->list, &core->instances);
	else
		ret = -EAGAIN;
unlock:
	mutex_unlock(&core->lock);

	return ret;
}

static int vidc_remove_session(struct iris_inst *inst)
{
	struct iris_inst *i, *temp;
	struct iris_core *core;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry_safe(i, temp, &core->instances, list) {
		if (i->session_id == inst->session_id) {
			list_del_init(&i->list);
			break;
		}
	}
	mutex_unlock(&core->lock);

	return 0;
}

static struct iris_inst *get_vidc_inst(struct file *filp, void *fh)
{
	if (!filp || !filp->private_data)
		return NULL;

	return container_of(filp->private_data,
					struct iris_inst, fh);
}

int vidc_open(struct file *filp)
{
	struct iris_core *core = video_drvdata(filp);
	struct iris_inst *inst = NULL;
	struct video_device *vdev;
	u32 session_type = 0;
	int i = 0;
	int ret;

	vdev = video_devdata(filp);
	if (strcmp(vdev->name, "qcom-iris-decoder") == 0)
		session_type = DECODER;
	else if (strcmp(vdev->name, "qcom-iris-encoder") == 0)
		session_type = ENCODER;

	if (session_type != DECODER && session_type != ENCODER)
		return -EINVAL;

	ret = iris_pm_get(core);
	if (ret)
		return ret;

	ret = iris_core_init(core);
	if (ret)
		goto fail_pm_put;

	ret = iris_core_init_wait(core);
	if (ret)
		goto fail_pm_put;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst) {
		ret = -ENOMEM;
		goto fail_pm_put;
	}

	inst->core = core;
	inst->domain = session_type;
	inst->session_id = hash32_ptr(inst);
	inst->ipsc_properties_set = false;
	inst->opsc_properties_set = false;
	inst->has_bframe = false;
	inst->iframe = false;
	iris_inst_change_state(inst, IRIS_INST_OPEN);
	mutex_init(&inst->lock);
	mutex_init(&inst->ctx_q_lock);

	ret = vidc_add_session(inst);
	if (ret)
		goto fail_free_inst;

	ret = iris_mem_pool_init(inst);
	if (ret)
		goto fail_remove_session;

	INIT_LIST_HEAD(&inst->buffers.input.list);
	INIT_LIST_HEAD(&inst->buffers.output.list);
	INIT_LIST_HEAD(&inst->buffers.read_only.list);
	INIT_LIST_HEAD(&inst->buffers.bin.list);
	INIT_LIST_HEAD(&inst->buffers.arp.list);
	INIT_LIST_HEAD(&inst->buffers.comv.list);
	INIT_LIST_HEAD(&inst->buffers.non_comv.list);
	INIT_LIST_HEAD(&inst->buffers.line.list);
	INIT_LIST_HEAD(&inst->buffers.dpb.list);
	INIT_LIST_HEAD(&inst->buffers.persist.list);
	INIT_LIST_HEAD(&inst->buffers.vpss.list);
	INIT_LIST_HEAD(&inst->caps_list);
	INIT_LIST_HEAD(&inst->input_timer_list);
	for (i = 0; i < MAX_SIGNAL; i++)
		init_completion(&inst->completions[i]);

	ret = vidc_v4l2_fh_init(inst);
	if (ret)
		goto fail_mem_pool_deinit;

	if (inst->domain == DECODER)
		ret = vdec_inst_init(inst);
	else if (inst->domain == ENCODER)
		ret = venc_inst_init(inst);
	if (ret)
		goto fail_fh_deinit;

	ret = vidc_vb2_queue_init(inst);
	if (ret)
		goto fail_inst_deinit;

	iris_scale_power(inst);

	ret = iris_hfi_session_open(inst);
	if (ret) {
		dev_err(core->dev, "%s: session open failed\n", __func__);
		goto fail_core_deinit;
	}

	iris_pm_put(core, true);

	filp->private_data = &inst->fh;

	return 0;

fail_core_deinit:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	iris_core_deinit(core);
	vidc_vb2_queue_deinit(inst);
fail_inst_deinit:
	if (inst->domain == DECODER)
		vdec_inst_deinit(inst);
	else if (inst->domain == ENCODER)
		venc_inst_deinit(inst);
fail_fh_deinit:
	vidc_v4l2_fh_deinit(inst);
fail_mem_pool_deinit:
	iris_mem_pool_deinit(inst);
fail_remove_session:
	vidc_remove_session(inst);
fail_free_inst:
	mutex_destroy(&inst->ctx_q_lock);
	mutex_destroy(&inst->lock);
	kfree(inst);
fail_pm_put:
	iris_pm_put(core, false);

	return ret;
}

int vidc_close(struct file *filp)
{
	struct iris_inst *inst;
	struct iris_core *core;

	inst = get_vidc_inst(filp, NULL);
	if (!inst)
		return -EINVAL;

	core = inst->core;

	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	if (inst->domain == DECODER)
		vdec_inst_deinit(inst);
	else if (inst->domain == ENCODER)
		venc_inst_deinit(inst);

	mutex_lock(&inst->lock);
	iris_pm_get(core);
	close_session(inst);
	iris_inst_change_state(inst, IRIS_INST_CLOSE);
	vidc_vb2_queue_deinit(inst);
	vidc_v4l2_fh_deinit(inst);
	iris_destroy_buffers(inst);
	vidc_remove_session(inst);
	iris_pm_put(core, false);
	mutex_unlock(&inst->lock);
	mutex_destroy(&inst->ctx_q_lock);
	mutex_destroy(&inst->lock);
	kfree(inst);
	filp->private_data = NULL;

	return 0;
}

static __poll_t get_poll_flags(struct iris_inst *inst, u32 plane)
{
	struct vb2_buffer *vb = NULL;
	struct vb2_queue *q = NULL;
	unsigned long flags = 0;
	__poll_t poll = 0;

	if (plane == INPUT_MPLANE)
		q = inst->vb2q_src;
	else if (plane == OUTPUT_MPLANE)
		q = inst->vb2q_dst;

	if (!q)
		return EPOLLERR;

	spin_lock_irqsave(&q->done_lock, flags);
	if (!list_empty(&q->done_list))
		vb = list_first_entry(&q->done_list, struct vb2_buffer,
				      done_entry);
	if (vb && (vb->state == VB2_BUF_STATE_DONE ||
		   vb->state == VB2_BUF_STATE_ERROR)) {
		if (plane == OUTPUT_MPLANE)
			poll |= EPOLLIN | EPOLLRDNORM;
		else if (plane == INPUT_MPLANE)
			poll |= EPOLLOUT | EPOLLWRNORM;
	}
	spin_unlock_irqrestore(&q->done_lock, flags);

	return poll;
}

static __poll_t vidc_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct iris_inst *inst;
	__poll_t poll = 0;

	inst = get_vidc_inst(filp, NULL);
	if (!inst)
		return EPOLLERR;

	if (IS_SESSION_ERROR(inst))
		return EPOLLERR;

	poll_wait(filp, &inst->fh.wait, pt);
	poll_wait(filp, &inst->vb2q_src->done_wq, pt);
	poll_wait(filp, &inst->vb2q_dst->done_wq, pt);

	if (v4l2_event_pending(&inst->fh))
		poll |= EPOLLPRI;

	poll |= get_poll_flags(inst, INPUT_MPLANE);
	poll |= get_poll_flags(inst, OUTPUT_MPLANE);

	return poll;
}

static int vidc_enum_fmt(struct file *filp, void *fh, struct v4l2_fmtdesc *f)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain == DECODER)
		ret = vdec_enum_fmt(inst, f);
	else if (inst->domain == ENCODER)
		ret = venc_enum_fmt(inst, f);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_try_fmt(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!allow_s_fmt(inst, f->type)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain == DECODER)
		ret = vdec_try_fmt(inst, f);
	else if (inst->domain == ENCODER)
		ret = venc_try_fmt(inst, f);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_s_fmt(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!allow_s_fmt(inst, f->type)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain == DECODER)
		ret = vdec_s_fmt(inst, f);
	else if (inst->domain == ENCODER)
		ret = venc_s_fmt(inst, f);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_g_fmt(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (f->type == INPUT_MPLANE)
		memcpy(f, inst->fmt_src, sizeof(*f));
	else if (f->type == OUTPUT_MPLANE)
		memcpy(f, inst->fmt_dst, sizeof(*f));

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_enum_framesizes(struct file *filp, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	enum colorformat_type colorfmt;
	struct iris_inst *inst;
	enum codec_type codec;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !fsize)
		return -EINVAL;

	if (fsize->index)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	codec = v4l2_codec_to_driver(inst, fsize->pixel_format);
	if (!codec) {
		colorfmt = v4l2_colorformat_to_driver(inst, fsize->pixel_format);
		if (colorfmt == FMT_NONE) {
			ret = -EINVAL;
			goto unlock;
		}
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = inst->cap[FRAME_WIDTH].min;
	fsize->stepwise.max_width = inst->cap[FRAME_WIDTH].max;
	fsize->stepwise.step_width = inst->cap[FRAME_WIDTH].step_or_mask;
	fsize->stepwise.min_height = inst->cap[FRAME_HEIGHT].min;
	fsize->stepwise.max_height = inst->cap[FRAME_HEIGHT].max;
	fsize->stepwise.step_height = inst->cap[FRAME_HEIGHT].step_or_mask;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_enum_frameintervals(struct file *filp, void *fh,
				    struct v4l2_frmivalenum *fival)

{
	enum colorformat_type colorfmt;
	struct iris_inst *inst;
	struct iris_core *core;
	u32 fps, mbpf;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !fival)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain == DECODER) {
		ret = -ENOTTY;
		goto unlock;
	}

	core = inst->core;

	if (fival->index) {
		ret = -EINVAL;
		goto unlock;
	}

	colorfmt = v4l2_colorformat_to_driver(inst, fival->pixel_format);
	if (colorfmt == FMT_NONE) {
		ret = -EINVAL;
		goto unlock;
	}

	if (fival->width > inst->cap[FRAME_WIDTH].max ||
	    fival->width < inst->cap[FRAME_WIDTH].min ||
	    fival->height > inst->cap[FRAME_HEIGHT].max ||
	    fival->height < inst->cap[FRAME_HEIGHT].min) {
		ret = -EINVAL;
		goto unlock;
	}

	mbpf = NUM_MBS_PER_FRAME(fival->height, fival->width);
	fps = core->cap[MAX_MBPS].value / mbpf;

	fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	fival->stepwise.min.numerator = 1;
	fival->stepwise.min.denominator = 1;

	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator =
			min_t(u32, fps, inst->cap[FRAME_RATE].max);
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = inst->cap[FRAME_RATE].max;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_reqbufs(struct file *filp, void *fh, struct v4l2_requestbuffers *b)
{
	struct vb2_queue *vb2q = NULL;
	struct iris_inst *inst;
	int ret;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !b)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (!allow_reqbufs(inst, b->type)) {
		ret = -EBUSY;
		goto unlock;
	}

	vb2q = get_vb2q(inst, b->type);
	if (!vb2q) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = vb2_reqbufs(vb2q, b);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_querybuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct vb2_queue *vb2q = NULL;
	struct iris_inst *inst;
	int ret;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !b)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	vb2q = get_vb2q(inst, b->type);
	if (!vb2q) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = vb2_querybuf(vb2q, b);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_create_bufs(struct file *filp, void *fh, struct v4l2_create_buffers *b)
{
	struct iris_inst *inst;
	struct vb2_queue *vb2q;
	struct v4l2_format *f;
	int ret;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !b)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	f = &b->format;
	vb2q = get_vb2q(inst, f->type);
	if (!vb2q) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = vb2_create_bufs(vb2q, b);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_prepare_buf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev;
	struct iris_inst *inst;
	struct vb2_queue *vb2q;
	int ret;

	inst = get_vidc_inst(filp, fh);
	vdev = video_devdata(filp);
	if (!inst || !vdev || !vdev->v4l2_dev->mdev)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	vb2q = get_vb2q(inst, b->type);
	if (!vb2q) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = vb2_prepare_buf(vb2q, vdev->v4l2_dev->mdev, b);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_qbuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vdev;
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	vdev = video_devdata(filp);
	if (!inst || !b)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (b->type == INPUT_MPLANE)
		ret = vb2_qbuf(inst->vb2q_src, vdev->v4l2_dev->mdev, b);
	else if (b->type == OUTPUT_MPLANE)
		ret = vb2_qbuf(inst->vb2q_dst, vdev->v4l2_dev->mdev, b);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_dqbuf(struct file *filp, void *fh, struct v4l2_buffer *b)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !b)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (b->type == INPUT_MPLANE)
		ret = vb2_dqbuf(inst->vb2q_src, b, true);
	else if (b->type == OUTPUT_MPLANE)
		ret = vb2_dqbuf(inst->vb2q_dst, b, true);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_streamon(struct file *filp, void *fh, enum v4l2_buf_type type)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!allow_streamon(inst, type)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (type == INPUT_MPLANE)
		ret = vb2_streamon(inst->vb2q_src, type);
	else if (type == OUTPUT_MPLANE)
		ret = vb2_streamon(inst->vb2q_dst, type);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_streamoff(struct file *filp, void *fh, enum v4l2_buf_type type)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!allow_streamoff(inst, type)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (type == INPUT_MPLANE)
		ret = vb2_streamoff(inst->vb2q_src, type);
	else if (type == OUTPUT_MPLANE)
		ret = vb2_streamoff(inst->vb2q_dst, type);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_querycap(struct file *filp, void *fh, struct v4l2_capability *cap)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	strscpy(cap->driver, VIDC_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, VIDC_BUS_NAME, sizeof(cap->bus_info));
	memset(cap->reserved, 0, sizeof(cap->reserved));

	if (inst->domain == DECODER)
		strscpy(cap->card, "iris_decoder", sizeof(cap->card));
	else if (inst->domain == ENCODER)
		strscpy(cap->card, "iris_encoder", sizeof(cap->card));

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_queryctrl(struct file *filp, void *fh, struct v4l2_queryctrl *q_ctrl)
{
	struct v4l2_ctrl *ctrl;
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !q_ctrl)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	ctrl = v4l2_ctrl_find(&inst->ctrl_handler, q_ctrl->id);
	if (!ctrl) {
		ret = -EINVAL;
		goto unlock;
	}

	q_ctrl->minimum = ctrl->minimum;
	q_ctrl->maximum = ctrl->maximum;
	q_ctrl->default_value = ctrl->default_value;
	q_ctrl->flags = 0;
	q_ctrl->step = ctrl->step;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_querymenu(struct file *filp, void *fh, struct v4l2_querymenu *qmenu)
{
	struct v4l2_ctrl *ctrl;
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !qmenu)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	ctrl = v4l2_ctrl_find(&inst->ctrl_handler, qmenu->id);
	if (!ctrl) {
		ret = -EINVAL;
		goto unlock;
	}

	if (ctrl->type != V4L2_CTRL_TYPE_MENU) {
		ret = -EINVAL;
		goto unlock;
	}

	if (qmenu->index < ctrl->minimum || qmenu->index > ctrl->maximum) {
		ret = -EINVAL;
		goto unlock;
	}

	if (ctrl->menu_skip_mask & (1 << qmenu->index)) {
		ret = -EINVAL;
		goto unlock;
	}

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = container_of(fh, struct iris_inst, fh);

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain == DECODER)
		ret = vdec_subscribe_event(inst, sub);
	else if (inst->domain == ENCODER)
		ret = venc_subscribe_event(inst, sub);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_unsubscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	struct iris_inst *inst;
	int ret;

	inst = container_of(fh, struct iris_inst, fh);

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = v4l2_event_unsubscribe(&inst->fh, sub);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_g_selection(struct file *filp, void *fh, struct v4l2_selection *s)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !s)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (s->type != OUTPUT_MPLANE && s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    inst->domain == DECODER) {
		ret = -EINVAL;
		goto unlock;
	}

	if (s->type != INPUT_MPLANE && s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    inst->domain == ENCODER) {
		ret = -EINVAL;
		goto unlock;
	}

	if (inst->domain == DECODER) {
		switch (s->target) {
		case V4L2_SEL_TGT_CROP_BOUNDS:
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		case V4L2_SEL_TGT_COMPOSE_PADDED:
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE:
			s->r.left = inst->crop.left;
			s->r.top = inst->crop.top;
			s->r.width = inst->crop.width;
			s->r.height = inst->crop.height;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else if (inst->domain == ENCODER) {
		switch (s->target) {
		case V4L2_SEL_TGT_CROP_BOUNDS:
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP:
			s->r.left = inst->crop.left;
			s->r.top = inst->crop.top;
			s->r.width = inst->crop.width;
			s->r.height = inst->crop.height;
			break;
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		case V4L2_SEL_TGT_COMPOSE_PADDED:
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE:
			s->r.left = inst->compose.left;
			s->r.top = inst->compose.top;
			s->r.width = inst->compose.width;
			s->r.height = inst->compose.height;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_s_selection(struct file *filp, void *fh, struct v4l2_selection *s)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !s)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}
	if (inst->domain == DECODER) {
		ret = -EINVAL;
		goto unlock;
	} else if (inst->domain == ENCODER) {
		ret = venc_s_selection(inst, s);
	}

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_s_parm(struct file *filp, void *fh, struct v4l2_streamparm *a)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !a)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto unlock;
	}

	if (inst->domain == ENCODER)
		ret = venc_s_param(inst, a);
	else
		ret = -EINVAL;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_g_parm(struct file *filp, void *fh, struct v4l2_streamparm *a)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !a)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto unlock;
	}

	if (inst->domain == ENCODER)
		ret = venc_g_param(inst, a);
	else
		ret = -EINVAL;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_try_dec_cmd(struct file *filp, void *fh,
			    struct v4l2_decoder_cmd *dec)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !dec)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (dec->cmd != V4L2_DEC_CMD_STOP && dec->cmd != V4L2_DEC_CMD_START) {
		ret = -EINVAL;
		goto unlock;
	}
	dec->flags = 0;
	if (dec->cmd == V4L2_DEC_CMD_STOP) {
		dec->stop.pts = 0;
	} else if (dec->cmd == V4L2_DEC_CMD_START) {
		dec->start.speed = 0;
		dec->start.format = V4L2_DEC_START_FMT_NONE;
	}

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_try_enc_cmd(struct file *filp, void *fh,
			    struct v4l2_encoder_cmd *enc)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !enc)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain != ENCODER) {
		ret = -ENOTTY;
		goto unlock;
	}

	if (enc->cmd != V4L2_ENC_CMD_STOP && enc->cmd != V4L2_ENC_CMD_START) {
		ret = -EINVAL;
		goto unlock;
	}
	enc->flags = 0;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_dec_cmd(struct file *filp, void *fh,
			struct v4l2_decoder_cmd *dec)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !dec)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (dec->cmd != V4L2_DEC_CMD_START &&
	    dec->cmd != V4L2_DEC_CMD_STOP) {
		ret = -EINVAL;
		goto unlock;
	}

	if (inst->state == IRIS_INST_OPEN)
		goto unlock;

	if (!allow_cmd(inst, dec->cmd)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = iris_pm_get(inst->core);
	if (ret)
		goto unlock;

	if (dec->cmd == V4L2_DEC_CMD_START)
		ret = vdec_start_cmd(inst);
	else if (dec->cmd == V4L2_DEC_CMD_STOP)
		ret = vdec_stop_cmd(inst);

	iris_pm_put(inst->core, true);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int vidc_enc_cmd(struct file *filp, void *fh,
			struct v4l2_encoder_cmd *enc)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = get_vidc_inst(filp, fh);
	if (!inst || !enc)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (IS_SESSION_ERROR(inst)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (inst->domain != ENCODER) {
		ret = -ENOTTY;
		goto unlock;
	}

	if (enc->cmd != V4L2_ENC_CMD_START &&
	    enc->cmd != V4L2_ENC_CMD_STOP) {
		ret = -EINVAL;
		goto unlock;
	}

	if (enc->cmd == V4L2_ENC_CMD_STOP && inst->state == IRIS_INST_OPEN) {
		ret = 0;
		goto unlock;
	}

	if (!allow_cmd(inst, enc->cmd)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = iris_pm_get(inst->core);
	if (ret)
		goto unlock;

	if (enc->cmd == V4L2_ENC_CMD_START)
		ret = venc_start_cmd(inst);
	else if (enc->cmd == V4L2_ENC_CMD_STOP)
		ret = venc_stop_cmd(inst);

	iris_pm_put(inst->core, true);

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static struct v4l2_file_operations v4l2_file_ops = {
	.owner                          = THIS_MODULE,
	.open                           = vidc_open,
	.release                        = vidc_close,
	.unlocked_ioctl                 = video_ioctl2,
	.poll                           = vidc_poll,
};

static const struct vb2_ops iris_vb2_ops = {
	.queue_setup                    = iris_vb2_queue_setup,
	.start_streaming                = iris_vb2_start_streaming,
	.stop_streaming                 = iris_vb2_stop_streaming,
	.buf_queue                      = iris_vb2_buf_queue,
};

static struct vb2_mem_ops iris_vb2_mem_ops = {
	.alloc                          = iris_vb2_alloc,
	.put                            = iris_vb2_put,
	.mmap                           = iris_vb2_mmap,
	.attach_dmabuf                  = iris_vb2_attach_dmabuf,
	.detach_dmabuf                  = iris_vb2_detach_dmabuf,
	.map_dmabuf                     = iris_vb2_map_dmabuf,
	.unmap_dmabuf                   = iris_vb2_unmap_dmabuf,
};

static const struct v4l2_ioctl_ops v4l2_ioctl_ops_dec = {
	.vidioc_enum_fmt_vid_cap        = vidc_enum_fmt,
	.vidioc_enum_fmt_vid_out        = vidc_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane  = vidc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane  = vidc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane    = vidc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane    = vidc_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane    = vidc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane    = vidc_g_fmt,
	.vidioc_enum_framesizes         = vidc_enum_framesizes,
	.vidioc_reqbufs                 = vidc_reqbufs,
	.vidioc_querybuf                = vidc_querybuf,
	.vidioc_create_bufs             = vidc_create_bufs,
	.vidioc_prepare_buf             = vidc_prepare_buf,
	.vidioc_qbuf                    = vidc_qbuf,
	.vidioc_dqbuf                   = vidc_dqbuf,
	.vidioc_streamon                = vidc_streamon,
	.vidioc_streamoff               = vidc_streamoff,
	.vidioc_querycap                = vidc_querycap,
	.vidioc_queryctrl               = vidc_queryctrl,
	.vidioc_querymenu               = vidc_querymenu,
	.vidioc_subscribe_event         = vidc_subscribe_event,
	.vidioc_unsubscribe_event       = vidc_unsubscribe_event,
	.vidioc_g_selection             = vidc_g_selection,
	.vidioc_try_decoder_cmd         = vidc_try_dec_cmd,
	.vidioc_decoder_cmd             = vidc_dec_cmd,
};

static const struct v4l2_ioctl_ops v4l2_ioctl_ops_enc = {
	.vidioc_enum_fmt_vid_cap        = vidc_enum_fmt,
	.vidioc_enum_fmt_vid_out        = vidc_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane  = vidc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane  = vidc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane    = vidc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane    = vidc_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane    = vidc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane    = vidc_g_fmt,
	.vidioc_enum_framesizes         = vidc_enum_framesizes,
	.vidioc_enum_frameintervals     = vidc_enum_frameintervals,
	.vidioc_reqbufs                 = vidc_reqbufs,
	.vidioc_querybuf                = vidc_querybuf,
	.vidioc_create_bufs             = vidc_create_bufs,
	.vidioc_prepare_buf             = vidc_prepare_buf,
	.vidioc_qbuf                    = vidc_qbuf,
	.vidioc_dqbuf                   = vidc_dqbuf,
	.vidioc_streamon                = vidc_streamon,
	.vidioc_streamoff               = vidc_streamoff,
	.vidioc_querycap                = vidc_querycap,
	.vidioc_queryctrl               = vidc_queryctrl,
	.vidioc_querymenu               = vidc_querymenu,
	.vidioc_subscribe_event         = vidc_subscribe_event,
	.vidioc_unsubscribe_event       = vidc_unsubscribe_event,
	.vidioc_g_selection             = vidc_g_selection,
	.vidioc_s_selection             = vidc_s_selection,
	.vidioc_s_parm                  = vidc_s_parm,
	.vidioc_g_parm                  = vidc_g_parm,
	.vidioc_try_encoder_cmd         = vidc_try_enc_cmd,
	.vidioc_encoder_cmd             = vidc_enc_cmd,
};

int init_ops(struct iris_core *core)
{
	core->v4l2_file_ops = &v4l2_file_ops;
	core->vb2_ops = &iris_vb2_ops;
	core->vb2_mem_ops = &iris_vb2_mem_ops;
	core->v4l2_ioctl_ops_dec = &v4l2_ioctl_ops_dec;
	core->v4l2_ioctl_ops_enc = &v4l2_ioctl_ops_enc;
	return 0;
}
