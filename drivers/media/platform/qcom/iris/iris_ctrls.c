// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/list.h>

#include "platform_common.h"
#include "iris_helpers.h"
#include "iris_hfi_packet.h"
#include "iris_hfi.h"
#include "iris_ctrls.h"

#define MIN_CAPTURE_BUFFERS 4
#define MIN_OUTPUT_BUFFERS 4

static inline bool is_all_childrens_visited(struct plat_inst_cap *cap,
					    bool lookup[INST_CAP_MAX])
{
	bool found = true;
	int i;

	for (i = 0; i < MAX_NUM_CHILD; i++) {
		if (cap->children[i] == INST_CAP_NONE)
			continue;

		if (!lookup[cap->children[i]]) {
			found = false;
			break;
		}
	}

	return found;
}

static bool is_valid_cap_id(enum plat_inst_cap_type cap_id)
{
	return cap_id > INST_CAP_NONE && cap_id < INST_CAP_MAX;
}

static enum plat_inst_cap_type get_cap_id(struct iris_inst *inst, u32 id)
{
	enum plat_inst_cap_type iter = INST_CAP_NONE;
	enum plat_inst_cap_type cap_id = INST_CAP_NONE;

	do {
		if (inst->cap[iter].v4l2_id == id) {
			cap_id = inst->cap[iter].cap_id;
			break;
		}
		iter++;
	} while (iter < INST_CAP_MAX);

	return cap_id;
}

static int add_node_list(struct list_head *list, enum plat_inst_cap_type cap_id)
{
	struct cap_entry *entry = NULL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->list);
	entry->cap_id = cap_id;
	list_add(&entry->list, list);

	return 0;
}

static int add_children(struct list_head *list,
			struct iris_inst *inst,
			enum plat_inst_cap_type cap_id)
{
	struct plat_inst_cap *cap;
	int i, ret = 0;

	cap = &inst->cap[cap_id];

	for (i = 0; i < MAX_NUM_CHILD; i++) {
		if (!cap->children[i])
			break;

		if (!is_valid_cap_id(cap->children[i]))
			continue;

		ret = add_node_list(list, cap->children[i]);
		if (ret)
			return ret;
	}

	return ret;
}

static int adjust_cap(struct iris_inst *inst,
		      enum plat_inst_cap_type cap_id,
		      struct v4l2_ctrl *ctrl)
{
	struct plat_inst_cap *cap;

	cap = &inst->cap[cap_id];
	if (!inst->cap[cap_id].cap_id)
		return 0;

	if (!cap->adjust) {
		if (ctrl)
			inst->cap[cap_id].value = ctrl->val;
		return 0;
	}

	return cap->adjust(inst, ctrl);
}

static int set_cap(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	struct plat_inst_cap *cap;

	cap = &inst->cap[cap_id];
	if (!inst->cap[cap_id].cap_id)
		return 0;

	if (!cap->set)
		return 0;

	return cap->set(inst, cap_id);
}

static int adjust_dynamic_property(struct iris_inst *inst,
				   enum plat_inst_cap_type cap_id,
				   struct v4l2_ctrl *ctrl,
				   struct list_head *firmware_list,
				   struct list_head *children_list,
				   bool cap_present[INST_CAP_MAX])
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap;
	s32 prev_value;
	int ret;

	cap = &inst->cap[0];

	if (!(cap[cap_id].flags & CAP_FLAG_DYNAMIC_ALLOWED))
		return -EBUSY;

	prev_value = cap[cap_id].value;
	ret = adjust_cap(inst, cap_id, ctrl);
	if (ret)
		return ret;

	ret = add_node_list(firmware_list, cap_id);
	if (ret)
		return ret;
	cap_present[cap->cap_id] = true;

	if (cap[cap_id].value == prev_value)
		return 0;

	ret = add_children(children_list, inst, cap_id);
	if (ret)
		return ret;

	list_for_each_entry_safe(entry, temp, children_list, list) {
		if (!cap[entry->cap_id].adjust) {
			list_del_init(&entry->list);
			kfree(entry);
			continue;
		}

		prev_value = cap[entry->cap_id].value;
		ret = adjust_cap(inst, entry->cap_id, NULL);
		if (ret)
			return ret;

		if (cap[entry->cap_id].value != prev_value) {
			if (!cap_present[cap->cap_id]) {
				ret = add_node_list(firmware_list, cap_id);
				if (ret)
					return ret;
				cap_present[cap->cap_id] = true;
			}

			ret = add_children(children_list, inst, entry->cap_id);
			if (ret)
				return ret;
		}

		list_del_init(&entry->list);
		kfree(entry);
	}

	if (!list_empty(children_list))
		return -EINVAL;

	return ret;
}

static int set_dynamic_property(struct iris_inst *inst,
				struct list_head *firmware_list)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap;
	int ret = 0;

	list_for_each_entry_safe(entry, temp, firmware_list, list) {
		cap = &inst->cap[entry->cap_id];
		if (cap->set) {
			ret = cap->set(inst, entry->cap_id);
			if (ret)
				return -EINVAL;
		}
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

static int iris_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	enum plat_inst_cap_type cap_id;
	struct iris_inst *inst = NULL;
	int ret = 0;

	inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctrl->val = inst->buffers.output.min_count;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ctrl->val = inst->buffers.input.min_count;
		break;
	default:
		cap_id = get_cap_id(inst, ctrl->id);
		if (is_valid_cap_id(cap_id))
			ctrl->val = inst->cap[cap_id].value;
		else
			ret = -EINVAL;
	}

	return ret;
}

static int iris_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct list_head children_list, firmware_list;
	struct ctrl_data *priv_ctrl_data;
	enum plat_inst_cap_type cap_id;
	bool cap_present[INST_CAP_MAX];
	struct plat_inst_cap *cap;
	struct iris_inst *inst;
	int ret = 0;

	priv_ctrl_data = ctrl->priv ? ctrl->priv : NULL;
	if (priv_ctrl_data && priv_ctrl_data->skip_s_ctrl)
		return 0;

	inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	cap = &inst->cap[0];

	INIT_LIST_HEAD(&firmware_list);
	INIT_LIST_HEAD(&children_list);
	memset(&cap_present, 0, sizeof(cap_present));

	cap_id = get_cap_id(inst, ctrl->id);
	if (!is_valid_cap_id(cap_id))
		return -EINVAL;

	if (!allow_s_ctrl(inst, cap_id))
		return -EBUSY;

	cap[cap_id].flags |= CAP_FLAG_CLIENT_SET;

	if ((inst->domain == ENCODER && !inst->vb2q_dst->streaming) ||
	    (inst->domain == DECODER && !inst->vb2q_src->streaming)) {
		inst->cap[cap_id].value = ctrl->val;
	} else {
		ret = adjust_dynamic_property(inst, cap_id, ctrl,
					      &firmware_list, &children_list,
					      cap_present);
		if (ret)
			goto free_list;

		ret = set_dynamic_property(inst, &firmware_list);
	}

