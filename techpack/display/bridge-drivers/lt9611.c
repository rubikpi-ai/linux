// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 */

/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/of_platform.h>

#include <sound/hdmi-codec.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_file.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

#include <linux/soc/qcom/msm_ext_display.h>

#define EDID_BLOCK_SIZE	128
#define EDID_NUM_BLOCKS	2

#define MAX_NUMBER_ADB 5
#define MAX_AUDIO_DATA_BLOCK_SIZE 30

typedef enum {
	MIPI_1LANE = 1,
	MIPI_2LANE = 2,
	MIPI_3LANE = 3,
	MIPI_4LANE = 0,
} mipi_lane_counts;

typedef enum {
	I2S = 0,
	SPDIF = 1,
} audio_intf;

typedef enum {
	MIPI_1PORT = 0x00,
	MIPI_2PORT = 0x03,
} mipi_port_counts;

typedef enum {
	VIDEO_1024x600_60HZ = 0,
	VIDEO_1920x1080_60HZ,
	VIDEO_2560x1440_60HZ,
	VIDEO_3840x2160_30HZ,
	VIDEO_1280x720_60HZ,
	VIDEO_640x480_60Hz,
	VIDEO_720x480_60Hz,
	VIDEO_720x576_60Hz,
	VIDEO_800x600_60Hz,
	VIDEO_1024x768_60Hz,
	VIDEO_1280x768_60Hz,
	VIDEO_1280x800_60Hz,
	VIDEO_1600x1200_60Hz,
	VIDEO_INDEX_MAX
} video_format_id;

struct lt9611_mode {
	u16 h_front_porch;
	u16 h_pulse_width;
	u16 h_back_porch;
	u16 h_active;
	u16 h_total;
	u16 v_front_porch;
	u16 v_pulse_width;
	u16 v_back_porch;
	u16 v_active;
	u16 v_total;
	u8  v_refresh;
	bool h_polarity;
	bool v_polarity;
	u8 vic;
	u32 pclk_khz;
};

struct lt9611 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct edid *edid;

	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;

	struct wait_queue_head wq;
	struct work_struct work;

	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct platform_device *audio_pdev;

	mipi_port_counts mipi_port_counts;
	mipi_lane_counts mipi_lane_counts;
	video_format_id video_format_id;
	audio_intf audio_out_intf;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *ocb_gpio;

	u8 pcr_m;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	bool hpd_supported;
	bool power_on;
	/* can be accessed from different threads, so protect this with ocm_lock */
	bool hdmi_connected;
	bool edid_read_sts;
	bool edid_read_en;

	/* external display platform device */
	struct platform_device *ext_pdev;
	struct msm_ext_disp_init_data ext_audio_data;
	struct msm_ext_disp_audio_edid_blk audio_edid_blk;
	u8 raw_sad[MAX_NUMBER_ADB * MAX_AUDIO_DATA_BLOCK_SIZE];
	bool audio_support;

	/* CEC support */
	struct cec_adapter *cec_adapter;
	u8 cec_log_addr;
	u8 cec_tx_data;
	bool cec_en;
	bool cec_support;
	struct work_struct cec_recv_work;
	struct work_struct cec_transmit_work;
	bool cec_status;
	unsigned int cec_tx_status;
	struct cec_notifier *cec_notifier;

	/* Dynamic Mode Switch support */
	struct drm_display_mode curr_mode;
	bool fix_mode;
	struct lt9611_mode debug_mode;
	u8 edid_buf[256];
};

#define LT9611_PAGE_CONTROL	0xff

static struct lt9611_mode lt9611_modes[] = {
	{ 44,  88,  188,  1024,   1344,  3,   6,  16,  600,  625,   60,  0,  0,  0,  50400  }, // video_1024x600_60Hz
	{ 88,  44,  148,  1920,   2200,  4,   5,  36,  1080, 1125,  60,  1,  1,  16, 148500 }, // video_1920x1080_60Hz
	{ 48,  32,  80,   2560,   2720,  3,   5,  33,  1440, 1481,  60,  1,  1,  0,  241700 }, // video_2560x1440_60Hz
	{ 176, 88,  296,  3840,   4400,  8,   10, 72,  2160, 2250,  30,  1,  1,  95, 297000 }, // video_3840x2160_30Hz
	{ 110, 40,  220,  1280,   1650,  5,   5,  20,  720,  750,   60,  1,  1,  4,  74250  }, // video_1280x720_60Hz
	{ 16,  96,  48,   640,    800,   10,  2,  33,  480,  525,   60,  0,  0,  1,  25000  }, // video_640x480_60Hz
	{ 16,  62,  60,   720,    858,   9,   6,  30,  480,  525,   60,  0,  0,  2,  27000  }, // video_720x480_60Hz
	{ 12,  64,  68,   720,    864,   5,   5,  39,  576,  625,   60,  0,  0,  17, 27000  }, // video_720x576_60Hz
	{ 48,  128, 88,   800,    1056,  1,   4,  23,  600,  628,   60,  1,  1,  0,  40000  }, // video_800x600_60Hz
	{ 24,  136, 160,  1024,   1344,  3,   6,  29,  768,  806,   60,  0,  0,  0,  65000  }, // video_1024x768_60Hz
	{ 64,  128, 192,  1280,   1664,  3,   7,  20,  768,  798,   60,  0,  1,  0,  79500  }, // video_1280x768_60Hz
	{ 28,  32,  100,  1280,   1440,  2,   6,  15,  800,  823,   60,  0,  0,  0,  71000  }, // video_1280x800_60Hz
	{ 64,  192, 304,  1600,   2160,  1,   3,  46,  1200, 1250,  60,  1,  1,  0,  162000 }, // video_1600x1200_60Hz
};

static const struct regmap_range_cfg lt9611_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0xd0ff,
		.selector_reg = LT9611_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611_ranges,
	.num_ranges = ARRAY_SIZE(lt9611_ranges),
};

#if !IS_ENABLED(CONFIG_CEC_CORE)
static inline struct cec_adapter *cec_allocate_adapter(
		const struct cec_adap_ops *ops, void *priv,
		const char *name, u32 caps, u8 available_las)
{
	return NULL;
}

static inline int cec_s_log_addrs(struct cec_adapter *adap,
		struct cec_log_addrs *log_addrs, bool block)
{
	return 0;
}

static inline void cec_transmit_attempt_done(struct cec_adapter *adap,
					     u8 status)
{
}

static inline void cec_received_msg(struct cec_adapter *adap,
				    struct cec_msg *msg)
{
}
#endif

static struct lt9611 *bridge_to_lt9611(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611, bridge);
}

static struct lt9611 *connector_to_lt9611(struct drm_connector *connector)
{
	return container_of(connector, struct lt9611, connector);
}

static void lt9611_lock(struct lt9611 *lt9611)
{
	mutex_lock(&lt9611->ocm_lock);
	regmap_write(lt9611->regmap, 0x80ee, 0x01);
}

static void lt9611_unlock(struct lt9611 *lt9611)
{
	regmap_write(lt9611->regmap, 0x80ee, 0x00);
	msleep(50);
	mutex_unlock(&lt9611->ocm_lock);
}

static int lt9611_setup_audio_infoframes(struct lt9611 *lt9611,
		struct msm_ext_disp_audio_setup_params *params)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;
	u8 i = 0;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(lt9611->dev, "Failed to setup audio infoframe: %zd\n", err);
		return err;
	}

	/* frame.coding_type */
	frame.channels = params->num_of_channels;
	frame.sample_frequency = params->sample_rate_hz;
	/* frame.sample_size */
	/* frame.coding_type_ext */
	frame.channel_allocation = params->channel_allocation;
	frame.downmix_inhibit = params->down_mix;
	frame.level_shift_value = params->level_shift;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(lt9611->dev, "Failed to pack audio infoframe: %zd\n", err);
		return err;
	}

	/* write checksum and byte 1 to byte 8 */
	for (i = 0; i < 9; i++)
		pr_err("hdmi_audio_infoframe_pack, buffer[3+%d]= %d\n", i, buffer[3+i]);

	return 0;
}

static void lt9611_cea_sad_to_raw_sad(struct cea_sad *sads, u8 sad_count,
		u8 *blk)
{
	int i = 0;

	for (i = 0; i < sad_count; i++) {
		blk[i * 3] = (sads[i].format << 3) + sads[i].channels;
		blk[i * 3 + 1] = sads[i].freq;
		blk[i * 3 + 2] = sads[i].byte2;
	}
}

static int lt9611_get_edid_audio_blk(struct msm_ext_disp_audio_edid_blk *blk,
				struct edid *edid)
{
	struct lt9611 *lt9611 = container_of(blk, struct lt9611, audio_edid_blk);
	int i = 0;

	/* Short Audio Descriptor */
	struct cea_sad *sads;
	int sad_count = 0;

	/* Speaker Allocation Data Block */
	u8 *sadb = NULL;
	int sadb_size = 0;

