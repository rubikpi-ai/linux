// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_power.h"
#include "iris_helpers.h"
#include "resources.h"

static int iris_set_buses(struct iris_inst *inst)
{
	struct iris_inst *instance;
	struct iris_core *core;
	u64 total_bw_ddr = 0;
	int ret;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		if (!instance->max_input_data_size)
			continue;

		total_bw_ddr += instance->power.bus_bw;
	}

	ret = vote_buses(core, total_bw_ddr);

	mutex_unlock(&core->lock);

	return ret;
}

static int iris_vote_buses(struct iris_inst *inst)
{
	struct v4l2_format *out_f, *inp_f;
	struct bus_vote_data *vote_data;
	struct iris_core *core;

	core = inst->core;

	vote_data = &inst->bus_data;

	out_f = inst->fmt_dst;
	inp_f = inst->fmt_src;

	vote_data->width = inp_f->fmt.pix_mp.width;
	vote_data->height = inp_f->fmt.pix_mp.height;
	vote_data->fps = inst->max_rate;

	if (inst->domain == ENCODER) {
		vote_data->color_formats[0] =
			v4l2_colorformat_to_driver(inst, inst->fmt_src->fmt.pix_mp.pixelformat);
	} else if (inst->domain == DECODER) {
		if (is_linear_colorformat(out_f->fmt.pix_mp.pixelformat)) {
			vote_data->color_formats[0] = V4L2_PIX_FMT_NV12;
			vote_data->color_formats[1] = out_f->fmt.pix_mp.pixelformat;
		} else {
			vote_data->color_formats[0] = out_f->fmt.pix_mp.pixelformat;
		}
	}

	call_session_op(core, calc_bw, inst, vote_data);

	inst->power.bus_bw = vote_data->bus_bw;

	return iris_set_buses(inst);
}

static int iris_set_clocks(struct iris_inst *inst)
{
	struct iris_inst *instance;
	struct iris_core *core;
	int ret = 0;
	u64 freq;

	core = inst->core;

	mutex_lock(&core->lock);

	freq = 0;
	list_for_each_entry(instance, &core->instances, list) {
		if (!instance->max_input_data_size)
			continue;

		freq += instance->power.min_freq;
	}

	core->power.clk_freq = freq;

	ret = opp_set_rate(core, freq);

	mutex_unlock(&core->lock);

	return ret;
}

static int iris_scale_clocks(struct iris_inst *inst)
{
	struct iris_buffer *vbuf;
	struct iris_core *core;
	u32 data_size = 0;

	core = inst->core;

	list_for_each_entry(vbuf, &inst->buffers.input.list, list)
		data_size = max(data_size, vbuf->data_size);

	inst->max_input_data_size = data_size;

	if (inst->domain == ENCODER) {
		inst->max_rate = max((inst->cap[OPERATING_RATE].value >> 16),
			(inst->cap[FRAME_RATE].value >> 16));
	} else if (inst->domain == DECODER) {
		inst->max_rate = inst->cap[QUEUED_RATE].value >> 16;
	}

	if (!inst->max_input_data_size)
		return 0;

	inst->power.min_freq = call_session_op(core, calc_freq, inst,
					       inst->max_input_data_size);
	iris_set_clocks(inst);

	return 0;
}

int iris_scale_power(struct iris_inst *inst)
{
	iris_scale_clocks(inst);
	iris_vote_buses(inst);

	return 0;
}

int iris_update_input_rate(struct iris_inst *inst, u64 time_us)
{
	struct iris_input_timer *prev_timer = NULL;
	struct iris_input_timer *input_timer;
	u64 input_timer_sum_us = 0;
	u64 counter = 0;

	input_timer = kzalloc(sizeof(*input_timer), GFP_KERNEL);
	if (!input_timer)
		return -ENOMEM;

	input_timer->time_us = time_us;
	INIT_LIST_HEAD(&input_timer->list);
	list_add_tail(&input_timer->list, &inst->input_timer_list);
	list_for_each_entry(input_timer, &inst->input_timer_list, list) {
		if (prev_timer) {
			input_timer_sum_us += input_timer->time_us - prev_timer->time_us;
			counter++;
		}
		prev_timer = input_timer;
	}

	if (input_timer_sum_us && counter >= INPUT_TIMER_LIST_SIZE)
		inst->cap[QUEUED_RATE].value =
			(s32)(DIV64_U64_ROUND_CLOSEST(counter * 1000000,
				input_timer_sum_us) << 16);

	if (counter >= INPUT_TIMER_LIST_SIZE) {
		input_timer = list_first_entry(&inst->input_timer_list,
					       struct iris_input_timer, list);
		list_del_init(&input_timer->list);
		kfree(input_timer);
	}

	return 0;
}

int iris_flush_input_timer(struct iris_inst *inst)
{
	struct iris_input_timer *input_timer, *dummy_timer;

	list_for_each_entry_safe(input_timer, dummy_timer, &inst->input_timer_list, list) {
		list_del_init(&input_timer->list);
		kfree(input_timer);
	}

	return 0;
}