free_list:
	list_for_each_entry_safe(entry, temp, &children_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	list_for_each_entry_safe(entry, temp, &firmware_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = iris_op_s_ctrl,
	.g_volatile_ctrl = iris_op_g_volatile_ctrl,
};

int ctrls_init(struct iris_inst *inst, bool init)
{
	int num_ctrls = 0, ctrl_idx = 0;
	u64 codecs_count, step_or_mask;
	struct plat_inst_cap *cap;
	struct iris_core *core;
	int idx = 0;
	int ret = 0;

	core = inst->core;
	cap = &inst->cap[0];

	for (idx = 0; idx < INST_CAP_MAX; idx++) {
		if (cap[idx].v4l2_id)
			num_ctrls++;
	}
	if (!num_ctrls)
		return -EINVAL;

	if (init) {
		codecs_count = inst->domain == ENCODER ?
			core->enc_codecs_count :
			core->dec_codecs_count;
		ret = v4l2_ctrl_handler_init(&inst->ctrl_handler,
					     INST_CAP_MAX * codecs_count);
		if (ret)
			return ret;
	}

	for (idx = 0; idx < INST_CAP_MAX; idx++) {
		struct v4l2_ctrl *ctrl;

		if (!cap[idx].v4l2_id)
			continue;

		if (ctrl_idx >= num_ctrls) {
			ret = -EINVAL;
			goto error;
		}

		if (!init) {
			struct ctrl_data ctrl_priv_data;

			ctrl = v4l2_ctrl_find(&inst->ctrl_handler, cap[idx].v4l2_id);
			if (ctrl) {
				step_or_mask = (cap[idx].flags & CAP_FLAG_MENU) ?
					~(cap[idx].step_or_mask) :
					cap[idx].step_or_mask;
				memset(&ctrl_priv_data, 0, sizeof(ctrl_priv_data));
				ctrl_priv_data.skip_s_ctrl = true;
				ctrl->priv = &ctrl_priv_data;
				v4l2_ctrl_modify_range(ctrl,
						       cap[idx].min,
						       cap[idx].max,
						       step_or_mask,
						       cap[idx].value);
				ctrl->priv = NULL;
				continue;
			}
		}

		if (cap[idx].flags & CAP_FLAG_MENU) {
			ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler,
						      &ctrl_ops,
						      cap[idx].v4l2_id,
						      cap[idx].max,
						      ~(cap[idx].step_or_mask),
						      cap[idx].value);
		} else {
			ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler,
						 &ctrl_ops,
						 cap[idx].v4l2_id,
						 cap[idx].min,
						 cap[idx].max,
						 cap[idx].step_or_mask,
						 cap[idx].value);
		}
		if (!ctrl) {
			ret = -EINVAL;
			goto error;
		}

		ret = inst->ctrl_handler.error;
		if (ret)
			goto error;

		if ((cap[idx].flags & CAP_FLAG_VOLATILE) ||
		    (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE ||
		     ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_OUTPUT))
			ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

		ctrl->flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		ctrl_idx++;
	}
	inst->num_ctrls = num_ctrls;

	return 0;
error:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);

	return ret;
}

int iris_init_core_caps(struct iris_core *core)
{
	struct plat_core_cap *core_platform_data;
	int i, num_core_caps;

	core_platform_data = core->platform_data->core_data;
	if (!core_platform_data)
		return -EINVAL;

	num_core_caps = core->platform_data->core_data_size;

	for (i = 0; i < num_core_caps && i < CORE_CAP_MAX; i++) {
		core->cap[core_platform_data[i].type].type = core_platform_data[i].type;
		core->cap[core_platform_data[i].type].value = core_platform_data[i].value;
	}

	return 0;
}

static int update_inst_capability(struct plat_inst_cap *in,
				  struct plat_inst_caps *capability)
{
	if (!in || !capability)
		return -EINVAL;

	if (in->cap_id >= INST_CAP_MAX)
		return -EINVAL;

	capability->cap[in->cap_id].cap_id = in->cap_id;
	capability->cap[in->cap_id].min = in->min;
	capability->cap[in->cap_id].max = in->max;
	capability->cap[in->cap_id].step_or_mask = in->step_or_mask;
	capability->cap[in->cap_id].value = in->value;
	capability->cap[in->cap_id].flags = in->flags;
	capability->cap[in->cap_id].v4l2_id = in->v4l2_id;
	capability->cap[in->cap_id].hfi_id = in->hfi_id;
	memcpy(capability->cap[in->cap_id].children, in->children,
	       sizeof(capability->cap[in->cap_id].children));
	capability->cap[in->cap_id].adjust = in->adjust;
	capability->cap[in->cap_id].set = in->set;

	return 0;
}

