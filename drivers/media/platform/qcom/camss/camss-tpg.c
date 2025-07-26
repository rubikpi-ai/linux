// SPDX-License-Identifier: GPL-2.0
/*
 * camss-tpg.c
 *
 * Qualcomm MSM Camera Subsystem - TPG Module
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "camss-tpg.h"
#include "camss.h"

const char * const testgen_payload_modes[] = {
	"Disabled",
	"Incrementing",
	"Alternating 0x55/0xAA",
	NULL,
	NULL,
	"Pseudo-random Data",
	"User Specified",
	NULL,
	NULL,
	"Color bars",
	NULL
};

static const struct tpg_format_info formats_gen1[] = {
	{
		MEDIA_BUS_FMT_SBGGR8_1X8,
		DATA_TYPE_RAW_8BIT,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
	},
	{
		MEDIA_BUS_FMT_SGBRG8_1X8,
		DATA_TYPE_RAW_8BIT,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
	},
	{
		MEDIA_BUS_FMT_SGRBG8_1X8,
		DATA_TYPE_RAW_8BIT,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
	},
	{
		MEDIA_BUS_FMT_SRGGB8_1X8,
		DATA_TYPE_RAW_8BIT,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
	},
	{
		MEDIA_BUS_FMT_SBGGR10_1X10,
		DATA_TYPE_RAW_10BIT,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
	},
	{
		MEDIA_BUS_FMT_SGBRG10_1X10,
		DATA_TYPE_RAW_10BIT,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
	},
	{
		MEDIA_BUS_FMT_SGRBG10_1X10,
		DATA_TYPE_RAW_10BIT,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
	},
	{
		MEDIA_BUS_FMT_SRGGB10_1X10,
		DATA_TYPE_RAW_10BIT,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
	},
	{
		MEDIA_BUS_FMT_SBGGR12_1X12,
		DATA_TYPE_RAW_12BIT,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
	},
	{
		MEDIA_BUS_FMT_SGBRG12_1X12,
		DATA_TYPE_RAW_12BIT,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
	},
	{
		MEDIA_BUS_FMT_SGRBG12_1X12,
		DATA_TYPE_RAW_12BIT,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
	},
	{
		MEDIA_BUS_FMT_SRGGB12_1X12,
		DATA_TYPE_RAW_12BIT,
		ENCODE_FORMAT_UNCOMPRESSED_12_BIT,
	},
	{
		MEDIA_BUS_FMT_Y8_1X8,
		DATA_TYPE_RAW_8BIT,
		ENCODE_FORMAT_UNCOMPRESSED_8_BIT,
	},
	{
		MEDIA_BUS_FMT_Y10_1X10,
		DATA_TYPE_RAW_10BIT,
		ENCODE_FORMAT_UNCOMPRESSED_10_BIT,
	},
};

const struct tpg_formats tpg_formats_gen1 = {
	.nformats = ARRAY_SIZE(formats_gen1),
	.formats = formats_gen1
};


const struct tpg_format_info *tpg_get_fmt_entry(const struct tpg_format_info *formats,
						unsigned int nformats,
						u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return &formats[i];

	WARN(1, "Unknown format\n");

	return &formats[0];
}

/*
 * tpg_set_clock_rates - Calculate and set clock rates on tpg module
 * @tpg: tpg device
 */