	sad_count = drm_edid_to_sad(edid, &sads);
	lt9611_cea_sad_to_raw_sad(sads, sad_count, lt9611->raw_sad);
	sadb_size = drm_edid_to_speaker_allocation(edid, &sadb);
	dev_err(lt9611->dev, "sad_count %d, sadb_size %d\n", sad_count, sadb_size);

	blk->audio_data_blk = lt9611->raw_sad;
	blk->audio_data_blk_size = sad_count * 3; /* SAD is 3B */
	for (i = 0; i < blk->audio_data_blk_size; i++)
		dev_err(lt9611->dev, "%02X\n", blk->audio_data_blk[i]);

	blk->spk_alloc_data_blk = sadb;
	blk->spk_alloc_data_blk_size = sadb_size;

	/* from CEA-861-F spec, the size is always 3 bytes */
	for (i = 0; i < blk->spk_alloc_data_blk_size; i++)
		dev_err(lt9611->dev, "%02X\n", blk->spk_alloc_data_blk[i]);

	return 0;
}

static struct lt9611 *lt9611_audio_get_pdata(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct lt9611 *lt9611;

	if (!pdev) {
		pr_err("%s:Invalid pdev\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		pr_err("%s:Invalid ext disp data\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	lt9611 = ext_data->intf_data;
	if (!lt9611) {
		pr_err("%s:Invalid intf data\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	return lt9611;
}

static int hdmi_audio_info_setup(struct platform_device *pdev,
		struct msm_ext_disp_audio_setup_params *params)
{
	struct lt9611 *lt9611 = lt9611_audio_get_pdata(pdev);
	int rc = 0;

	rc = lt9611_setup_audio_infoframes(lt9611, params);

	return 0;
}

static int hdmi_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	struct lt9611 *lt9611 = lt9611_audio_get_pdata(pdev);

	lt9611_get_edid_audio_blk(&lt9611->audio_edid_blk, lt9611->edid);

	blk->audio_data_blk = lt9611->audio_edid_blk.audio_data_blk;
	blk->audio_data_blk_size = lt9611->audio_edid_blk.audio_data_blk_size;

	blk->spk_alloc_data_blk = lt9611->audio_edid_blk.spk_alloc_data_blk;
	blk->spk_alloc_data_blk_size =
		lt9611->audio_edid_blk.spk_alloc_data_blk_size;

	return 0;
}

static int hdmi_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	struct lt9611 *lt9611 = lt9611_audio_get_pdata(pdev);

	return IS_ERR(lt9611) ? PTR_ERR(lt9611): lt9611->hdmi_connected;
}

static int hdmi_audio_get_intf_id(struct platform_device *pdev)
{
	struct lt9611 *lt9611 = lt9611_audio_get_pdata(pdev);

	return IS_ERR(lt9611) ? PTR_ERR(lt9611): EXT_DISPLAY_TYPE_HDMI;
}

static void hdmi_audio_teardown_done(struct platform_device *pdev)
{
}

static int hdmi_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	return 0;
}

static int hdmi_audio_codec_ready(struct platform_device *pdev)
{
	return 0;
}

static int hdmi_audio_register_ext_disp(struct lt9611 *lt9611)
{
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;
	struct device_node *np = NULL;
	const char *phandle = "lt,ext-disp";

	int rc = 0;

	ext = &lt9611->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_HDMI;
	ext->codec.ctrl_id = 1;
	ext->codec.stream_id = 0;
	ext->pdev = lt9611->audio_pdev;
	ext->intf_data = lt9611;

	ops->audio_info_setup   = hdmi_audio_info_setup;
	ops->get_audio_edid_blk = hdmi_audio_get_edid_blk;
	ops->cable_status       = hdmi_audio_get_cable_status;
	ops->get_intf_id        = hdmi_audio_get_intf_id;
	ops->teardown_done      = hdmi_audio_teardown_done;
	ops->acknowledge        = hdmi_audio_ack_done;
	ops->ready              = hdmi_audio_codec_ready;

	if (!lt9611->dev->of_node) {
		dev_err(lt9611->dev, "cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	np = of_parse_phandle(lt9611->dev->of_node, phandle, 0);
	if (!np) {
		dev_err(lt9611->dev, "cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	lt9611->ext_pdev = of_find_device_by_node(np);
	if (!lt9611->ext_pdev) {
		dev_err(lt9611->dev, "cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	rc = msm_ext_disp_register_intf(lt9611->ext_pdev, ext);
	if (rc)
		dev_err(lt9611->dev, "failed to register ext disp\n");

end:
	return rc;
}

static int hdmi_audio_deregister_ext_disp(struct lt9611 *lt9611)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "lt,ext-disp";
	struct msm_ext_disp_init_data *ext;

	ext = &lt9611->ext_audio_data;

	if (!lt9611->dev->of_node) {
		dev_err(lt9611->dev, "cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(lt9611->dev->of_node, phandle, 0);
	if (!pd) {
		dev_err(lt9611->dev, "cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	lt9611->ext_pdev = of_find_device_by_node(pd);
	if (!lt9611->ext_pdev) {
		dev_err(lt9611->dev, "cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	rc = msm_ext_disp_deregister_intf(lt9611->ext_pdev, ext);
	if (rc)
		dev_err(lt9611->dev, "failed to deregister ext disp\n");

end:
	return rc;
}

static int lt9611_sys_init(struct lt9611 *lt9611)
{
	int ret;
	const struct reg_sequence seq[] = {
		/* LT9611_System_Init */
		{ 0x8101, 0x18 }, /* sel xtal clock */
		{ 0x8251, 0x11 },

		/* timer for frequency meter */
		{ 0x821b, 0x69 }, /* timer 2 */
		{ 0x821c, 0x78 },
		{ 0x82cb, 0x69 }, /* timer 1 */
		{ 0x82cc, 0x78 },

		/* power consumption for work */
		{ 0x8004, 0xf0 },
		{ 0x8006, 0xf0 },
		{ 0x800a, 0x80 },
		{ 0x800b, 0x46 },
		{ 0x800d, 0xef },
		{ 0x8011, 0xfa },
	};

	ret = regmap_multi_reg_write(lt9611->regmap, seq, ARRAY_SIZE(seq));

	return ret;
}

static int lt9611_mipi_input_analog(struct lt9611 *lt9611)
{
	const struct reg_sequence reg_cfg[] = {
		{ 0x8106, 0x60 }, /* port A rx current */
		{ 0x8107, 0x3f }, //eq
		{ 0x8108, 0x3f }, //eq
		{ 0x810a, 0xfe }, /* port A ldo voltage set */
		{ 0x810b, 0xbf }, /* enable port A lprx */

		{ 0x8111, 0x60 }, /* port B rx current */
		{ 0x8112, 0x3f }, //eq
		{ 0x8113, 0x3f }, //eq
		{ 0x8115, 0xfe }, /* port B ldo voltage set */
		{ 0x8116, 0xbf }, /* enable port B lprx */

		{ 0x811c, 0x03 }, /* PortA clk lane no-LP mode */
		{ 0x8120, 0x03 }, /* PortB clk lane with-LP mode */
	};

	return regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static int lt9611_mipi_input_digital(struct lt9611 *lt9611,
				     const struct drm_display_mode *mode)
{
	u8 lanes = lt9611->mipi_lane_counts;
	u8 ports = lt9611->mipi_port_counts;
	int ret;

	struct reg_sequence reg_cfg[] = {
		{ 0x8250, 0x14 },
		{ 0x8303, 0x40 },
		{ 0x824f, 0x80 },
		{ 0x8300, lanes },
		{ 0x8302, 0x0a }, //settle
		{ 0x8306, 0x0a }, //settle
		{ 0x830a, ports },
	};

	ret = regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	return ret;
}

static void lt9611_Audio_Init(struct lt9611 *lt9611)
{
	if (lt9611->audio_out_intf == I2S) {
		regmap_write(lt9611->regmap, 0x82d7, 0x04);
		regmap_write(lt9611->regmap, 0x8406, 0x08);
		regmap_write(lt9611->regmap, 0x8407, 0x10);

		// 48K sampling frequency
		regmap_write(lt9611->regmap, 0x840f, 0x2b);
		regmap_write(lt9611->regmap, 0x8434, 0xd4);//CTS_N 0xd5: sclk = 32fs, 0xd4: sclk = 64fs

		regmap_write(lt9611->regmap, 0x8435, 0x00); // N value = 6144
		regmap_write(lt9611->regmap, 0x8436, 0x18);
		regmap_write(lt9611->regmap, 0x8437, 0x00);
	}

	if (lt9611->audio_out_intf == SPDIF) {
		regmap_write(lt9611->regmap, 0x8406, 0x0c);
		regmap_write(lt9611->regmap, 0x8407, 0x10);

		regmap_write(lt9611->regmap, 0x8434, 0xd4); //CTS_N
		regmap_write(lt9611->regmap, 0x8436, 0x20);
	}
}

static void lt9611_hdmi_tx_phy(struct lt9611 *lt9611)
{
	struct reg_sequence reg_cfg[] = {
		{ 0x8130, 0x6a },
		{ 0x8131, 0x44 }, /* HDMI DC mode */
		{ 0x8132, 0x4a },
		{ 0x8133, 0x0b },
		{ 0x8134, 0x00 },
		{ 0x8135, 0x00 },
		{ 0x8136, 0x00 },
		{ 0x8137, 0x44 },
		{ 0x813f, 0x0f },
		{ 0x8140, 0x98 },
		{ 0x8141, 0x98 }, //clk swing
		{ 0x8142, 0x98 }, //D0 swing
		{ 0x8143, 0x98 }, //D1 swing
		{ 0x8144, 0x0a }, //D2 swing
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static void lt9611_LowPower_mode(struct lt9611 *lt9611, bool on)
{
	/* only hpd irq is working for low power consumption */
	/* 1.8V: 15 mA */
	if (on) {
		regmap_write(lt9611->regmap, 0x8102, 0x49);
		regmap_write(lt9611->regmap, 0x8123, 0x80);
		//0x00 --> 0xc0, tx phy and clk can not power down, otherwise dc det don't work
		regmap_write(lt9611->regmap, 0x8130, 0x00);
		regmap_write(lt9611->regmap, 0x8011, 0x0a);
	} else {
		regmap_write(lt9611->regmap, 0x8102, 0x12);
		regmap_write(lt9611->regmap, 0x8123, 0x40);
		regmap_write(lt9611->regmap, 0x8130, 0xea);
		regmap_write(lt9611->regmap, 0x8011, 0xfa);
	}
}

static int lt9611_read_video_check(struct lt9611 *lt9611, unsigned int reg)
{
	unsigned int temp, temp2;
	int ret;

	ret = regmap_read(lt9611->regmap, reg, &temp);
	if (ret)
		return ret;
	
	temp <<= 8;
	ret = regmap_read(lt9611->regmap, reg + 1, &temp2);
	if (ret)
		return ret;

	return (temp + temp2);
}

static int lt9611_video_check(struct lt9611 *lt9611)
{
	u32 v_total, vactive, hactive_a, hactive_b, h_total_sysclk;
	int temp;
	unsigned int mipi_video_format = 0;
	video_format_id i;

	/* top module video check */

	/* vactive */
	temp = lt9611_read_video_check(lt9611, 0x8282);
	if (temp < 0)
		goto end;
	vactive = temp;

	/* v_total */
	temp = lt9611_read_video_check(lt9611, 0x826c);
	if (temp < 0)
		goto end;
	v_total = temp;

	/* h_total_sysclk */
	temp = lt9611_read_video_check(lt9611, 0x8286);
	if (temp < 0)
		goto end;
	h_total_sysclk = temp;

	/* hactive_a */
	temp = lt9611_read_video_check(lt9611, 0x8382);
	if (temp < 0)
		goto end;
	hactive_a = temp / 3;

	/* hactive_b */
	temp = lt9611_read_video_check(lt9611, 0x8386);
	if (temp < 0)
		goto end;
	hactive_b = temp / 3;

	regmap_read(lt9611->regmap, 0x8388, &mipi_video_format);

	dev_err(lt9611->dev,
		 "video check: hactive_a=%d, hactive_b=%d, vactive=%d, v_total=%d, h_total_sysclk=%d, mipi_video_format=%d\n",
		 hactive_a, hactive_b, vactive, v_total, h_total_sysclk, mipi_video_format);

	// Adaptive Resolution
	for (i = 0; i < VIDEO_INDEX_MAX; i++) {
		if ((hactive_a == lt9611_modes[i].h_active) && (vactive == lt9611_modes[i].v_active)) {
			lt9611->video_format_id = i;
			break;
		}
	}

	if (lt9611->video_format_id == VIDEO_3840x2160_30HZ) {
		regmap_write(lt9611->regmap, 0x8302, 0x10);
		regmap_write(lt9611->regmap, 0x8306, 0x10);
	}

	return 0;

end:
	dev_err(lt9611->dev, "read video check error\n");
	return temp;
}

u8 pcr_m_ex = 0x00;
static int lt9611_pll_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int *postdiv)
{
	u32 pclk;
	u8 pcr_m;
	u8 hdmi_post_div;
	unsigned int pll_lock_flag ,cal_done_flag ,band_out;
	u8 i;
	struct lt9611_mode *cfg;
	const struct reg_sequence reg_cfg[] = {
		/* txpll init */
		{ 0x8123, 0x40 },
		{ 0x8124, 0x62 },
		{ 0x8125, 0x80 },
		{ 0x8126, 0x55 },
		{ 0x812c, 0x37 },
		{ 0x812f, 0x01 },
		{ 0x8127, 0x66 },
		{ 0x8128, 0x88 },
		{ 0x812a, 0x20 },
	};

	cfg = &lt9611_modes[lt9611->video_format_id];
	pclk = cfg->pclk_khz;

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (pclk > 150000) {
		regmap_write(lt9611->regmap, 0x812d, 0x88);
		*postdiv = 1;
	} else if (pclk > 70000) {
		regmap_write(lt9611->regmap, 0x812d, 0x99);
		*postdiv = 2;
	} else {
		regmap_write(lt9611->regmap, 0x812d, 0xaa);
		*postdiv = 4;
	}

	hdmi_post_div = *postdiv;

	pcr_m = (u8)((pclk * 5 * hdmi_post_div) / 27000);
	pcr_m --;
	pcr_m_ex = pcr_m;

	regmap_write(lt9611->regmap, 0x832d, 0x40); //M up limit
	regmap_write(lt9611->regmap, 0x8331, 0x08); //M down limit
	regmap_write(lt9611->regmap, 0x8326, 0x80 | pcr_m);
	lt9611->pcr_m = pcr_m;

	pclk = pclk / 2;
	regmap_write(lt9611->regmap, 0x82e3, pclk / 65536); //13.5M

	pclk = pclk % 65536;
	regmap_write(lt9611->regmap, 0x82e4, pclk / 256);
	regmap_write(lt9611->regmap, 0x82e5, pclk % 256);
	regmap_write(lt9611->regmap, 0x82de, 0x20);
	regmap_write(lt9611->regmap, 0x82de, 0xe0);

	regmap_write(lt9611->regmap, 0x8011, 0x5a);
	regmap_write(lt9611->regmap, 0x8011, 0xfa); /* Pcr clk reset */
	regmap_write(lt9611->regmap, 0x8016, 0xf2);	
	regmap_write(lt9611->regmap, 0x8018, 0xdc);
	regmap_write(lt9611->regmap, 0x8018, 0xfc); /* pll analog reset */
	regmap_write(lt9611->regmap, 0x8016, 0xf3);

	/* pll lock status */
	for (i = 0; i < 5; i++) {
		regmap_write(lt9611->regmap, 0x8016, 0xe3); /* pll lock logic reset */
		regmap_write(lt9611->regmap, 0x8016, 0xf3);
		regmap_read(lt9611->regmap, 0x8215, &pll_lock_flag);
		regmap_read(lt9611->regmap, 0x82e6, &band_out);
		regmap_read(lt9611->regmap, 0x82e7, &cal_done_flag);

		if((pll_lock_flag & 0x80) && (cal_done_flag & 0x80) && (band_out != 0xff)) {
			break;
		} else {
			regmap_write(lt9611->regmap, 0x8011, 0x5a);
			msleep(10);
			regmap_write(lt9611->regmap, 0x8011, 0xfa); /* Pcr clk reset */
			regmap_write(lt9611->regmap, 0x8016, 0xf2); /* pll cal reset*/	
			regmap_write(lt9611->regmap, 0x8018, 0xdc);
			msleep(10);
			regmap_write(lt9611->regmap, 0x8018, 0xfc); /* pll analog reset */
			regmap_write(lt9611->regmap, 0x8016, 0xf3);
			msleep(10);
		}
	}

	return 0;
}

static void lt9611_pcr_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int postdiv)
{
	video_format_id video_format_id = lt9611->video_format_id;
	struct lt9611_mode *cfg;
	u8 POL;
	u16 hact;

	cfg = &lt9611_modes[video_format_id];
	hact = cfg->h_active;
	POL = (cfg-> h_polarity)*0x02 + (cfg-> v_polarity);
	POL = ~POL;
	POL &= 0x03;

	hact = (hact>>2);
	hact += 0x50;
	hact = (0x3e0>hact ? hact:0x3e0);

	// single port
	regmap_write(lt9611->regmap, 0x830b, 0x01); //vsync mode
	regmap_write(lt9611->regmap, 0x830c, 0x10); //=1/4 hact
	regmap_write(lt9611->regmap, 0x8348, 0x00); //de mode delay
	regmap_write(lt9611->regmap, 0x8349, 0x81); //=1/4 hact

	regmap_write(lt9611->regmap, 0x8321, 0x4a); //bit[3:0] step[11:8]
	regmap_write(lt9611->regmap, 0x8324, 0x71); //bit[7:4]v/h/de mode; line for clk
	regmap_write(lt9611->regmap, 0x8325, 0x30); //line for clk stb[7:0]
	regmap_write(lt9611->regmap, 0x832a, 0x01); //clk stable in

	regmap_write(lt9611->regmap, 0x834a, 0x40); //offset //0x10
	regmap_write(lt9611->regmap, 0x831d, 0x10|POL); //PCR de mode step setting.

	if (video_format_id == VIDEO_1024x600_60HZ) {
		regmap_write(lt9611->regmap, 0x8324, 0x70); //bit[7:4]v/h/de mode; line for clk stb[11:8]
		regmap_write(lt9611->regmap, 0x8325, 0x80); //line for clk stb[7:0]
		regmap_write(lt9611->regmap, 0x832a, 0x10); //clk stable in

		regmap_write(lt9611->regmap, 0x8323, 0x04); //pcr h mode step
		regmap_write(lt9611->regmap, 0x834a, 0x10); //offset //0x10
		regmap_write(lt9611->regmap, 0x831d, 0xf0); //PCR de mode step setting.
	}
}

static void lt9611_mipi_video_setup(struct lt9611 *lt9611,
				    const struct drm_display_mode *mode)
{
	u32 h_total, h_act, hpw, hfp, hss;
	u32 v_total, v_act, vpw, vfp, vss;
	video_format_id video_id;
	struct lt9611_mode *cfg;

	video_id = lt9611->video_format_id;
	cfg = &lt9611_modes[video_id];

	h_total = cfg->h_active + cfg->h_front_porch + cfg->h_pulse_width + cfg->h_back_porch;
	v_total = cfg->v_active + cfg->v_front_porch + cfg->v_pulse_width + cfg->v_back_porch;

	h_act = cfg->h_active;
	hpw = cfg->h_pulse_width;
	hfp = cfg->h_front_porch;
	hss = cfg->h_pulse_width + cfg->h_back_porch;

	v_act = cfg->v_active;
	vpw = cfg->v_pulse_width;
	vfp = cfg->v_front_porch;
	vss = cfg->v_pulse_width + cfg->v_back_porch;

	regmap_write(lt9611->regmap, 0x830d, (u8)(v_total / 256));
	regmap_write(lt9611->regmap, 0x830e, (u8)(v_total % 256));

	regmap_write(lt9611->regmap, 0x830f, (u8)(v_act / 256));
	regmap_write(lt9611->regmap, 0x8310, (u8)(v_act % 256));

	regmap_write(lt9611->regmap, 0x8311, (u8)(h_total / 256));
	regmap_write(lt9611->regmap, 0x8312, (u8)(h_total % 256));

	regmap_write(lt9611->regmap, 0x8313, (u8)(h_act / 256));
	regmap_write(lt9611->regmap, 0x8314, (u8)(h_act % 256));

	regmap_write(lt9611->regmap, 0x8315, (u8)(vpw % 256));
	regmap_write(lt9611->regmap, 0x8316, (u8)(hpw % 256));

	regmap_write(lt9611->regmap, 0x8317, (u8)(vfp % 256));
	regmap_write(lt9611->regmap, 0x8318, (u8)(vss % 256));
	regmap_write(lt9611->regmap, 0x8319, (u8)(hfp % 256));

	regmap_write(lt9611->regmap, 0x831a, (u8)(((hfp/256)<<4)+(hss/256)));
	regmap_write(lt9611->regmap, 0x831b, (u8)(hss % 256));
}

static int lt9611_pcr_start(struct lt9611 *lt9611)
{
	regmap_write(lt9611->regmap, 0x8011, 0x5a);
	msleep(10);
	regmap_write(lt9611->regmap, 0x8011, 0xfa); //Pcr reset

	return 0;
}

static void lt9611_hdmi_tx_digital(struct lt9611 *lt9611, bool is_hdmi)
{
	struct lt9611_mode *cfg;
	video_format_id video_id;
	u32 vic;
	u8 AR = 0x02;
	u8 pb0,pb2,pb4;
	u8 infoFrame_en;

	video_id = lt9611->video_format_id;

	cfg = &lt9611_modes[video_id];
	vic = cfg->vic;

	infoFrame_en = (0x02|0x08);
	pb2 =  (AR<<4) + 0x08;
	if(vic == 95) {
		pb4 = 0x00;
	} else {
		pb4 =  vic;
	}

	pb0 = (((pb2 + pb4) <= 0x5f)?(0x5f - pb2 - pb4):(0x15f - pb2 - pb4));

	if (is_hdmi)
		regmap_write(lt9611->regmap, 0x82d6, 0x8e);
	else
		regmap_write(lt9611->regmap, 0x82d6, 0x0e);

	// audio_i2s
	if (lt9611->audio_out_intf == I2S) {
		regmap_write(lt9611->regmap, 0x82d7, 0x04);
	}

	// audio_spdif
	if (lt9611->audio_out_intf == SPDIF) {
		regmap_write(lt9611->regmap, 0x82d7, 0x80);
	}

	regmap_write(lt9611->regmap, 0x8443, pb0); //AVI_PB0
	regmap_write(lt9611->regmap, 0x8445, pb2); //AVI_PB2
	regmap_write(lt9611->regmap, 0x8447, pb4); //AVI_PB4

	regmap_write(lt9611->regmap, 0x8410, 0x02); //data iland
	regmap_write(lt9611->regmap, 0x8412, 0x64); //act_h_blank

	if(vic == 95) {
		regmap_write(lt9611->regmap, 0x843d, infoFrame_en|0x20);
		regmap_write(lt9611->regmap, 0x8474, 0x81); //HB0
		regmap_write(lt9611->regmap, 0x8475, 0x01); //HB1
		regmap_write(lt9611->regmap, 0x8476, 0x05); //HB2
		regmap_write(lt9611->regmap, 0x8477, 0x49); //PB0
		regmap_write(lt9611->regmap, 0x8478, 0x03); //PB1
		regmap_write(lt9611->regmap, 0x8479, 0x0c); //PB2
		regmap_write(lt9611->regmap, 0x847a, 0x00); //PB3
		regmap_write(lt9611->regmap, 0x847b, 0x20); //PB4
		regmap_write(lt9611->regmap, 0x847c, 0x01); //PB5
	} else {
		regmap_write(lt9611->regmap, 0x843d, infoFrame_en);
	}
}

static void lt9611_HDP_Interrupt_Handle(struct lt9611 *lt9611)
{
	unsigned int postdiv, reg;

	/* Disable HDMI output */
	regmap_write(lt9611->regmap, 0x8130, 0x00);
	regmap_write(lt9611->regmap, 0x8123, 0x80);

	regmap_read(lt9611->regmap, 0x825e, &reg);
	if((reg & 0x04) == 0x04) {
		msleep(10);
		regmap_read(lt9611->regmap, 0x825e, &reg);

		if((reg & 0x04) == 0x04) {
			lt9611_LowPower_mode(lt9611, 0);
			msleep(100);
			lt9611_video_check(lt9611);
			lt9611_pll_setup(lt9611, NULL, &postdiv);
			lt9611_pcr_setup(lt9611, NULL, 0);
			lt9611_mipi_video_setup(lt9611, NULL);
			regmap_write(lt9611->regmap, 0x8326, pcr_m_ex);
			lt9611_pcr_start(lt9611);
			lt9611_hdmi_tx_digital(lt9611, true);

			/* Enable HDMI output */
			regmap_write(lt9611->regmap, 0x8123, 0x40);
			regmap_write(lt9611->regmap, 0x82de, 0x20);
			regmap_write(lt9611->regmap, 0x82de, 0xe0);
			regmap_write(lt9611->regmap, 0x8018, 0xdc);
			regmap_write(lt9611->regmap, 0x8018, 0xfc);
			regmap_write(lt9611->regmap, 0x8016, 0xf1);
			regmap_write(lt9611->regmap, 0x8016, 0xf3);
			regmap_write(lt9611->regmap, 0x8011, 0x5a); //Pcr reset
			regmap_write(lt9611->regmap, 0x8011, 0xfa);
			regmap_write(lt9611->regmap, 0x8130, 0xea);
		}
	} else {
		lt9611_LowPower_mode(lt9611, 1);
	}
}

static void lt9611_power_on(struct lt9611 *lt9611)
{
	if (lt9611->power_on)
		return;

	lt9611_sys_init(lt9611);

	lt9611_mipi_input_analog(lt9611);
	lt9611_mipi_input_digital(lt9611, NULL);
	lt9611_Audio_Init(lt9611);
	lt9611_hdmi_tx_phy(lt9611);

	//init and enable hpd interrupt
	regmap_write(lt9611->regmap, 0x8258, 0x0a); //Det HPD 0x0a --> 0x08
	regmap_write(lt9611->regmap, 0x8259, 0x00); //HPD debounce width
	regmap_write(lt9611->regmap, 0x8207, 0xff); //clear3
	regmap_write(lt9611->regmap, 0x8207, 0x3f); //clear3
	regmap_write(lt9611->regmap, 0x8203, 0x3f); //mask3  //Tx_det

	//cec init and enable cec interrupt
	regmap_write(lt9611->regmap, 0x800d, 0xff);
	regmap_write(lt9611->regmap, 0x8015, 0xf1); //reset cec logic
	regmap_write(lt9611->regmap, 0x8015, 0xf9);
	regmap_write(lt9611->regmap, 0x86fe, 0xa5); //clk div

	regmap_write(lt9611->regmap, 0x86fa, 0x00); //cec interrup mask
	regmap_write(lt9611->regmap, 0x86fc, 0x7f); //cec irq clr
	regmap_write(lt9611->regmap, 0x86fc, 0x00);
	regmap_write(lt9611->regmap, 0x8201, 0x7f); //mask bit[7]
	regmap_write(lt9611->regmap, 0x8205, 0xff); //clr bit[7]
	regmap_write(lt9611->regmap, 0x8205, 0x7f);

	msleep(200);
	lt9611->power_on = true;
}

static void lt9611_on(struct lt9611 *lt9611, bool on)
{
	if (on) {
		lt9611_power_on(lt9611);
		lt9611_HDP_Interrupt_Handle(lt9611);
	} else {
		/* Disable HDMI output */
		regmap_write(lt9611->regmap, 0x8130, 0x00);
		regmap_write(lt9611->regmap, 0x8123, 0x80);
	}
}

static irqreturn_t lt9611_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611 *lt9611 = dev_id;
	unsigned int hpd_status = 0;
	unsigned int irq_flag0, irq_flag1;
	unsigned int cec_status;

	lt9611_lock(lt9611);

	regmap_read(lt9611->regmap, 0x820d, &irq_flag0); // cec interrupt
	regmap_read(lt9611->regmap, 0x820f, &irq_flag1); // hpd interrupt

	//0x80 disconnected, 0x40 connected
	if (irq_flag1 & 0xc0) {
		regmap_write(lt9611->regmap, 0x8207, 0xff); //clear hpd interrupt
		regmap_write(lt9611->regmap, 0x8207, 0x3f); //clear hpd interrupt

		regmap_read(lt9611->regmap, 0x825e, &hpd_status);
		if ((hpd_status & 0x04) == 0x04) {
			msleep(10);
			regmap_read(lt9611->regmap, 0x825e, &hpd_status);
			if ((hpd_status & 0x04) == 0x04) {
				lt9611->hdmi_connected = true;
				lt9611->edid_read_en = false;
				cec_queue_pin_hpd_event(lt9611->cec_adapter, true, 0);
			}
		} else {
			lt9611->hdmi_connected = false;
			cec_notifier_phys_addr_invalidate(lt9611->cec_notifier);
			cec_queue_pin_hpd_event(lt9611->cec_adapter, false, 0);
		}
		wake_up_all(&lt9611->wq);
		schedule_work(&lt9611->work);
	}

	if ((irq_flag0 & 0x80) == 0x80) {
		regmap_read(lt9611->regmap, 0x86d2, &cec_status);

		lt9611->cec_status = cec_status;

		regmap_write(lt9611->regmap, 0x86fc, 0x7f); //cec irq clr
		regmap_write(lt9611->regmap, 0x86fc, 0x00);

		regmap_write(lt9611->regmap, 0x8205, 0xff); //clear3
		regmap_write(lt9611->regmap, 0x8205, 0x7f);

		if (lt9611->cec_status) {
			schedule_work(&lt9611->cec_transmit_work);
			schedule_work(&lt9611->cec_recv_work);
		}
	}

	lt9611_unlock(lt9611);

	return IRQ_HANDLED;
}

static void lt9611_release_edid(struct lt9611 *lt9611)
{
	dev_info(lt9611->dev, "release edid\n");
	kfree(lt9611->edid);
	lt9611->edid = NULL;
	drm_connector_update_edid_property(&lt9611->connector, NULL);
}

static void lt9611_helper_hotplug_event(struct lt9611 *lt9611)
{
	struct drm_device *dev = NULL;
	char name[32], status[32];
	char *event_string = "HOTPLUG=1";
	char *envp[5];

	dev = lt9611->connector.dev;

	scnprintf(name, 32, "name=%s",
		lt9611->connector.name);
	scnprintf(status, 32, "status=%s",
		drm_get_connector_status_name(lt9611->connector.status));
	envp[0] = name;
	envp[1] = status;
	envp[2] = event_string;
	envp[3] = NULL;
	envp[4] = NULL;

	dev_info(lt9611->dev, "[%s]:[%s]\n", name, status);
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE,
		envp);
}
static void lt9611_hpd_work(struct work_struct *work)
{
	struct lt9611 *lt9611 = container_of(work, struct lt9611, work);
	bool connected;

	if (lt9611->connector.dev) {
		lt9611->connector.status = (lt9611->hdmi_connected) ?
				connector_status_connected: connector_status_disconnected;
		if (lt9611->connector.status == connector_status_disconnected)
			lt9611_release_edid(lt9611);
		lt9611_helper_hotplug_event(lt9611);
	} else {

		mutex_lock(&lt9611->ocm_lock);
		connected = lt9611->hdmi_connected;
		mutex_unlock(&lt9611->ocm_lock);

		drm_bridge_hpd_notify(&lt9611->bridge,
				      connected ?
				      connector_status_connected :
				      connector_status_disconnected);
	}
}

static void lt9611_reset(struct lt9611 *lt9611)
{
	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(200);
}

static void lt9611_assert_5v(struct lt9611 *lt9611)
{
	if ((!lt9611->enable_gpio) || (!lt9611->ocb_gpio))
		return;

	gpiod_set_value_cansleep(lt9611->enable_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(lt9611->ocb_gpio, 1);
	msleep(20);
}

static int lt9611_regulator_init(struct lt9611 *lt9611)
{
	int ret;

	lt9611->supplies[0].supply = "vdd";
	lt9611->supplies[1].supply = "vcc";

	ret = devm_regulator_bulk_get(lt9611->dev, 2, lt9611->supplies);
	if (ret < 0)
		return ret;

	ret = regulator_set_voltage(lt9611->supplies[1].consumer, 3300000, 3500000);
	if (ret) {
		pr_err("%s:regulator set voltage failed %d", __func__, ret);
		return ret;
	}

	return regulator_set_load(lt9611->supplies[0].consumer, 200000);
}

static int lt9611_regulator_enable(struct lt9611 *lt9611)
{
	int ret;

	ret = regulator_enable(lt9611->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(1000, 10000); /* 50000 according to dtsi */

	ret = regulator_enable(lt9611->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static struct lt9611_mode *lt9611_find_mode(const struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lt9611_modes); i++) {
		if (lt9611_modes[i].h_active == mode->hdisplay &&
		    lt9611_modes[i].h_total == mode->htotal &&
		    lt9611_modes[i].v_active == mode->vdisplay &&
		    lt9611_modes[i].v_total == mode->vtotal &&
		    lt9611_modes[i].v_refresh == drm_mode_vrefresh(mode)) {
			return &lt9611_modes[i];
		}
	}

	return NULL;
}

static struct mipi_dsi_device *lt9611_attach_dsi(struct lt9611 *lt9611,
						    struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		return ERR_PTR(ret);
	}

	return dsi;
}

#define MODE_SIZE(m) ((m)->hdisplay * (m)->vdisplay)
#define MODE_REFRESH_DIFF(c, t) (abs((c) - (t)))

static void lt9611_choose_best_mode(struct drm_connector *connector)
{
	struct drm_display_mode *t, *cur_mode, *preferred_mode;
	int cur_vrefresh, preferred_vrefresh;
	int target_refresh = 60;

	if (list_empty(&connector->probed_modes)) {
		return;
	}

	preferred_mode = list_first_entry(&connector->probed_modes,
			struct drm_display_mode, head);
	list_for_each_entry_safe(cur_mode, t, &connector->probed_modes, head) {
		cur_mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		if (cur_mode == preferred_mode) {
			continue;
		}

		/* Largest mode is preferred */
		if (MODE_SIZE(cur_mode) > MODE_SIZE(preferred_mode))
			preferred_mode = cur_mode;

		cur_vrefresh = drm_mode_vrefresh(cur_mode);
		preferred_vrefresh = drm_mode_vrefresh(preferred_mode);

		/* At a given size, try to get closest to target refresh */
		if ((MODE_SIZE(cur_mode) == MODE_SIZE(preferred_mode)) &&
			MODE_REFRESH_DIFF(cur_vrefresh, target_refresh) <
			MODE_REFRESH_DIFF(preferred_vrefresh, target_refresh) &&
			cur_vrefresh <= target_refresh) {
			preferred_mode = cur_mode;
		}
	}

	preferred_mode->type |= DRM_MODE_TYPE_PREFERRED;
}

static int lt9611_connector_get_modes(struct drm_connector *connector)
{
	struct lt9611 *lt9611 = connector_to_lt9611(connector);
	struct cec_notifier *notify = lt9611->cec_notifier;
	unsigned int count;

	lt9611->edid = lt9611->bridge.funcs->get_edid(&lt9611->bridge, connector);
	drm_connector_update_edid_property(connector, lt9611->edid);
	count = drm_add_edid_modes(connector, lt9611->edid);

	/* TODO: add checks for num_of_edid_ext_blk == 0 case */
	if (lt9611->cec_support)
		cec_notifier_set_phys_addr_from_edid(notify, lt9611->edid);

	return count;
}

static enum drm_connector_status lt9611_connector_detect(struct drm_connector *connector,
							    bool force)
{
	struct lt9611 *lt9611 = connector_to_lt9611(connector);

	return lt9611->bridge.funcs->detect(&lt9611->bridge);
}

static enum drm_mode_status lt9611_connector_mode_valid(struct drm_connector *connector,
							   struct drm_display_mode *mode)
{
	struct lt9611_mode *lt9611_mode = lt9611_find_mode(mode);

	return lt9611_mode ? MODE_OK : MODE_BAD;
}

static const struct drm_connector_helper_funcs lt9611_bridge_connector_helper_funcs = {
	.get_modes = lt9611_connector_get_modes,
	.mode_valid = lt9611_connector_mode_valid,
};

static const struct drm_connector_funcs lt9611_bridge_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = lt9611_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lt9611_connector_init(struct drm_bridge *bridge, struct lt9611 *lt9611)
{
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	lt9611->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&lt9611->connector,
				 &lt9611_bridge_connector_helper_funcs);
	ret = drm_connector_init(bridge->dev, &lt9611->connector,
				 &lt9611_bridge_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	ret = drm_connector_attach_encoder(&lt9611->connector, bridge->encoder);
	if (ret) {
		DRM_ERROR("Failed to link up connector to encoder: %d\n", ret);
		return ret;
	}

	/* Attach primary DSI */
	lt9611->dsi0 = lt9611_attach_dsi(lt9611, lt9611->dsi0_node);
	if (IS_ERR(lt9611->dsi0)) {
		ret = PTR_ERR(lt9611->dsi0);
		drm_bridge_remove(&lt9611->bridge);
		return ret;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611->dsi1_node) {
		lt9611->dsi1 = lt9611_attach_dsi(lt9611, lt9611->dsi1_node);
		if (IS_ERR(lt9611->dsi1)) {
			ret = PTR_ERR(lt9611->dsi1);
			drm_bridge_remove(&lt9611->bridge);
			return ret;
		}
	}

	return ret;
}

static int lt9611_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;

	ret = lt9611_connector_init(bridge, lt9611);
	if (ret < 0)
		return ret;

	return 0;
}

static enum drm_mode_status
lt9611_bridge_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct lt9611_mode *lt9611_mode;
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_mode = lt9611_find_mode(mode);

	//dev_info(lt9611->dev, "%s: %s: %d(%d:%d:%d:%d) x %d(%d:%d:%d:%d)-%d\n",
	//		lt9611_mode ? "MODE_OK": "MODE_BAD", mode->name,
	//		mode->hdisplay, mode->htotal, mode->hsync_start, mode->hsync_end, mode->hskew,
	//		mode->vdisplay, mode->vtotal, mode->vsync_start, mode->vsync_end, mode->vscan,
	//		drm_mode_vrefresh(mode));

	return lt9611_mode ? MODE_OK : MODE_BAD;
}

static void lt9611_bridge_enable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611;
	int rc;

	if (!bridge)
		return;

	pr_info("bridge enable\n");

	lt9611 = bridge_to_lt9611(bridge);
	if (lt9611->audio_support) {
		pr_err("notify audio(%d)\n", EXT_DISPLAY_CABLE_CONNECT);
		rc = hdmi_audio_register_ext_disp(lt9611);
		if (rc) {
			pr_err("hdmi audio register failed. rc=%d\n", rc);
			return;
		}
		lt9611->ext_audio_data.intf_ops.audio_config(lt9611->ext_pdev,
				&lt9611->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
		lt9611->ext_audio_data.intf_ops.audio_notify(lt9611->ext_pdev,
				&lt9611->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
	}
	lt9611_lock(lt9611);
	lt9611_on(lt9611, true);
	lt9611_unlock(lt9611);
}

static void lt9611_bridge_disable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611;

	if (!bridge)
		return;

	lt9611 = bridge_to_lt9611(bridge);
	dev_info(lt9611->dev, "bridge disable\n");

	lt9611_lock(lt9611);
	lt9611_on(lt9611, false);
	lt9611_unlock(lt9611);

	if (lt9611->audio_support) {
		dev_err(lt9611->dev, "notify audio(%d)\n", EXT_DISPLAY_CABLE_DISCONNECT);
		lt9611->ext_audio_data.intf_ops.audio_notify(lt9611->ext_pdev,
				&lt9611->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);
		lt9611->ext_audio_data.intf_ops.audio_config(lt9611->ext_pdev,
				&lt9611->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);
		hdmi_audio_deregister_ext_disp(lt9611);
	}
}

static void lt9611_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj_mode)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	dev_info(lt9611->dev, "hdisplay=%d, vdisplay=%d, clock=%d \n",
		mode->hdisplay, mode->vdisplay, mode->clock);
	drm_mode_copy(&lt9611->curr_mode, mode);
}

static enum drm_connector_status lt9611_bridge_detect(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	unsigned int reg_val = 0;
	int ret;
	bool connected = true;

	lt9611_lock(lt9611);

	if (lt9611->hpd_supported) {
		ret = regmap_read(lt9611->regmap, 0x825e, &reg_val);

		if (ret) {
			dev_info(lt9611->dev, "failed to read hpd status: %d\n", ret);
		} else {
			dev_info(lt9611->dev, "success to read hpd status: %d\n", reg_val);
			if ((reg_val & 0x04) == 0x04) {
				connected  = true;
			} else {
				connected  = false;
				lt9611_on(lt9611, true);
			}
		}
	}
	lt9611->hdmi_connected = connected;

	lt9611_unlock(lt9611);

	return connected ?  connector_status_connected :
				connector_status_disconnected;
}

static int lt9611_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct lt9611 *lt9611 = data;
	unsigned int i, j;
	unsigned int reg, extended_flag;

	if (len > EDID_BLOCK_SIZE)
		return -EINVAL;

	if (block >= EDID_NUM_BLOCKS)
		return -EINVAL;

	if (!lt9611->edid_read_en) {
		if (!lt9611->edid_read_sts) {
			lt9611_lock(lt9611);

			regmap_write(lt9611->regmap, 0x8503,0xc9);
			regmap_write(lt9611->regmap, 0x8504,0xA0); //0xA0 is EDID device address
			regmap_write(lt9611->regmap, 0x8505,0x00); //0x00 is EDID offset address
			regmap_write(lt9611->regmap, 0x8506,0x20); //length for read
			regmap_write(lt9611->regmap, 0x8514,0x7f);

			for(i = 0;i < 8;i ++) { // block 0
				regmap_write(lt9611->regmap, 0x8505,i*32); //0x00 is EDID offset address
				regmap_write(lt9611->regmap, 0x8507,0x36);
				regmap_write(lt9611->regmap, 0x8507,0x34); //0x31
				regmap_write(lt9611->regmap, 0x8507,0x37); //0x37
				msleep(5); // wait 5ms for reading edid data.

				regmap_read(lt9611->regmap, 0x8540, &reg);
				if(reg & 0x02) { //KEY_DDC_ACCS_DONE=1
					regmap_read(lt9611->regmap, 0x8540, &reg);
					if(reg & 0x50) { //DDC No Ack or Abitration lost
						goto end;
					} else {
						for(j = 0; j < 32; j ++) {
							regmap_read(lt9611->regmap, 0x8583, &reg);
							lt9611->edid_buf[i*32+j]= reg & 0xFF;
							if((i == 3) && ( j == 30)) {
								extended_flag = reg & 0x03;
							}
						}
						if(i == 3) {
							if(extended_flag < 1) { //no block 1, stop reading edid.
								goto end;
							}
						}
					}
				} else {
					goto end;
				}
			}

			lt9611->edid_read_sts = true;
			lt9611->edid_read_en = true;

end:
			regmap_write(lt9611->regmap, 0x8503,0xc2);
			regmap_write(lt9611->regmap, 0x8507,0x1f);

			lt9611_unlock(lt9611);
		}
	}

	if (block == 0) {
		memcpy(buf, lt9611->edid_buf, len);
	} else {
		memcpy(buf, lt9611->edid_buf + len, len);
		lt9611->edid_read_sts = false;
	}

	return 0;
};

static struct edid *lt9611_bridge_get_edid(struct drm_bridge *bridge,
					      struct drm_connector *connector)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;

	return drm_do_get_edid(connector, lt9611_get_edid_block, lt9611);
}

static const struct drm_bridge_funcs lt9611_bridge_funcs = {
	.attach = lt9611_bridge_attach,
	.mode_valid = lt9611_bridge_mode_valid,
	.mode_set = lt9611_bridge_mode_set,
	.detect = lt9611_bridge_detect,
	.get_edid = lt9611_bridge_get_edid,
	.enable = lt9611_bridge_enable,
	.disable = lt9611_bridge_disable,
};

static int lt9611_parse_dt(struct device *dev,
			      struct lt9611 *lt9611)
{
	lt9611->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611->dsi0_node) {
		dev_err(lt9611->dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	lt9611->audio_support = of_property_read_bool(dev->of_node, "lt,audio-support");
	dev_info(lt9611->dev, "audio support = %d\n", lt9611->audio_support);

	return 0;
}

static int lt9611_gpio_init(struct lt9611 *lt9611)
{
	struct device *dev = lt9611->dev;

	lt9611->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611->reset_gpio);
	}

	lt9611->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(lt9611->enable_gpio);
	}

	lt9611->ocb_gpio = devm_gpiod_get_optional(dev, "ocb",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611->ocb_gpio)) {
		dev_err(dev, "failed to acquire ocb gpio\n");
		return PTR_ERR(lt9611->ocb_gpio);
	}

	return 0;
}

static int lt9611_read_device_rev(struct lt9611 *lt9611)
{
	unsigned int rev0, rev1, rev2;
	int ret;

	lt9611_lock(lt9611);

	ret = regmap_read(lt9611->regmap, 0x8002, &rev0);
	ret |= regmap_read(lt9611->regmap, 0x8000, &rev1);
	ret |= regmap_read(lt9611->regmap, 0x8001, &rev2);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);
	else
		dev_err(lt9611->dev, "LT9611 revision: 0x%02x.%02x.%02x\n", rev0, rev1, rev2);

	lt9611_unlock(lt9611);

	return ret;
}

int lt9611_read_cec_msg(struct lt9611 *lt9611, struct cec_msg *msg)
{
	unsigned int cec_val, cec_len, i;
	int ret = 0;

	pr_info("%s: Enter \n", __func__);

	lt9611_lock(lt9611);
	regmap_write(lt9611->regmap, 0x86f5, 0x01); //lock rx data buff
	regmap_read(lt9611->regmap, 0x86d3, &cec_len);
	msg->len = (cec_len > CEC_MAX_MSG_SIZE) ? CEC_MAX_MSG_SIZE : cec_len;

	/* TODO: implement regmap_bulk_read */
	for (i = 0; i < cec_len; i++) {
		regmap_read(lt9611->regmap, 0x86d4 + i, &cec_val);
		msg->msg[i] = cec_val;
	}

	regmap_write(lt9611->regmap, 0x86f5, 0x00); //unlock rx data buff
	lt9611_unlock(lt9611);

	/* TODO hexdump */
	if (cec_len == 0) {
		ret = -EINVAL;
		pr_err("ERROR: CEC message length = %u, skip read CEC message.\n", cec_len);
	} else {
		for (i = 0; i < msg->len; i++)
			dev_err(lt9611->dev, "received msg[%d] = %x", i, msg->msg[i]);
	}

	return ret;
}

void lt9611_cec_transmit_work(struct work_struct *work)
{
	struct lt9611 *lt9611 = container_of(work, struct lt9611,
					cec_transmit_work);

	dev_info(lt9611->dev, "cec_status 0x%x : cec_tx_data %0x\n", lt9611->cec_status, lt9611->cec_tx_data);
	if ((lt9611->cec_tx_data == 0x44) || (lt9611->cec_tx_data == 0x88) || (lt9611->cec_tx_data == 0xbb)) {
		lt9611->cec_tx_data = 0x00;
		cec_transmit_attempt_done(lt9611->cec_adapter, CEC_TX_STATUS_NACK);

		return;
	}

	if (lt9611->cec_status & BIT(2))
		cec_transmit_attempt_done(lt9611->cec_adapter, CEC_TX_STATUS_NACK);
	else if (lt9611->cec_status & BIT(0))
		cec_transmit_attempt_done(lt9611->cec_adapter, CEC_TX_STATUS_OK);
}

void lt9611_cec_recv_work(struct work_struct *work)
{
	struct lt9611 *lt9611 = container_of(work, struct lt9611, cec_recv_work);
	struct cec_msg cec_msg = {};

	pr_info("%s: Enter \n", __func__);
	if (!lt9611->cec_status) {
		dev_err(lt9611->dev, "cec message is receiving\n");
		return;
	}

	if (lt9611_read_cec_msg(lt9611, &cec_msg) == 0)
		cec_received_msg(lt9611->cec_adapter, &cec_msg);
}

static int lt9611_cec_enable(struct cec_adapter *adap, bool enable)
{
	struct lt9611 *lt9611 = cec_get_drvdata(adap);

	lt9611->cec_en = enable;
	return 0;
}

static int lt9611_cec_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct lt9611 *lt9611 = cec_get_drvdata(adap);
	unsigned int reg0 = 0x00, reg1 = 0x80;

	pr_info("%s: Enter , cec_log_addr 0x%x\n", __func__, logical_addr);
	lt9611->cec_log_addr = logical_addr;
	if (logical_addr != CEC_LOG_ADDR_INVALID) {
		switch(logical_addr) {
			case 0:
				reg0 = 0x01;
				reg1 = 0x00;
				break;
			case 1:
				reg0 = 0x02;
				reg1 = 0x00;
				break;
			case 2:
				reg0 = 0x03;
				reg1 = 0x00;
				break;
			case 3:
				reg0 = 0x04;
				reg1 = 0x00;
				break;
			case 4:
				reg0 = 0x10;
				reg1 = 0x00;
				break;
			case 5:
				reg0 = 0x20;
				reg1 = 0x00;
				break;
			case 6:
				reg0 = 0x30;
				reg1 = 0x00;
				break;
			case 7:
				reg0 = 0x40;
				reg1 = 0x00;
				break;
			case 8:
				reg0 = 0x00;
				reg1 = 0x01;
				break;
			case 9:
				reg0 = 0x00;
				reg1 = 0x02;
				break;
			case 10:
				reg0 = 0x00;
				reg1 = 0x03;
				break;
			case 11:
				reg0 = 0x00;
				reg1 = 0x04;
				break;
			case 12:
				reg0 = 0x00;
				reg1 = 0x10;
				break;
			case 13:
				reg0 = 0x00;
				reg1 = 0x20;
				break;
			case 14:
				reg0 = 0x00;
				reg1 = 0x30;
				break;
			case 15:
				reg0 = 0x00;
				reg1 = 0x40;
				break;
			default:
				break;
		}

		lt9611_lock(lt9611);
		regmap_write(lt9611->regmap, 0x86f7, reg0);
		regmap_write(lt9611->regmap, 0x86f8, reg1);
		lt9611_unlock(lt9611);
	}

	return 0;
}

static int lt9611_cec_transmit(struct cec_adapter *adap, u8 attempts,
		u32 signal_free_time, struct cec_msg *msg)
{
	int i;
	unsigned int reg_cec_flag;
	struct lt9611 *lt9611 = cec_get_drvdata(adap);
	unsigned int len = (msg->len > CEC_MAX_MSG_SIZE) ? CEC_MAX_MSG_SIZE : msg->len;

	pr_info("%s: Enter \n", __func__);

	lt9611_lock(lt9611);
	regmap_write(lt9611->regmap, 0x86f5, 0x01); //lock rx data buff
	regmap_write(lt9611->regmap, 0x86f4, len);

	/* TODO check regmap_bulk_write */
	for (i = 0; i < len; i++)
		regmap_write(lt9611->regmap, 0x86e4 + i, msg->msg[i]);

	regmap_write(lt9611->regmap, 0x86f9, 0x03); //start send msg
	msleep(25 * i);
	regmap_write(lt9611->regmap, 0x86f5, 0x00); //unlock rx data buff
	regmap_write(lt9611->regmap, 0x86f9, 0x02);
	lt9611_unlock(lt9611);

	lt9611->cec_tx_data = msg->msg[0];

	/* TODO: check hexdump */
	for (i = 0; i < len; i++)
		dev_err(lt9611->dev, "cec transmit msg[%d] = %x\n", i, msg->msg[i]);

	return 0;
}

struct cec_adap_ops lt9611_cec_ops = {
	.adap_enable = lt9611_cec_enable,
	.adap_log_addr = lt9611_cec_log_addr,
	.adap_transmit = lt9611_cec_transmit,
};

static int lt9611_cec_adap_init(struct lt9611 *lt9611)
{
	int ret = 0;
	struct cec_adapter *adap = NULL;
	unsigned int cec_flags = CEC_CAP_DEFAULTS;

	adap = cec_allocate_adapter(&lt9611_cec_ops, lt9611,
			"lt9611_cec", cec_flags, 1);
	if (!adap) {
		dev_err(lt9611->dev, "cec adapter allocate failed\n");
		return -ENOMEM;
	}

	lt9611->cec_notifier = cec_notifier_cec_adap_register(lt9611->dev,
						NULL, adap);
	if (!lt9611->cec_notifier) {
		dev_err(lt9611->dev, "get cec notifier failed\n");
		cec_delete_adapter(adap);
		lt9611->cec_adapter = NULL;
		lt9611->cec_support = false;
		lt9611->cec_en = false;
		return -ENOMEM;
	}

	ret = cec_register_adapter(adap, lt9611->dev);
	if (ret != 0) {
		dev_err(lt9611->dev, "register cec adapter failed\n");
		cec_delete_adapter(adap);
		lt9611->cec_adapter = NULL;
		lt9611->cec_support = false;
		lt9611->cec_en = false;
	} else {
		dev_err(lt9611->dev, "CEC adapter registered\n");
		lt9611->cec_en = true;
		lt9611->cec_support = true;
		lt9611->cec_log_addr = CEC_LOG_ADDR_PLAYBACK_1;

		lt9611->cec_adapter = adap;
		cec_s_log_addrs(lt9611->cec_adapter, NULL, false);
	}

	return ret;
}

static int lt9611_hdmi_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *fmt,
				    struct hdmi_codec_params *hparms)
{
	/*
	 * LT9611 will automatically detect rate and sample size, so no need
	 * to setup anything here.
	 */
	return 0;
}