int iris_init_instance_caps(struct iris_core *core)
{
	struct plat_inst_cap *inst_plat_cap_data = NULL;
	u8 enc_codecs_count = 0, dec_codecs_count = 0;
	u32 enc_valid_codecs, dec_valid_codecs;
	int i, j, check_bit = 0;
	u8 codecs_count = 0;
	int num_inst_cap;
	int ret = 0;

	inst_plat_cap_data = core->platform_data->inst_cap_data;
	if (!inst_plat_cap_data)
		return -EINVAL;

	enc_valid_codecs = core->cap[ENC_CODECS].value;
	enc_codecs_count = hweight32(enc_valid_codecs);
	core->enc_codecs_count = enc_codecs_count;

	dec_valid_codecs = core->cap[DEC_CODECS].value;
	dec_codecs_count = hweight32(dec_valid_codecs);
	core->dec_codecs_count = dec_codecs_count;

	codecs_count = enc_codecs_count + dec_codecs_count;
	core->inst_caps = devm_kzalloc(core->dev,
				       codecs_count * sizeof(struct plat_inst_caps),
				       GFP_KERNEL);
	if (!core->inst_caps)
		return -ENOMEM;

	for (i = 0; i < enc_codecs_count; i++) {
		while (check_bit < (sizeof(enc_valid_codecs) * 8)) {
			if (enc_valid_codecs & BIT(check_bit)) {
				core->inst_caps[i].domain = ENCODER;
				core->inst_caps[i].codec = enc_valid_codecs &
						BIT(check_bit);
				check_bit++;
				break;
			}
			check_bit++;
		}
	}

	check_bit = 0;

	for (; i < codecs_count; i++) {
		while (check_bit < (sizeof(dec_valid_codecs) * 8)) {
			if (dec_valid_codecs & BIT(check_bit)) {
				core->inst_caps[i].domain = DECODER;
				core->inst_caps[i].codec = dec_valid_codecs &
						BIT(check_bit);
				check_bit++;
				break;
			}
			check_bit++;
		}
	}

	num_inst_cap = core->platform_data->inst_cap_data_size;

	for (i = 0; i < num_inst_cap; i++) {
		for (j = 0; j < codecs_count; j++) {
			if ((inst_plat_cap_data[i].domain & core->inst_caps[j].domain) &&
			    (inst_plat_cap_data[i].codec & core->inst_caps[j].codec)) {
				ret = update_inst_capability(&inst_plat_cap_data[i],
							     &core->inst_caps[j]);
				if (ret)
					return ret;
			}
		}
	}

	return ret;
}

int get_inst_capability(struct iris_inst *inst)
{
	struct iris_core *core;
	u32 codecs_count = 0;
	int i;

	core = inst->core;

	codecs_count = core->enc_codecs_count + core->dec_codecs_count;

	for (i = 0; i < codecs_count; i++) {
		if (core->inst_caps[i].domain == inst->domain &&
			core->inst_caps[i].codec == inst->codec) {
			memcpy(&inst->cap[0], &core->inst_caps[i].cap[0],
			       (INST_CAP_MAX + 1) * sizeof(struct plat_inst_cap));
		}
	}

	return 0;
}

int prepare_dependency_list(struct iris_inst *inst)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap, *temp_cap;
	int caps_to_prepare, pending_list_counter,
		pending_at_start = 0;
	struct list_head prepared_list, pending_list;
	bool is_prepared[INST_CAP_MAX];
	bool is_pending[INST_CAP_MAX];
	int i, ret = 0;

	cap = &inst->cap[0];

	if (!list_empty(&inst->caps_list))
		return 0;

	INIT_LIST_HEAD(&prepared_list);
	INIT_LIST_HEAD(&pending_list);
	memset(&is_prepared, 0, sizeof(is_prepared));
	memset(&is_pending, 0, sizeof(is_pending));

	for (i = 1; i < INST_CAP_MAX; i++) {
		temp_cap = &cap[i];
		if (!is_valid_cap_id(temp_cap->cap_id))
			continue;

		if (!temp_cap->children[0]) {
			if (!is_prepared[temp_cap->cap_id]) {
				ret = add_node_list(&prepared_list, temp_cap->cap_id);
				if (ret)
					goto free_list;
				is_prepared[temp_cap->cap_id] = true;
			}
		} else {
			if (!is_pending[temp_cap->cap_id]) {
				ret = add_node_list(&pending_list, temp_cap->cap_id);
				if (ret)
					goto free_list;
				is_pending[temp_cap->cap_id] = true;
			}
		}
	}

	list_for_each_entry(entry, &pending_list, list)
		pending_at_start++;

	caps_to_prepare = pending_at_start;
	pending_list_counter = pending_at_start;

	list_for_each_entry_safe(entry, temp, &pending_list, list) {
		list_del_init(&entry->list);
		is_pending[entry->cap_id] = false;
		pending_list_counter--;
		temp_cap = &cap[entry->cap_id];

		if (is_all_childrens_visited(temp_cap, is_prepared)) {
			list_add(&entry->list, &prepared_list);
			is_prepared[entry->cap_id] = true;
			caps_to_prepare--;
		} else {
			list_add_tail(&entry->list, &pending_list);
			is_pending[entry->cap_id] = true;
		}

		if (!pending_list_counter) {
			if (pending_at_start == caps_to_prepare) {
				ret = -EINVAL;
				goto free_list;
			}
			pending_at_start = caps_to_prepare;
			pending_list_counter = caps_to_prepare;
		}
	}

	if (!list_empty(&pending_list)) {
		ret = -EINVAL;
		goto free_list;
	}

	list_replace_init(&prepared_list, &inst->caps_list);

free_list:
	list_for_each_entry_safe(entry, temp, &pending_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}
	list_for_each_entry_safe(entry, temp, &prepared_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

static inline bool is_layer_bitrate_set(struct iris_inst *inst)
{
	u32 layer_br_caps[6] = {L0_BR, L1_BR, L2_BR, L3_BR, L4_BR, L5_BR};
	u32 cap_id = 0, i, enh_layer_count;

	enh_layer_count = inst->cap[ENH_LAYER_COUNT].value;

	for (i = 0; i <= enh_layer_count; i++) {
		if (i >= ARRAY_SIZE(layer_br_caps))
			break;

		cap_id = layer_br_caps[i];
		if (!(inst->cap[cap_id].flags & CAP_FLAG_CLIENT_SET))
			return false;
	}

	return true;
}

static inline u32 get_cumulative_bitrate(struct iris_inst *inst)
{
	u32 layer_br_caps[6] = {L0_BR, L1_BR, L2_BR, L3_BR, L4_BR, L5_BR};
	u32 cumulative_br = 0;
	s32 enh_layer_count;
	u32 cap_id = 0;
	int i;

	enh_layer_count = inst->cap[ENH_LAYER_COUNT].value;

	for (i = 0; i <= enh_layer_count; i++) {
		if (i >= ARRAY_SIZE(layer_br_caps))
			break;
		cap_id = layer_br_caps[i];
		cumulative_br += inst->cap[cap_id].value;
	}

	return cumulative_br;
}

int set_u32_enum(struct iris_inst *inst,
		 enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_value, sizeof(u32));
}

int set_u32(struct iris_inst *inst,
	    enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_value, sizeof(u32));
}

int set_q16(struct iris_inst *inst,
	    enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_Q16,
				     &hfi_value, sizeof(u32));
}

int set_stage(struct iris_inst *inst,
	      enum plat_inst_cap_type cap_id)
{
	struct v4l2_format *inp_f;
	u32 work_mode = STAGE_2;
	u32 width, height;
	u32 hfi_id;