static int tpg_set_clock_rates(struct tpg_device *tpg)
{
	struct device *dev = tpg->camss->dev;
	int i, j;
	int ret;

	for (i = 0; i < tpg->nclocks; i++) {
		struct camss_clock *clock = &tpg->clock[i];
		u64 min_rate = 0;
		long round_rate;

		camss_add_clock_margin(&min_rate);

		for (j = 0; j < clock->nfreqs; j++)
			if (min_rate < clock->freq[j])
				break;

		if (j == clock->nfreqs) {
			dev_err(dev,
				"clock is too high for TPG\n");
			return -EINVAL;
		}

		/* if clock is not available */
		/* set highest possible tpg clock rate */
		if (min_rate == 0)
			j = clock->nfreqs - 1;

		round_rate = clk_round_rate(clock->clk, clock->freq[j]);
		if (round_rate < 0) {
			dev_err(dev, "clk round rate failed: %ld\n",
				round_rate);
			return -EINVAL;
		}

		tpg->timer_clk_rate = round_rate;

		ret = clk_set_rate(clock->clk, tpg->timer_clk_rate);
		if (ret < 0) {
			dev_err(dev, "clk set rate failed: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

/*
 * tpg_set_power - Power on/off tpg module
 * @sd: tpg V4L2 subdevice
 * @on: Requested power state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_set_power(struct v4l2_subdev *sd, int on)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct device *dev = tpg->camss->dev;

	if (on) {
		int ret;

		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			return ret;

		ret = tpg_set_clock_rates(tpg);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		ret = camss_enable_clocks(tpg->nclocks, tpg->clock, dev);
		if (ret < 0) {
			pm_runtime_put_sync(dev);
			return ret;
		}

		enable_irq(tpg->irq);

		tpg->res->hw_ops->reset(tpg);

		tpg->res->hw_ops->hw_version(tpg);
	} else {
		disable_irq(tpg->irq);

		camss_disable_clocks(tpg->nclocks, tpg->clock);

		pm_runtime_put_sync(dev);
	}

	return 0;
}


/*
 * tpg_set_stream - Enable/disable streaming on tpg module
 * @sd: tpg V4L2 subdevice
 * @enable: Requested streaming state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (enable) {
		ret = v4l2_ctrl_handler_setup(&tpg->ctrls);
		if (ret < 0) {
			dev_err(tpg->camss->dev,
				"could not sync v4l2 controls: %d\n", ret);
			return ret;
		}
	}

	tpg->res->hw_ops->configure_stream(tpg, enable);

	return 0;
}

/*
 * __tpg_get_format - Get pointer to format structure
 * @tpg: tpg device
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad from which format is requested
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE format structure
 */
static struct v4l2_mbus_framefmt *
__tpg_get_format(struct tpg_device *tpg,
		 struct v4l2_subdev_state *sd_state,
		 unsigned int pad,
		 enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&tpg->subdev, sd_state,
						  pad);

	return &tpg->fmt[pad];
}

/*
 * tpg_try_format - Handle try format by pad subdev method
 * @tpg: tpg device
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad on which format is requested
 * @fmt: pointer to v4l2 format structure
 * @which: wanted subdev format
 */
static void tpg_try_format(struct tpg_device *tpg,
			   struct v4l2_subdev_state *sd_state,
			   unsigned int pad,
			   struct v4l2_mbus_framefmt *fmt,
			   enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case MSM_TPG_PAD_SINK:
		/* Test generator is enabled, set format on source */
		/* pad to allow test generator usage */

		for (i = 0; i < tpg->res->formats->nformats; i++)
			if (tpg->res->formats->formats[i].code == fmt->code)
				break;

		/* If not found, use SBGGR8 as default */
		if (i >= tpg->res->formats->nformats)
			fmt->code = MEDIA_BUS_FMT_SBGGR8_1X8;

		fmt->width = clamp_t(u32, fmt->width, 1, 8191);
		fmt->height = clamp_t(u32, fmt->height, 1, 8191);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		break;
	case MSM_TPG_PAD_SRC:
		/* Set and return a format same as sink pad */

		*fmt = *__tpg_get_format(tpg, sd_state,
					    MSM_TPG_PAD_SINK,
					    which);

		break;
	}
}

/*
 * tpg_enum_mbus_code - Handle format enumeration
 * @sd: tpg V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int tpg_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (code->pad == MSM_TPG_PAD_SINK) {
		if (code->index >= tpg->res->formats->nformats)
			return -EINVAL;

		code->code = tpg->res->formats->formats[code->index].code;
	} else {
		if (code->index > 0)
			return -EINVAL;

		format = __tpg_get_format(tpg, sd_state,
					     MSM_TPG_PAD_SINK,
					     code->which);

		code->code = format->code;
	}

	return 0;
}

/*
 * tpg_enum_frame_size - Handle frame size enumeration
 * @sd: tpg V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fse: pointer to v4l2_subdev_frame_size_enum structure
 * return -EINVAL or zero on success
 */
static int tpg_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	tpg_try_format(tpg, sd_state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	tpg_try_format(tpg, sd_state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * tpg_get_format - Handle get format by pads subdev method
 * @sd: tpg V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int tpg_get_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __tpg_get_format(tpg, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

/*
 * tpg_set_format - Handle set format by pads subdev method
 * @sd: tpg V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int tpg_set_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tpg_device *tpg = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __tpg_get_format(tpg, sd_state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	tpg_try_format(tpg, sd_state, fmt->pad, &fmt->format,
			  fmt->which);
	*format = fmt->format;

	if (fmt->pad == MSM_TPG_PAD_SINK) {
		format = __tpg_get_format(tpg, sd_state,
					     MSM_TPG_PAD_SRC,
					     fmt->which);

		*format = fmt->format;
		tpg_try_format(tpg, sd_state, MSM_TPG_PAD_SRC,
				  format,
				  fmt->which);
	}
	return 0;
}

/*
 * tpg_init_formats - Initialize formats on all pads
 * @sd: tpg V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_init_formats(struct v4l2_subdev *sd,
			    struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = MSM_TPG_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
			      V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_SBGGR8_1X8,
			.width = 1920,
			.height = 1080
		}
	};

	return tpg_set_format(sd, fh ? fh->state : NULL, &format);
}

/*
 * tpg_set_test_pattern - Set test generator's pattern mode
 * @tpg: TPG device
 * @value: desired test pattern mode
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_set_test_pattern(struct tpg_device *tpg, s32 value)
{
	return tpg->res->hw_ops->configure_testgen_pattern(tpg, value);
}

/*
 * tpg_s_ctrl - Handle set control subdev method
 * @ctrl: pointer to v4l2 control structure
 *
 * Return 0 on success or a negative error code otherwise
 */
static int tpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tpg_device *tpg = container_of(ctrl->handler,
					      struct tpg_device, ctrls);
	int ret = -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		ret = tpg_set_test_pattern(tpg, ctrl->val);
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops tpg_ctrl_ops = {
	.s_ctrl = tpg_s_ctrl,
};

/*
 * msm_tpg_subdev_init - Initialize tpg device structure and resources
 * @tpg: tpg device
 * @res: tpg module resources table
 * @id: tpg module id
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_tpg_subdev_init(struct camss *camss,
			struct tpg_device *tpg,
			const struct camss_subdev_resources *res, u8 id)
{
	struct device *dev = camss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i, j;
	int ret;

	tpg->camss = camss;
	tpg->id = id;
	tpg->res = &res->tpg;
	tpg->res->hw_ops->subdev_init(tpg);

	/* Memory */
	tpg->base = devm_platform_ioremap_resource_byname(pdev, res->reg[0]);
	if (IS_ERR(tpg->base))
		return PTR_ERR(tpg->base);

	/* Interrupt */
	ret = platform_get_irq_byname(pdev, res->interrupt[0]);
	if (ret < 0)
		return ret;

	tpg->irq = ret;
	snprintf(tpg->irq_name, sizeof(tpg->irq_name), "%s_%s%d",
		 dev_name(dev), MSM_TPG_NAME, tpg->id);

	ret = devm_request_irq(dev, tpg->irq, tpg->res->hw_ops->isr,
			       IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN,
			       tpg->irq_name, tpg);
	if (ret < 0) {
		dev_err(dev, "request_irq failed: %d\n", ret);
		return ret;
	}

	/* Clocks */
	tpg->nclocks = 0;
	while (res->clock[tpg->nclocks])
		tpg->nclocks++;

	tpg->clock = devm_kcalloc(dev,
				  tpg->nclocks, sizeof(*tpg->clock),
				  GFP_KERNEL);
	if (!tpg->clock)
		return -ENOMEM;

	for (i = 0; i < tpg->nclocks; i++) {
		struct camss_clock *clock = &tpg->clock[i];

		clock->clk = devm_clk_get(dev, res->clock[i]);
		if (IS_ERR(clock->clk))
			return PTR_ERR(clock->clk);

		clock->name = res->clock[i];

		clock->nfreqs = 0;
		while (res->clock_rate[i][clock->nfreqs])
			clock->nfreqs++;

		if (!clock->nfreqs) {
			clock->freq = NULL;
			continue;
		}

		clock->freq = devm_kcalloc(dev,
					   clock->nfreqs,
					   sizeof(*clock->freq),
					   GFP_KERNEL);
		if (!clock->freq)
			return -ENOMEM;

		for (j = 0; j < clock->nfreqs; j++)
			clock->freq[j] = res->clock_rate[i][j];
	}

	return 0;
}

/*
 * tpg_link_setup - Setup tpg connections
 * @entity: Pointer to media entity structure
 * @local: Pointer to local pad
 * @remote: Pointer to remote pad
 * @flags: Link flags
 *
 * Rreturn 0 on success
 */
static int tpg_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_pad_remote_pad_first(local))
			return -EBUSY;

	return 0;
}

static const struct v4l2_subdev_core_ops tpg_core_ops = {
	.s_power = tpg_set_power,
};

static const struct v4l2_subdev_video_ops tpg_video_ops = {
	.s_stream = tpg_set_stream,
};

static const struct v4l2_subdev_pad_ops tpg_pad_ops = {
	.enum_mbus_code = tpg_enum_mbus_code,
	.enum_frame_size = tpg_enum_frame_size,
	.get_fmt = tpg_get_format,
	.set_fmt = tpg_set_format,
};

static const struct v4l2_subdev_ops tpg_v4l2_ops = {
	.core = &tpg_core_ops,
	.video = &tpg_video_ops,
	.pad = &tpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops tpg_v4l2_internal_ops = {
	.open = tpg_init_formats,
};

static const struct media_entity_operations tpg_media_ops = {
	.link_setup = tpg_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * msm_tpg_register_entity - Register subdev node for tpg module
 * @tpg: tpg device
 * @v4l2_dev: V4L2 device
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_tpg_register_entity(struct tpg_device *tpg,
			    struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd = &tpg->subdev;
	struct media_pad *pads = tpg->pads;
	struct device *dev = tpg->camss->dev;
	int ret;

	v4l2_subdev_init(sd, &tpg_v4l2_ops);
	sd->internal_ops = &tpg_v4l2_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d",
		 MSM_TPG_NAME, tpg->id);
	v4l2_set_subdevdata(sd, tpg);

	ret = v4l2_ctrl_handler_init(&tpg->ctrls, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to init ctrl handler: %d\n", ret);
		return ret;
	}

	tpg->testgen_mode = v4l2_ctrl_new_std_menu_items(&tpg->ctrls,
							 &tpg_ctrl_ops, V4L2_CID_TEST_PATTERN,
							 TPG_PAYLOAD_MODE_COLOR_BARS, 0, 0,
							 tpg->testgen.modes);

	if (tpg->ctrls.error) {
		dev_err(dev, "Failed to init ctrl: %d\n", tpg->ctrls.error);
		ret = tpg->ctrls.error;
		goto free_ctrl;
	}

	tpg->subdev.ctrl_handler = &tpg->ctrls;

	ret = tpg_init_formats(sd, NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to init format: %d\n", ret);
		goto free_ctrl;
	}

	pads[MSM_TPG_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[MSM_TPG_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &tpg_media_ops;
	ret = media_entity_pads_init(&sd->entity, MSM_TPG_PADS_NUM, pads);
	if (ret < 0) {
		dev_err(dev, "Failed to init media entity: %d\n", ret);
		goto free_ctrl;
	}

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		media_entity_cleanup(&sd->entity);
		goto free_ctrl;
	}

	return 0;

free_ctrl:
	v4l2_ctrl_handler_free(&tpg->ctrls);

	return ret;
}

/*
 * msm_tpg_unregister_entity - Unregister tpg module subdev node
 * @tpg: tpg device
 */
void msm_tpg_unregister_entity(struct tpg_device *tpg)
{
	v4l2_device_unregister_subdev(&tpg->subdev);
	media_entity_cleanup(&tpg->subdev.entity);
	v4l2_ctrl_handler_free(&tpg->ctrls);
}