static void lt9611_audio_shutdown(struct device *dev, void *data)
{

}

static int lt9611_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
					 struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static const struct hdmi_codec_ops lt9611_codec_ops = {
	.hw_params	= lt9611_hdmi_hw_params,
	.audio_shutdown = lt9611_audio_shutdown,
	.get_dai_id	= lt9611_hdmi_i2s_get_dai_id,
};

static int lt9611_audio_init(struct device *dev, struct lt9611 *lt9611)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &lt9611_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
		.data = lt9611,
	};

	lt9611->audio_pdev =
		platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					      PLATFORM_DEVID_AUTO,
					      &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(lt9611->audio_pdev);
}

static void lt9611_audio_exit(struct lt9611 *lt9611)
{
	if (lt9611->audio_pdev) {
		platform_device_unregister(lt9611->audio_pdev);
		lt9611->audio_pdev = NULL;
	}
}

static ssize_t edid_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lt9611 *lt9611 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "curr_mode: hdisplay x vdisplay x vrefresh = %dx%dx%d\n",
			lt9611->curr_mode.hdisplay, lt9611->curr_mode.vdisplay,
			drm_mode_vrefresh(&lt9611->curr_mode));
};

static ssize_t edid_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	int hdisplay = 0, vdisplay = 0, vrefresh = 0;
	struct lt9611 *lt9611 = dev_get_drvdata(dev);

	if (!lt9611)
		return -EINVAL;

	if (sscanf(buf, "%d %d %d", &hdisplay, &vdisplay, &vrefresh) != 3)
		goto err;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto err;

	lt9611->fix_mode = true;
	lt9611->debug_mode.h_active = hdisplay;
	lt9611->debug_mode.v_active = vdisplay;
	lt9611->debug_mode.v_refresh = vrefresh;

	dev_info(lt9611->dev, "fixed mode hdisplay=%d vdisplay=%d, vrefresh=%d\n",
			hdisplay, vdisplay, vrefresh);

	return count;