	hfi_id = inst->cap[cap_id].hfi_id;

	if (inst->domain == DECODER) {
		inp_f = inst->fmt_src;
		height = inp_f->fmt.pix_mp.height;
		width = inp_f->fmt.pix_mp.width;
		if (res_is_less_than(width, height, 1280, 720))
			work_mode = STAGE_1;
	} else if (inst->domain == ENCODER) {
		if (inst->cap[SLICE_MODE].value ==
		    V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES)
			work_mode = STAGE_1;

		if (inst->hfi_rc_type == HFI_RC_CBR_CFR ||
		    inst->hfi_rc_type == HFI_RC_CBR_VFR)
			work_mode = STAGE_1;

		if (!inst->cap[GOP_SIZE].value)
			work_mode = STAGE_2;
	}

	inst->cap[cap_id].value = work_mode;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &work_mode, sizeof(u32));
}

int set_pipe(struct iris_inst *inst,
	     enum plat_inst_cap_type cap_id)
{
	u32 work_route, hfi_id;

	work_route = inst->cap[cap_id].value;
	hfi_id = inst->cap[cap_id].hfi_id;

	if (inst->domain == ENCODER) {
		if (inst->cap[SLICE_MODE].value ==
		    V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES)
			work_route = PIPE_1;
	}

	inst->cap[cap_id].value = work_route;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &work_route, sizeof(u32));
}

int set_level(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	if (!(inst->cap[cap_id].flags & CAP_FLAG_CLIENT_SET))
		hfi_value = HFI_LEVEL_NONE;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
					 get_port_info(inst, cap_id),
					 HFI_PAYLOAD_U32_ENUM,
					 &hfi_value, sizeof(u32));
}

int decide_quality_mode(struct iris_inst *inst)
{
	u32 fps, mbpf, mbps, max_hq_mbpf, max_hq_mbps;
	u32 mode = POWER_SAVE_MODE;
	struct iris_core *core;

	if (inst->domain != ENCODER)
		return 0;

	mbpf = NUM_MBS_PER_FRAME(inst->crop.height, inst->crop.width);
	fps = max3(inst->cap[QUEUED_RATE].value >> 16,
		   inst->cap[FRAME_RATE].value >> 16,
		   inst->cap[OPERATING_RATE].value >> 16);
	mbps = mbpf * fps;
	core = inst->core;
	max_hq_mbpf = core->cap[MAX_MBPF_HQ].value;
	max_hq_mbps = core->cap[MAX_MBPS_HQ].value;

	if (mbpf <= max_hq_mbpf && mbps <= max_hq_mbps)
		mode = MAX_QUALITY_MODE;

	inst->cap[QUALITY_MODE].value = mode;

	return mode;
}

int set_req_sync_frame(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id, hfi_val;
	s32 prepend_sps_pps;

	prepend_sps_pps = inst->cap[PREPEND_SPSPPS_TO_IDR].value;
	hfi_id = inst->cap[cap_id].hfi_id;

	if (prepend_sps_pps)
		hfi_val = HFI_SYNC_FRAME_REQUEST_WITH_PREFIX_SEQ_HDR;
	else
		hfi_val = HFI_SYNC_FRAME_REQUEST_WITHOUT_SEQ_HDR;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_val, sizeof(u32));
}

int set_flip(struct iris_inst *inst,
	     enum plat_inst_cap_type cap_id)
{
	u32 hflip, vflip, ret = 0;

	u32 hfi_value = HFI_DISABLE_FLIP;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	hflip = inst->cap[HFLIP].value;
	vflip = inst->cap[VFLIP].value;

	if (hflip)
		hfi_value |= HFI_HORIZONTAL_FLIP;

	if (vflip)
		hfi_value |= HFI_VERTICAL_FLIP;

	if (inst->vb2q_dst->streaming) {
		if (hfi_value != HFI_DISABLE_FLIP) {
			ret = set_req_sync_frame(inst, REQUEST_I_FRAME);
			if (ret)
				return ret;
		}
	}

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_value, sizeof(u32));
}

int set_rotation(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id, hfi_val;

	hfi_id = inst->cap[cap_id].hfi_id;
	v4l2_to_hfi_enum(inst, cap_id, &hfi_val);

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int set_header_mode(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 header_mode, hfi_id, hfi_val;
	s32 prepend_sps_pps;

	prepend_sps_pps = inst->cap[PREPEND_SPSPPS_TO_IDR].value;
	header_mode = inst->cap[cap_id].value;
	hfi_id = inst->cap[cap_id].hfi_id;

	if (prepend_sps_pps)
		hfi_val = HFI_SEQ_HEADER_PREFIX_WITH_SYNC_FRAME;
	else if (header_mode == V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME)
		hfi_val = HFI_SEQ_HEADER_JOINED_WITH_1ST_FRAME;
	else
		hfi_val = HFI_SEQ_HEADER_SEPERATE_FRAME;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_val, sizeof(u32));
}

int set_gop_size(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_value, hfi_id;

	if (inst->vb2q_dst->streaming) {
		if (inst->hfi_layer_type == HFI_HIER_B)
			return 0;
	}

	hfi_value = inst->cap[GOP_SIZE].value;
	hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_value, sizeof(u32));
}

int set_bitrate(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id, hfi_val;

	if (inst->cap[BIT_RATE].flags & CAP_FLAG_CLIENT_SET)
		goto set_total_bitrate;

	if (inst->vb2q_dst->streaming)
		return 0;

set_total_bitrate:
	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_val = inst->cap[cap_id].value;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int set_layer_bitrate(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = 0;
	u32 hfi_id;

	if (!inst->vb2q_dst->streaming)
		return 0;

	if (inst->cap[BIT_RATE].flags & CAP_FLAG_CLIENT_SET)
		return 0;

	if (!inst->cap[ENH_LAYER_COUNT].max ||
	    !is_layer_bitrate_set(inst))
		return 0;

	hfi_value = inst->cap[BIT_RATE].value;
	hfi_id = inst->cap[BIT_RATE].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_value, sizeof(u32));
}

int set_peak_bitrate(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id, hfi_val;
	s32 rc_mode;

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_val = inst->cap[cap_id].value;

	rc_mode = inst->cap[BITRATE_MODE].value;
	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		return 0;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int set_bitrate_mode(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_value, hfi_id;

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_value = inst->hfi_rc_type;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
			get_port_info(inst, cap_id),
			HFI_PAYLOAD_U32_ENUM,
			&hfi_value, sizeof(u32));
}

int set_use_and_mark_ltr(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id, hfi_val;

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_val = inst->cap[cap_id].value;

	if (!inst->cap[LTR_COUNT].value ||
	    inst->cap[cap_id].value == INVALID_DEFAULT_MARK_OR_USE_LTR)
		return 0;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_val, sizeof(u32));
}

int set_ir_period(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_id = 0, hfi_val;

	hfi_val = inst->cap[cap_id].value;

	if (inst->cap[IR_TYPE].value ==
	    V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_RANDOM) {
		hfi_id = HFI_PROP_IR_RANDOM_PERIOD;
	} else if (inst->cap[IR_TYPE].value ==
		   V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC) {
		hfi_id = HFI_PROP_IR_CYCLIC_PERIOD;
	}

	return iris_hfi_set_ir_period(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				      get_port_info(inst, cap_id),
				      HFI_PAYLOAD_U32,
				      &hfi_val, sizeof(u32));
}

int set_min_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	s32 i_frame_qp = 0, p_frame_qp = 0, b_frame_qp = 0, min_qp_enable = 0;
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0;
	u32 client_qp_enable = 0, hfi_value = 0;
	u32 hfi_id;

	if (inst->cap[MIN_FRAME_QP].flags & CAP_FLAG_CLIENT_SET)
		min_qp_enable = 1;

	if (min_qp_enable ||
	    (inst->cap[I_FRAME_MIN_QP].flags & CAP_FLAG_CLIENT_SET))
		i_qp_enable = 1;
	if (min_qp_enable ||
	    (inst->cap[P_FRAME_MIN_QP].flags & CAP_FLAG_CLIENT_SET))
		p_qp_enable = 1;
	if (min_qp_enable ||
	    (inst->cap[B_FRAME_MIN_QP].flags & CAP_FLAG_CLIENT_SET))
		b_qp_enable = 1;

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	i_frame_qp = max(inst->cap[I_FRAME_MIN_QP].value, inst->cap[MIN_FRAME_QP].value);
	p_frame_qp = max(inst->cap[P_FRAME_MIN_QP].value, inst->cap[MIN_FRAME_QP].value);
	b_frame_qp = max(inst->cap[B_FRAME_MIN_QP].value, inst->cap[MIN_FRAME_QP].value);

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_value = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 |
		client_qp_enable << 24;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_value, sizeof(u32));
}

int set_max_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	s32 i_frame_qp = 0, p_frame_qp = 0, b_frame_qp = 0, max_qp_enable = 0;
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0;
	u32 client_qp_enable = 0, hfi_value = 0;
	u32 hfi_id;

	if (inst->cap[MAX_FRAME_QP].flags & CAP_FLAG_CLIENT_SET)
		max_qp_enable = 1;

	if (max_qp_enable ||
	    (inst->cap[I_FRAME_MAX_QP].flags & CAP_FLAG_CLIENT_SET))
		i_qp_enable = 1;
	if (max_qp_enable ||
	    (inst->cap[P_FRAME_MAX_QP].flags & CAP_FLAG_CLIENT_SET))
		p_qp_enable = 1;
	if (max_qp_enable ||
	    (inst->cap[B_FRAME_MAX_QP].flags & CAP_FLAG_CLIENT_SET))
		b_qp_enable = 1;

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	i_frame_qp = min(inst->cap[I_FRAME_MAX_QP].value, inst->cap[MAX_FRAME_QP].value);
	p_frame_qp = min(inst->cap[P_FRAME_MAX_QP].value, inst->cap[MAX_FRAME_QP].value);
	b_frame_qp = min(inst->cap[B_FRAME_MAX_QP].value, inst->cap[MAX_FRAME_QP].value);

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_value = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 |
		client_qp_enable << 24;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_value, sizeof(u32));
}

int set_frame_qp(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 i_qp_enable = 0, p_qp_enable = 0, b_qp_enable = 0;
	u32 client_qp_enable = 0, hfi_value = 0;
	s32 i_frame_qp = 0, p_frame_qp = 0, b_frame_qp = 0;
	s32 rc_type = -1;
	u32 hfi_id;

	rc_type = inst->hfi_rc_type;
	if (inst->vb2q_dst->streaming) {
		if (rc_type != HFI_RC_OFF)
			return 0;
	}

	if (rc_type == HFI_RC_OFF) {
		i_qp_enable = 1;
		p_qp_enable = 1;
		b_qp_enable = 1;
	} else {
		if (inst->cap[I_FRAME_QP].flags & CAP_FLAG_CLIENT_SET)
			i_qp_enable = 1;
		if (inst->cap[P_FRAME_QP].flags & CAP_FLAG_CLIENT_SET)
			p_qp_enable = 1;
		if (inst->cap[B_FRAME_QP].flags & CAP_FLAG_CLIENT_SET)
			b_qp_enable = 1;
	}

	client_qp_enable = i_qp_enable | p_qp_enable << 1 | b_qp_enable << 2;
	if (!client_qp_enable)
		return 0;

	i_frame_qp = inst->cap[I_FRAME_QP].value;
	p_frame_qp = inst->cap[P_FRAME_QP].value;
	b_frame_qp = inst->cap[B_FRAME_QP].value;

	hfi_id = inst->cap[cap_id].hfi_id;
	hfi_value = i_frame_qp | p_frame_qp << 8 | b_frame_qp << 16 |
		client_qp_enable << 24;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_32_PACKED,
				     &hfi_value, sizeof(u32));
}

int set_layer_count_and_type(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_layer_count, hfi_layer_type = 0;
	int ret, hfi_id;

	if (!inst->vb2q_dst->streaming) {
		hfi_layer_type = inst->hfi_layer_type;
		hfi_id = inst->cap[LAYER_TYPE].hfi_id;

		ret = iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
					    get_port_info(inst, LAYER_TYPE),
					    HFI_PAYLOAD_U32_ENUM,
					    &hfi_layer_type, sizeof(u32));
		if (ret)
			return ret;
	} else {
		if (inst->hfi_layer_type == HFI_HIER_B)
			return 0;
	}

	hfi_id = inst->cap[ENH_LAYER_COUNT].hfi_id;
	hfi_layer_count = inst->cap[ENH_LAYER_COUNT].value + 1;

	ret = iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				    get_port_info(inst, ENH_LAYER_COUNT),
				    HFI_PAYLOAD_U32,
				    &hfi_layer_count, sizeof(u32));

	return ret;
}