err:
	lt9611->fix_mode = false;
	return -EINVAL;
}

static ssize_t send_cec_msg_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	int i, value;
	u8 reg_cec_flag;
	u8 len = (u8) strlen(buf);
	__u8 msg[CEC_MAX_MSG_SIZE];
	size_t j;
	char hex[3];
	struct lt9611 *lt9611 = dev_get_drvdata(dev);

	if (!lt9611) {
		pr_err("lt9611 is NULL\n");
		return -EINVAL;
	}

	memset(msg, 0, CEC_MAX_MSG_SIZE);

	len = (len > CEC_MAX_MSG_SIZE) ? 16 : len;
	len = len / 2;
	for (j = 0; j < len; j++) {
        strncpy(hex, buf + j*2, 2);
        hex[2] = '\0';

		sscanf(hex, "%x", &value);
		msg[j] = (__u8)value;
    }

	lt9611_lock(lt9611);

	regmap_write(lt9611->regmap, 0x86f5, 0x01); //lock rx data buff
	regmap_write(lt9611->regmap, 0x86f4, len);

	for (i = 0; i < len; i++)
		regmap_write(lt9611->regmap, 0x86e4 + i, msg[i]);

	regmap_write(lt9611->regmap, 0x86f9, 0x03); //start send msg
	msleep(25 * i);
	regmap_write(lt9611->regmap, 0x86f5, 0x00); //unlock rx data buff
	regmap_write(lt9611->regmap, 0x86f9, 0x02);

	lt9611_unlock(lt9611);

	/* To check what message sent */
	for (i = 0; i < len; i++)
		pr_err("lt9611 transmitting msg[%d] = %x\n", i, msg[i]);

	return count;
}