int set_slice_count(struct iris_inst *inst, enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = 0, set_cap_id = 0, hfi_id;
	s32 slice_mode = -1;

	slice_mode = inst->cap[SLICE_MODE].value;

	if (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE)
		return 0;

	if (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB) {
		hfi_value = (inst->codec == HEVC) ?
			(inst->cap[SLICE_MAX_MB].value + 3) / 4 :
			inst->cap[SLICE_MAX_MB].value;
		set_cap_id = SLICE_MAX_MB;
	} else if (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES) {
		hfi_value = inst->cap[SLICE_MAX_BYTES].value;
		set_cap_id = SLICE_MAX_BYTES;
	}

	hfi_id = inst->cap[set_cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, set_cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_value, sizeof(u32));
}

int set_v4l2_properties(struct iris_inst *inst)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	int ret = 0;

	list_for_each_entry_safe(entry, temp, &inst->caps_list, list) {
		ret = set_cap(inst, entry->cap_id);
		if (ret)
			return ret;
	}

	return ret;
}

int adjust_v4l2_properties(struct iris_inst *inst)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	int ret = 0;

	list_for_each_entry_safe(entry, temp, &inst->caps_list, list) {
		ret = adjust_cap(inst, entry->cap_id, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

int adjust_output_order(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 display_delay = -1, display_delay_enable = -1;
	u32 adjusted_value;

	adjusted_value = ctrl ? ctrl->val :
		inst->cap[OUTPUT_ORDER].value;

	display_delay = inst->cap[DISPLAY_DELAY].value;
	display_delay_enable = inst->cap[DISPLAY_DELAY_ENABLE].value;

	if (display_delay_enable && !display_delay)
		adjusted_value = 1;

	inst->cap[OUTPUT_ORDER].value = adjusted_value;

	return 0;
}

int adjust_profile(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	u32 adjusted_value;
	s32 pix_fmt = -1;

	adjusted_value = ctrl ? ctrl->val : inst->cap[PROFILE].value;

	pix_fmt = inst->cap[PIX_FMTS].value;

	if (pix_fmt == FMT_TP10C)
		adjusted_value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10;
	else
		adjusted_value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;

	inst->cap[PROFILE].value = adjusted_value;

	return 0;
}

int adjust_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	u32 layer_br_caps[6] = {L0_BR, L1_BR, L2_BR, L3_BR, L4_BR, L5_BR};
	u32 adjusted_value, cumulative_bitrate, cap_id, cap_val, i;
	s32 layer_count, max_bitrate = 0, entropy_mode;

	adjusted_value = ctrl ? ctrl->val : inst->cap[BIT_RATE].value;

	if (inst->cap[BIT_RATE].flags & CAP_FLAG_CLIENT_SET) {
		inst->cap[BIT_RATE].value = adjusted_value;
		return 0;
	}

	entropy_mode = inst->cap[ENTROPY_MODE].value;

	if (inst->codec == HEVC)
		max_bitrate = CABAC_MAX_BITRATE;

	if (inst->codec == H264) {
		if (entropy_mode == V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC)
			max_bitrate = CABAC_MAX_BITRATE;
		else
			max_bitrate = CAVLC_MAX_BITRATE;
	}

	if (inst->cap[BIT_RATE].value > max_bitrate)
		inst->cap[BIT_RATE].value = max_bitrate;

	layer_count = inst->cap[ENH_LAYER_COUNT].value;
	if (!layer_count)
		return 0;

	if (!is_layer_bitrate_set(inst))
		return 0;

	cumulative_bitrate = get_cumulative_bitrate(inst);

	if (cumulative_bitrate > max_bitrate) {
		u32 decrement_in_value = 0;
		u32 decrement_in_percent = ((cumulative_bitrate - max_bitrate) * 100) /
				max_bitrate;

		cumulative_bitrate = 0;
		for (i = 0; i <= layer_count; i++) {
			if (i >= ARRAY_SIZE(layer_br_caps))
				break;
			cap_id = layer_br_caps[i];
			cap_val = inst->cap[cap_id].value;
			decrement_in_value = (cap_val * decrement_in_percent) / 100;
			cumulative_bitrate += (cap_val - decrement_in_value);
			inst->cap[cap_id].value = cap_val - decrement_in_value;
		}
		inst->cap[BIT_RATE].value = cumulative_bitrate;
	}

	return 0;
}

int adjust_layer_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	u32 old_br = 0, new_br = 0, exceeded_br = 0;
	u32 client_set_cap_id = INST_CAP_NONE;
	u32 cumulative_bitrate = 0;
	s32 max_bitrate;

	if (!ctrl)
		return 0;

	if (inst->cap[BIT_RATE].flags & CAP_FLAG_CLIENT_SET ||
	    !inst->vb2q_dst->streaming)
		return 0;

	if (!inst->cap[ENH_LAYER_COUNT].max)
		return -EINVAL;

	if (!is_layer_bitrate_set(inst))
		return 0;

	client_set_cap_id = get_cap_id(inst, ctrl->id);
	if (!is_valid_cap_id(client_set_cap_id))
		return -EINVAL;

	cumulative_bitrate = get_cumulative_bitrate(inst);
	max_bitrate = inst->cap[BIT_RATE].max;
	old_br = inst->cap[client_set_cap_id].value;
	new_br = ctrl->val;

	if ((cumulative_bitrate - old_br + new_br) > max_bitrate) {
		exceeded_br = (cumulative_bitrate - old_br + new_br) - max_bitrate;
		new_br = ctrl->val - exceeded_br;
	}
	inst->cap[client_set_cap_id].value = new_br;

	inst->cap[BIT_RATE].value = get_cumulative_bitrate(inst);

	return 0;
}

int adjust_peak_bitrate(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	u32 adjusted_value;
	s32 rc_mode, bitrate;

	adjusted_value = ctrl ? ctrl->val : inst->cap[PEAK_BITRATE].value;

	rc_mode = inst->cap[BITRATE_MODE].value;

	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		return 0;

	bitrate = inst->cap[BIT_RATE].value;

	if (inst->cap[PEAK_BITRATE].flags & CAP_FLAG_CLIENT_SET) {
		if (adjusted_value < bitrate)
			adjusted_value = bitrate;
	} else {
		adjusted_value = inst->cap[BIT_RATE].value;
	}

	inst->cap[PEAK_BITRATE].value = adjusted_value;

	return 0;
}

int adjust_bitrate_mode(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 frame_rc, bitrate_mode, frame_skip;

	bitrate_mode = inst->cap[BITRATE_MODE].value;
	frame_rc = inst->cap[FRAME_RC_ENABLE].value;
	frame_skip = inst->cap[FRAME_SKIP_MODE].value;

	if (!frame_rc) {
		inst->hfi_rc_type = HFI_RC_OFF;
		return 0;
	}

	if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) {
		inst->hfi_rc_type = HFI_RC_VBR_CFR;
	} else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR) {
		if (frame_skip)
			inst->hfi_rc_type = HFI_RC_CBR_VFR;
		else
			inst->hfi_rc_type = HFI_RC_CBR_CFR;
	} else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ) {
		inst->hfi_rc_type = HFI_RC_CQ;
	} else if (bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_MBR) {
		inst->hfi_rc_type = HFI_RC_MBR;
	}

	return 0;
}

int adjust_gop_size(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, enh_layer_count;
	u32 min_gop_size, num_subgops;

	adjusted_value = ctrl ? ctrl->val : inst->cap[GOP_SIZE].value;

	enh_layer_count = inst->cap[ENH_LAYER_COUNT].value;

	if (!enh_layer_count)
		goto exit;

	/*
	 * Layer encoding needs GOP size to be multiple of subgop size
	 * And subgop size is 2 ^ number of enhancement layers.
	 */
	min_gop_size = 1 << enh_layer_count;
	num_subgops = (adjusted_value + (min_gop_size >> 1)) / min_gop_size;
	if (num_subgops)
		adjusted_value = num_subgops * min_gop_size;
	else
		adjusted_value = min_gop_size;

exit:
	inst->cap[GOP_SIZE].value = adjusted_value;

	return 0;
}

int adjust_b_frame(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, enh_layer_count = -1;
	const u32 max_bframe_size = inst->cap[B_FRAME].max;

	adjusted_value = ctrl ? ctrl->val : inst->cap[B_FRAME].value;

	enh_layer_count = inst->cap[ENH_LAYER_COUNT].value;

	if (!enh_layer_count || inst->hfi_layer_type != HFI_HIER_B) {
		adjusted_value = 0;
		goto exit;
	}

	adjusted_value = (1 << enh_layer_count) - 1;

	if (adjusted_value > max_bframe_size)
		adjusted_value = max_bframe_size;

exit:
	inst->cap[B_FRAME].value = adjusted_value;

	return 0;
}

int adjust_ltr_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, rc_mode, pix_fmt;

	adjusted_value = ctrl ? ctrl->val : inst->cap[LTR_COUNT].value;

	rc_mode = inst->cap[BITRATE_MODE].value;

	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR &&
	    inst->hfi_rc_type != HFI_RC_OFF)
		adjusted_value = 0;

	pix_fmt = inst->cap[PIX_FMTS].value;
	if (is_10bit_colorformat(pix_fmt))
		adjusted_value = 0;

	inst->cap[LTR_COUNT].value = adjusted_value;

	return 0;
}

int adjust_use_ltr(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, ltr_count;

	adjusted_value = ctrl ? ctrl->val : inst->cap[USE_LTR].value;

	ltr_count = inst->cap[LTR_COUNT].value;
	if (!ltr_count)
		return 0;

	/*
	 * USE_LTR is bitmask value, hence should be
	 * > 0 and <= (2 ^ LTR_COUNT) - 1
	 */
	if (adjusted_value <= 0 ||
	    adjusted_value > (1 << ltr_count) - 1)
		return 0;

	inst->cap[USE_LTR].value = adjusted_value;

	return 0;
}

int adjust_mark_ltr(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, ltr_count;

	adjusted_value = ctrl ? ctrl->val : inst->cap[MARK_LTR].value;

	ltr_count = inst->cap[LTR_COUNT].value;
	if (!ltr_count)
		return 0;

	if (adjusted_value < 0 || adjusted_value > ltr_count - 1)
		return 0;

	inst->cap[MARK_LTR].value = adjusted_value;

	return 0;
}

int adjust_ir_period(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, pix_fmt, rc_mode;

	adjusted_value = ctrl ? ctrl->val : inst->cap[IR_PERIOD].value;

	pix_fmt = inst->cap[PIX_FMTS].value;
	if (is_10bit_colorformat(pix_fmt))
		adjusted_value = 0;

	rc_mode = inst->cap[BITRATE_MODE].value;
	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		adjusted_value = 0;

	inst->cap[IR_PERIOD].value = adjusted_value;

	return 0;
}