static DEVICE_ATTR_RW(edid_mode);
static DEVICE_ATTR_WO(send_cec_msg);

static struct attribute *lt9611_attrs[] = {
	&dev_attr_edid_mode.attr,
	&dev_attr_send_cec_msg.attr,
	NULL,
};

static const struct attribute_group lt9611_attr_group = {
	.attrs = lt9611_attrs,
};

static const struct attribute_group *lt9611_attr_groups[] = {
	&lt9611_attr_group,
	NULL,
};

static int lt9611_probe(struct i2c_client *client)
{
	struct lt9611 *lt9611;
	struct device *dev = &client->dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611 = devm_kzalloc(dev, sizeof(*lt9611), GFP_KERNEL);
	if (!lt9611)
		return -ENOMEM;

	lt9611->dev = dev;
	lt9611->client = client;
	mutex_init(&lt9611->ocm_lock);

	lt9611->regmap = devm_regmap_init_i2c(client, &lt9611_regmap_config);
	if (IS_ERR(lt9611->regmap)) {
		dev_err(lt9611->dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611->regmap);
	}

	ret = lt9611_parse_dt(dev, lt9611);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611_gpio_init(lt9611);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611_regulator_init(lt9611);
	if (ret < 0)
		goto err_of_put;

	lt9611_assert_5v(lt9611);

	ret = lt9611_regulator_enable(lt9611);
	if (ret)
		goto err_of_put;

	lt9611->video_format_id = VIDEO_1920x1080_60HZ;
	lt9611->mipi_lane_counts = MIPI_4LANE;
	lt9611->mipi_port_counts = MIPI_1PORT;
	lt9611->audio_out_intf = I2S;
	lt9611->power_on = false;

	lt9611_reset(lt9611);

	ret = lt9611_read_device_rev(lt9611);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
	} else {
		lt9611->hpd_supported = true;
	}

	init_waitqueue_head(&lt9611->wq);
	INIT_WORK(&lt9611->work, lt9611_hpd_work);
	INIT_WORK(&lt9611->cec_recv_work, lt9611_cec_recv_work);
	INIT_WORK(&lt9611->cec_transmit_work, lt9611_cec_transmit_work);

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					lt9611_irq_thread_handler,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "lt9611", lt9611);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
	}

	i2c_set_clientdata(client, lt9611);

	lt9611->bridge.funcs = &lt9611_bridge_funcs;
	lt9611->bridge.of_node = client->dev.of_node;
	lt9611->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID | DRM_BRIDGE_OP_MODES;
	if (lt9611->hpd_supported)
		lt9611->bridge.ops |= DRM_BRIDGE_OP_HPD;

	ret = lt9611_cec_adap_init(lt9611);
	if (ret)
		dev_err(dev, "CEC init failed. ret=%d\n", ret);
	else
		dev_info(dev, "CEC init success\n");

	lt9611->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&lt9611->bridge);

	return lt9611_audio_init(dev, lt9611);

err_of_put:
	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);

	return ret;
}

static void lt9611_remove(struct i2c_client *client)
{
	struct lt9611 *lt9611 = i2c_get_clientdata(client);

	disable_irq(client->irq);
	cancel_work_sync(&lt9611->work);
	lt9611_audio_exit(lt9611);
	drm_bridge_remove(&lt9611->bridge);
	/* TODO check order of closing wq */
	cancel_work_sync(&lt9611->cec_recv_work);
	cancel_work_sync(&lt9611->cec_transmit_work);
	if (lt9611->cec_adapter)
		cec_unregister_adapter(lt9611->cec_adapter);

	mutex_destroy(&lt9611->ocm_lock);

	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);
}

static struct i2c_device_id lt9611_id[] = {
	{ "lt,lt9611", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611_match_table[] = {
	{ .compatible = "lt,lt9611" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt9611_match_table);

static struct i2c_driver lt9611_driver = {
	.driver = {
		.name = "lt9611",
		.of_match_table = lt9611_match_table,
		.dev_groups = lt9611_attr_groups,
	},
	.probe = lt9611_probe,
	.remove = lt9611_remove,
	.id_table = lt9611_id,
};
module_i2c_driver(lt9611_driver);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_LICENSE("GPL v2");