int adjust_min_quality(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, pix_fmt, rc_mode, layer_count;
	u32 width, height, frame_rate;
	struct v4l2_format *f;

	if (inst->vb2q_dst->streaming)
		return 0;

	adjusted_value = MAX_SUPPORTED_MIN_QUALITY;

	rc_mode = inst->cap[BITRATE_MODE].value;
	if (rc_mode != V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
		adjusted_value = 0;

	layer_count = inst->cap[ENH_LAYER_COUNT].value;
	if (layer_count && inst->hfi_layer_type != HFI_HIER_B)
		adjusted_value = 0;

	pix_fmt = inst->cap[PIX_FMTS].value;
	if (is_10bit_colorformat(pix_fmt))
		adjusted_value = 0;

	frame_rate = inst->cap[FRAME_RATE].value >> 16;
	f = inst->fmt_dst;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (!res_is_less_than(width, height, 1920, 1080))
		adjusted_value = 0;

	if (frame_rate > 60)
		adjusted_value = 0;

	inst->cap[MIN_QUALITY].value = adjusted_value;

	return 0;
}

static int adjust_static_layer_count_and_type(struct iris_inst *inst, s32 layer_count)
{
	bool hb_requested = false;
	s32 max_enh_count = 0;

	if (!layer_count)
		goto exit;

	if (inst->hfi_rc_type == HFI_RC_CQ) {
		layer_count = 0;
		goto exit;
	}

	if (inst->codec == H264) {
		if (!inst->cap[LAYER_ENABLE].value) {
			layer_count = 0;
			goto exit;
		}
		hb_requested = inst->cap[LAYER_TYPE].value ==
				V4L2_MPEG_VIDEO_H264_HIERARCHICAL_CODING_B;
	} else if (inst->codec == HEVC) {
		hb_requested = inst->cap[LAYER_TYPE].value ==
				V4L2_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_B;
	}

	if (hb_requested && inst->hfi_rc_type != HFI_RC_VBR_CFR) {
		layer_count = 0;
		goto exit;
	}

	inst->hfi_layer_type = hb_requested ? HFI_HIER_B :
		(inst->codec == H264 && inst->hfi_rc_type == HFI_RC_VBR_CFR) ?
		HFI_HIER_P_HYBRID_LTR : HFI_HIER_P_SLIDING_WINDOW;

	max_enh_count = inst->hfi_layer_type == HFI_HIER_B ? MAX_ENH_LAYER_HB :
		inst->hfi_layer_type == HFI_HIER_P_HYBRID_LTR ? MAX_AVC_ENH_LAYER_HYBRID_HP :
		inst->hfi_layer_type == HFI_HIER_P_SLIDING_WINDOW ?
		(inst->codec == H264 ? MAX_AVC_ENH_LAYER_SLIDING_WINDOW :
		 (inst->codec == HEVC && inst->hfi_rc_type == HFI_RC_VBR_CFR) ?
		 MAX_HEVC_VBR_ENH_LAYER_SLIDING_WINDOW :
		 MAX_HEVC_NON_VBR_ENH_LAYER_SLIDING_WINDOW) :
		layer_count;

	layer_count = min(layer_count, max_enh_count);

exit:
	inst->cap[ENH_LAYER_COUNT].value = layer_count;
	inst->cap[ENH_LAYER_COUNT].max = layer_count;

	return 0;
}

int adjust_layer_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 client_layer_count;
	int ret = 0;

	client_layer_count = ctrl ? ctrl->val : inst->cap[ENH_LAYER_COUNT].value;

	if (!inst->vb2q_dst->streaming) {
		ret = adjust_static_layer_count_and_type(inst, client_layer_count);
		if (ret)
			return ret;
	} else {
		if (inst->hfi_rc_type == HFI_RC_CBR_CFR ||
		    inst->hfi_rc_type == HFI_RC_CBR_VFR)
			return ret;

		if (inst->hfi_layer_type == HFI_HIER_P_HYBRID_LTR ||
		    inst->hfi_layer_type == HFI_HIER_P_SLIDING_WINDOW)
			inst->cap[ENH_LAYER_COUNT].value =
				min(client_layer_count, inst->cap[ENH_LAYER_COUNT].max);
	}

	return ret;
}

int adjust_entropy_mode(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value;
	s32 profile = -1;

	adjusted_value = ctrl ? ctrl->val : inst->cap[ENTROPY_MODE].value;

	profile = inst->cap[PROFILE].value;
	if (profile == V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE ||
	    profile == V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE)
		adjusted_value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;

	inst->cap[ENTROPY_MODE].value = adjusted_value;

	return 0;
}

int adjust_slice_count(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value, rc_type = -1, slice_mode, enh_layer_count = 0;
	u32 slice_val, mbpf = 0, mbps = 0, max_mbpf = 0, max_mbps = 0, bitrate = 0;
	u32 update_cap, max_avg_slicesize, output_width, output_height;
	u32 min_width, min_height, max_width, max_height, fps;

	slice_mode = ctrl ? ctrl->val : inst->cap[SLICE_MODE].value;
	if (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE)
		return 0;

	bitrate = inst->cap[BIT_RATE].value;
	enh_layer_count = inst->cap[ENH_LAYER_COUNT].value;
	if (enh_layer_count && is_layer_bitrate_set(inst))
		bitrate = get_cumulative_bitrate(inst);

	rc_type = inst->hfi_rc_type;
	fps = inst->cap[FRAME_RATE].value >> 16;
	if (fps > MAX_SLICES_FRAME_RATE ||
	    (rc_type != HFI_RC_OFF && rc_type != HFI_RC_CBR_CFR &&
	     rc_type != HFI_RC_CBR_VFR)) {
		adjusted_value = V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE;
		update_cap = SLICE_MODE;
		goto exit;
	}

	output_width = inst->fmt_dst->fmt.pix_mp.width;
	output_height = inst->fmt_dst->fmt.pix_mp.height;

	max_width = (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB) ?
		MAX_MB_SLICE_WIDTH : MAX_BYTES_SLICE_WIDTH;
	max_height = (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB) ?
		MAX_MB_SLICE_HEIGHT : MAX_BYTES_SLICE_HEIGHT;
	min_width = (inst->codec == HEVC) ?
		MIN_HEVC_SLICE_WIDTH : MIN_AVC_SLICE_WIDTH;
	min_height = MIN_SLICE_HEIGHT;

	if (output_width < min_width || output_height < min_height ||
	    output_width > max_width || output_height > max_width) {
		adjusted_value = V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE;
		update_cap = SLICE_MODE;
		goto exit;
	}

	mbpf = NUM_MBS_PER_FRAME(output_height, output_width);
	mbps = mbpf * fps;
	max_mbpf = NUM_MBS_PER_FRAME(max_height, max_width);
	max_mbps = max_mbpf * MAX_SLICES_FRAME_RATE;

	if (mbpf > max_mbpf || mbps > max_mbps) {
		adjusted_value = V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE;
		update_cap = SLICE_MODE;
		goto exit;
	}

	if (slice_mode == V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB) {
		update_cap = SLICE_MAX_MB;
		slice_val = inst->cap[SLICE_MAX_MB].value;
		slice_val = max(slice_val, mbpf / MAX_SLICES_PER_FRAME);
	} else {
		slice_val = inst->cap[SLICE_MAX_BYTES].value;
		update_cap = SLICE_MAX_BYTES;
		if (rc_type != HFI_RC_OFF) {
			max_avg_slicesize =
				((bitrate / fps) / 8) / MAX_SLICES_PER_FRAME;
			slice_val = max(slice_val, max_avg_slicesize);
		}
	}
	adjusted_value = slice_val;

exit:
	inst->cap[update_cap].value = adjusted_value;

	return 0;
}

int adjust_transform_8x8(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 adjusted_value;
	s32 profile = -1;

	adjusted_value = ctrl ? ctrl->val : inst->cap[TRANSFORM_8X8].value;

	profile = inst->cap[PROFILE].value;
	if (profile != V4L2_MPEG_VIDEO_H264_PROFILE_HIGH &&
	    profile != V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH)
		adjusted_value = 0;

	inst->cap[TRANSFORM_8X8].value = adjusted_value;

	return 0;
}
