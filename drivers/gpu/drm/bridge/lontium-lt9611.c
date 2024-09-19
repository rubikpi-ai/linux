// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>

#include <sound/hdmi-codec.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_file.h>

#define EDID_SEG_SIZE	256
#define EDID_LEN	32
#define EDID_LOOP	8
#define KEY_DDC_ACCS_DONE 0x02
#define DDC_NO_ACK	0x50

#define LT9611_4LANES	0

struct lt9611_video_cfg {
	u32 h_front_porch;
	u32 h_pulse_width;
	u32 h_back_porch;
	u32 h_active;
	u32 v_front_porch;
	u32 v_pulse_width;
	u32 v_back_porch;
	u32 v_active;
	bool h_polarity;
	bool v_polarity;
	u32 vic;
	u32 pclk_khz;
};

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
	VIDEO_640x480_60HZ = 0,
	VIDEO_720x480_60HZ,
	VIDEO_1024x600_60HZ,
	VIDEO_1280x720_60HZ,
	VIDEO_1920x1080_30HZ,
	VIDEO_1920x1080_60HZ,
	VIDEO_2560x1440_60HZ,
	VIDEO_3840x1080_60HZ,
	VIDEO_3840x2160_30HZ,
	VIDEO_INDEX_MAX
} video_format_id;

struct lt9611 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;

	struct regmap *regmap;
	struct mutex ocm_lock;

	struct work_struct work;

	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct platform_device *audio_pdev;

	bool ac_mode;

	int hdmi_connected;

	mipi_port_counts mipi_port_counts;
	mipi_lane_counts mipi_lane_counts;
	video_format_id video_format_id;
	audio_intf audio_out_intf;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *ocb_gpio;

	bool power_on;
	bool sleep;

	u8 pcr_m;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	enum drm_connector_status status;

	u8 edid_buf[EDID_SEG_SIZE];
};

static struct lt9611_video_cfg video_tab[] = {
	{ 8,   96,  40,	 640,  33, 2,  10, 480,	 0, 0, 0,  25000 },//video_640x480_60Hz
	{ 16,  62,  60,	 720,  9,  6,  30, 480,	 0, 0, 0,  27000 },//video_720x480_60Hz
	{ 60,  60,  100, 1024, 2,  5,  10, 600,	 0, 0, 0,  34000 },//video_1026x600_60Hz
	{ 110, 40, 220,  1280, 5,  5,  20, 720,	 1, 1, 0,  74250 },//video_1280x720_60Hz
	{ 88,  44, 148,  1920, 4,  5,  36, 1080, 1, 1, 0,  74250 },//video_1920x1080_30Hz
	{ 88,  44, 148,  1920, 4,  5,  36, 1080, 1, 1, 16, 148500 },//video_1920x1080_60Hz
	{ 48,  32,  80,  2560, 3,  5,  33, 1440, 1, 1, 0,  241500 },//video_2560x1440_60Hz
	{ 176, 88, 296,  3840, 4,  5,  36, 1080, 1, 1, 0,  297000 },//video_3840x1080_60Hz
	{ 176, 88, 296,  3840, 8,  10, 72, 2160, 1, 1, 95, 297000 },//video_3840x2160_30Hz
};

struct lt9611 *this_lt9611;
#define LT9611_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0x85ff,
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

static struct lt9611 *bridge_to_lt9611(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611, bridge);
}

static int l9611_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	int ret;

	ret = regmap_write(map, reg, val);
	if (ret) {
		pr_err("%s: %d: regmap_write failed, ret = %x\n", __func__, __LINE__, ret);
	}

	return ret;
}

static int lt9611_multi_reg_write(struct regmap *map, const struct reg_sequence *regs, int num_regs)
{
	int ret;

	ret = regmap_multi_reg_write(map, regs, num_regs);
	if (ret) {
		pr_err("%s: %d: regmap_multi_reg_write failed, ret = %d\n", __func__, __LINE__, ret);
	}

	return ret;
}

static int lt9611_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	int ret;

	ret = regmap_read(map, reg, val);

	if (ret) {
		pr_err("%s: %d: lt9611_read failed, ret = %d\n", __func__, __LINE__, ret);
	}

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

	return lt9611_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
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

	ret = lt9611_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	return ret;
}

static void lt9611_mipi_video_setup(struct lt9611 *lt9611,
				    const struct drm_display_mode *mode)
{
	u32 h_total, h_act, hpw, hfp, hss;
	u32 v_total, v_act, vpw, vfp, vss;
	video_format_id video_id;
	struct lt9611_video_cfg *cfg;

	video_id = lt9611->video_format_id;
	cfg = &video_tab[video_id];

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

	l9611_write(lt9611->regmap, 0x830d, (u8)(v_total / 256));
	l9611_write(lt9611->regmap, 0x830e, (u8)(v_total % 256));

	l9611_write(lt9611->regmap, 0x830f, (u8)(v_act / 256));
	l9611_write(lt9611->regmap, 0x8310, (u8)(v_act % 256));

	l9611_write(lt9611->regmap, 0x8311, (u8)(h_total / 256));
	l9611_write(lt9611->regmap, 0x8312, (u8)(h_total % 256));

	l9611_write(lt9611->regmap, 0x8313, (u8)(h_act / 256));
	l9611_write(lt9611->regmap, 0x8314, (u8)(h_act % 256));

	l9611_write(lt9611->regmap, 0x8315, (u8)(vpw % 256));
	l9611_write(lt9611->regmap, 0x8316, (u8)(hpw % 256));

	l9611_write(lt9611->regmap, 0x8317, (u8)(vfp % 256));
	l9611_write(lt9611->regmap, 0x8318, (u8)(vss % 256));
	l9611_write(lt9611->regmap, 0x8319, (u8)(hfp % 256));

	l9611_write(lt9611->regmap, 0x831a, (u8)(((hfp/256)<<4)+(hss/256)));
	l9611_write(lt9611->regmap, 0x831b, (u8)(hss % 256));
}

static void lt9611_pcr_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int postdiv)
{
	video_format_id video_format_id = lt9611->video_format_id;
	struct lt9611_video_cfg *cfg;
	u8 POL;
	u16 hact;

	cfg = &video_tab[video_format_id];
	hact = cfg->h_active;
	POL = (cfg-> h_polarity)*0x02 + (cfg-> v_polarity);
	POL = ~POL;
	POL &= 0x03;

	hact = (hact>>2);
	hact += 0x50;
	hact = (0x3e0>hact ? hact:0x3e0);

	// single port
	l9611_write(lt9611->regmap, 0x830b, 0x01); //vsync mode
	l9611_write(lt9611->regmap, 0x830c, 0x10); //=1/4 hact
	l9611_write(lt9611->regmap, 0x8348, 0x00); //de mode delay
	l9611_write(lt9611->regmap, 0x8349, 0x81); //=1/4 hact

	l9611_write(lt9611->regmap, 0x8321, 0x4a); //bit[3:0] step[11:8]
	l9611_write(lt9611->regmap, 0x8324, 0x71); //bit[7:4]v/h/de mode; line for clk
	l9611_write(lt9611->regmap, 0x8325, 0x30); //line for clk stb[7:0]
	l9611_write(lt9611->regmap, 0x832a, 0x01); //clk stable in

	l9611_write(lt9611->regmap, 0x834a, 0x40); //offset //0x10
	l9611_write(lt9611->regmap, 0x831d, 0x10|POL); //PCR de mode step setting.

	if (video_format_id == VIDEO_1024x600_60HZ) {
		pr_err("%s: %d : video_format_id is VIDEO_1024x600_60HZ! \n", __func__, __LINE__);
		l9611_write(lt9611->regmap, 0x8324, 0x70); //bit[7:4]v/h/de mode; line for clk stb[11:8]
		l9611_write(lt9611->regmap, 0x8325, 0x80); //line for clk stb[7:0]
		l9611_write(lt9611->regmap, 0x832a, 0x10); //clk stable in

		l9611_write(lt9611->regmap, 0x8323, 0x04); //pcr h mode step
		l9611_write(lt9611->regmap, 0x834a, 0x10); //offset //0x10
		l9611_write(lt9611->regmap, 0x831d, 0xf0); //PCR de mode step setting.
	}
}

static int lt9611_pcr_start(struct lt9611 *lt9611)
{
	l9611_write(lt9611->regmap, 0x8011, 0x5a);
	msleep(10);
	l9611_write(lt9611->regmap, 0x8011, 0xfa); //Pcr reset

	return 0;
}

u8 pcr_m_ex = 0x00;
static int lt9611_pll_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode, unsigned int *postdiv)
{
	u32 pclk;
	u8 pcr_m;
	u8 hdmi_post_div;
	unsigned int pll_lock_flag ,cal_done_flag ,band_out;
	u8 i;
	struct lt9611_video_cfg *cfg;
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

	cfg = &video_tab[lt9611->video_format_id];
	pclk = cfg->pclk_khz;

	lt9611_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (pclk > 150000) {
		l9611_write(lt9611->regmap, 0x812d, 0x88);
		*postdiv = 1;
	} else if (pclk > 70000) {
		l9611_write(lt9611->regmap, 0x812d, 0x99);
		*postdiv = 2;
	} else {
		l9611_write(lt9611->regmap, 0x812d, 0xaa);
		*postdiv = 4;
	}

	hdmi_post_div = *postdiv;

	pcr_m = (u8)((pclk * 5 * hdmi_post_div) / 27000);
	pcr_m --;
	pcr_m_ex = pcr_m;
	pr_err("pcr_m = 0x%x, hdmi_post_div = %ld \n", pcr_m, hdmi_post_div); //Hex

	l9611_write(lt9611->regmap, 0x832d, 0x40); //M up limit
	l9611_write(lt9611->regmap, 0x8331, 0x08); //M down limit
	l9611_write(lt9611->regmap, 0x8326, 0x80 | pcr_m);
	lt9611->pcr_m = pcr_m;

	pclk = pclk / 2;
	l9611_write(lt9611->regmap, 0x82e3, pclk / 65536); //13.5M

	pclk = pclk % 65536;
	l9611_write(lt9611->regmap, 0x82e4, pclk / 256);
	l9611_write(lt9611->regmap, 0x82e5, pclk % 256);
	l9611_write(lt9611->regmap, 0x82de, 0x20);
	l9611_write(lt9611->regmap, 0x82de, 0xe0);

	l9611_write(lt9611->regmap, 0x8011, 0x5a);
	l9611_write(lt9611->regmap, 0x8011, 0xfa); /* Pcr clk reset */
	l9611_write(lt9611->regmap, 0x8016, 0xf2);	
	l9611_write(lt9611->regmap, 0x8018, 0xdc);
	l9611_write(lt9611->regmap, 0x8018, 0xfc); /* pll analog reset */
	l9611_write(lt9611->regmap, 0x8016, 0xf3);

	/* pll lock status */
	for (i = 0; i < 5; i++) {
		l9611_write(lt9611->regmap, 0x8016, 0xe3); /* pll lock logic reset */
		l9611_write(lt9611->regmap, 0x8016, 0xf3);
		lt9611_read(lt9611->regmap, 0x8215, &pll_lock_flag);
		lt9611_read(lt9611->regmap, 0x82e6, &band_out);
		lt9611_read(lt9611->regmap, 0x82e7, &cal_done_flag);

		if((pll_lock_flag & 0x80) && (cal_done_flag & 0x80) && (band_out != 0xff)) {
			pr_err("%s: %d : HDMI pll locked\n", __func__, __LINE__);
			break;
		} else {
			l9611_write(lt9611->regmap, 0x8011, 0x5a);
			msleep(10);
			l9611_write(lt9611->regmap, 0x8011, 0xfa); /* Pcr clk reset */
			l9611_write(lt9611->regmap, 0x8016, 0xf2); /* pll cal reset*/	
			l9611_write(lt9611->regmap, 0x8018, 0xdc);
			msleep(10);
			l9611_write(lt9611->regmap, 0x8018, 0xfc); /* pll analog reset */
			l9611_write(lt9611->regmap, 0x8016, 0xf3);
			msleep(10);
			pr_err("%s: %d : HDMI pll unlocked, reset pll\n", __func__, __LINE__);
		}
	}

	return 0;
}

static int lt9611_read_video_check(struct lt9611 *lt9611, unsigned int reg)
{
	unsigned int temp, temp2;
	int ret;

	ret = lt9611_read(lt9611->regmap, reg, &temp);
	if (ret)
		return ret;
	
	temp <<= 8;
	ret = lt9611_read(lt9611->regmap, reg + 1, &temp2);
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

	lt9611_read(lt9611->regmap, 0x8388, &mipi_video_format);

	dev_err(lt9611->dev,
		 "video check: hactive_a=%d, hactive_b=%d, vactive=%d, v_total=%d, h_total_sysclk=%d, mipi_video_format=%d\n",
		 hactive_a, hactive_b, vactive, v_total, h_total_sysclk, mipi_video_format);

	// Adaptive Resolution
	for (i = 0; i < VIDEO_INDEX_MAX; i++) {
		if ((hactive_a == video_tab[i].h_active) && (vactive == video_tab[i].v_active)) {
			lt9611->video_format_id = i;
			pr_err("%s:%d:video_format_id = %d\n", __func__, __LINE__, i);
			break;
		}
	}

	return 0;

end:
	dev_err(lt9611->dev, "read video check error\n");
	return temp;
}

static void lt9611_hdmi_set_infoframes(struct lt9611 *lt9611,
				       struct drm_connector *connector,
				       struct drm_display_mode *mode)
{
	union hdmi_infoframe infoframe;
	ssize_t len;
	u8 iframes = 0x0a; /* UD1 infoframe */
	u8 buf[32];
	int ret;
	int i;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&infoframe.avi,
						       connector,
						       mode);
	if (ret < 0)
		goto out;

	len = hdmi_infoframe_pack(&infoframe, buf, sizeof(buf));
	if (len < 0)
		goto out;

	for (i = 0; i < len; i++)
		l9611_write(lt9611->regmap, 0x8440 + i, buf[i]);

	ret = drm_hdmi_vendor_infoframe_from_display_mode(&infoframe.vendor.hdmi,
							  connector,
							  mode);
	if (ret < 0)
		goto out;

	len = hdmi_infoframe_pack(&infoframe, buf, sizeof(buf));
	if (len < 0)
		goto out;

	for (i = 0; i < len; i++)
		l9611_write(lt9611->regmap, 0x8474 + i, buf[i]);

	iframes |= 0x20;

out:
	l9611_write(lt9611->regmap, 0x843d, iframes); /* UD1 infoframe */
}

static void lt9611_hdmi_tx_digital(struct lt9611 *lt9611, bool is_hdmi)
{
	struct lt9611_video_cfg *cfg;
	video_format_id video_id;
	u32 vic;
	u8 AR = 0x02;
	u8 pb0,pb2,pb4;
	u8 infoFrame_en;

	video_id = lt9611->video_format_id;

	cfg = &video_tab[video_id];
	vic = cfg->vic;

	infoFrame_en = (0x02|0x08);
	pb2 =  (AR<<4) + 0x08;
	if(vic == 95) {
		pb4 = 0x00;
	} else {
		pb4 =  vic;
	}

	pb0 = (((pb2 + pb4) <= 0x5f)?(0x5f - pb2 - pb4):(0x15f - pb2 - pb4));
	pr_err("pb0 = 0x%x, VIC = %x\n", pb0, vic);

	if (is_hdmi)
		l9611_write(lt9611->regmap, 0x82d6, 0x8e);
	else
		l9611_write(lt9611->regmap, 0x82d6, 0x0e);

	// audio_i2s
	if (lt9611->audio_out_intf == I2S) {
		l9611_write(lt9611->regmap, 0x82d7, 0x04);
	}

	// audio_spdif
	if (lt9611->audio_out_intf == SPDIF) {
		l9611_write(lt9611->regmap, 0x82d7, 0x80);
	}

	l9611_write(lt9611->regmap, 0x8443, pb0); //AVI_PB0
	l9611_write(lt9611->regmap, 0x8445, pb2); //AVI_PB2
	l9611_write(lt9611->regmap, 0x8447, pb4); //AVI_PB4

	l9611_write(lt9611->regmap, 0x8410, 0x02); //data iland
	l9611_write(lt9611->regmap, 0x8412, 0x64); //act_h_blank

	if(vic == 95) {
		l9611_write(lt9611->regmap, 0x843d, infoFrame_en|0x20);
		l9611_write(lt9611->regmap, 0x8474, 0x81); //HB0
		l9611_write(lt9611->regmap, 0x8475, 0x01); //HB1
		l9611_write(lt9611->regmap, 0x8476, 0x05); //HB2
		l9611_write(lt9611->regmap, 0x8477, 0x49); //PB0
		l9611_write(lt9611->regmap, 0x8478, 0x03); //PB1
		l9611_write(lt9611->regmap, 0x8479, 0x0c); //PB2
		l9611_write(lt9611->regmap, 0x847a, 0x00); //PB3
		l9611_write(lt9611->regmap, 0x847b, 0x20); //PB4
		l9611_write(lt9611->regmap, 0x847c, 0x01); //PB5
	} else {
		l9611_write(lt9611->regmap, 0x843d, infoFrame_en);
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

	/* HDMI AC mode */
	if (lt9611->ac_mode)
		reg_cfg[2].def = 0x73;

	lt9611_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
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

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}

static void lt9611_hpd_work(struct work_struct *work)
{
	struct lt9611 *lt9611 = container_of(work, struct lt9611, work);
	int connected;

	if (lt9611->connector.dev) {
		lt9611->connector.status = (lt9611->status) ?
			connector_status_connected: connector_status_disconnected;
		lt9611_helper_hotplug_event(lt9611);
	} else {
		mutex_lock(&lt9611->ocm_lock);
		connected = lt9611->status;
		mutex_unlock(&lt9611->ocm_lock);

		drm_bridge_hpd_notify(&lt9611->bridge,
				      connected ?
				      connector_status_connected :
				      connector_status_disconnected);
	}
}

static irqreturn_t lt9611_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611 *lt9611 = dev_id;
	unsigned int irq_flag0 = 0;
	unsigned int irq_flag3 = 0;

	lt9611_read(lt9611->regmap, 0x820f, &irq_flag3);
	lt9611_read(lt9611->regmap, 0x820c, &irq_flag0);

	/* hpd changed low */
	if (irq_flag3 & 0x80) {
		dev_err(lt9611->dev, "hdmi cable disconnected\n");

		l9611_write(lt9611->regmap, 0x8207, 0xbf);
		l9611_write(lt9611->regmap, 0x8207, 0x3f);

		lt9611->status = connector_status_disconnected;
	}

	/* hpd changed high */
	if (irq_flag3 & 0x40) {
		dev_err(lt9611->dev, "hdmi cable connected\n");

		l9611_write(lt9611->regmap, 0x8207, 0x7f);
		l9611_write(lt9611->regmap, 0x8207, 0x3f);

		lt9611->status = connector_status_connected;
	}

	//schedule_work(&lt9611->work);
	if ((irq_flag3 & 0xc0) && lt9611->bridge.dev) {
		drm_kms_helper_hotplug_event(lt9611->bridge.dev);
	}

	/* video input changed */
	if (irq_flag0 & 0x01) {
		dev_err(lt9611->dev, "video input changed\n");
		l9611_write(lt9611->regmap, 0x829e, 0xff);
		l9611_write(lt9611->regmap, 0x829e, 0xf7);
		l9611_write(lt9611->regmap, 0x8204, 0xff);
		l9611_write(lt9611->regmap, 0x8204, 0xfe);
	}

	return IRQ_HANDLED;
}

static void lt9611_enable_hpd_interrupts(struct lt9611 *lt9611)
{
	unsigned int val;

	lt9611_read(lt9611->regmap, 0x8203, &val);

	val &= ~0xc0;
	l9611_write(lt9611->regmap, 0x8203, val);
	l9611_write(lt9611->regmap, 0x8207, 0xff); /* clear */
	l9611_write(lt9611->regmap, 0x8207, 0x3f);
}

static void lt9611_sleep_setup(struct lt9611 *lt9611)
{
	const struct reg_sequence sleep_setup[] = {
		{ 0x8024, 0x76 },
		{ 0x8023, 0x01 },
		{ 0x8157, 0x03 }, /* set addr pin as output */
		{ 0x8149, 0x0b },

		{ 0x8102, 0x48 }, /* MIPI Rx power down */
		{ 0x8123, 0x80 },
		{ 0x8130, 0x00 },
		{ 0x8011, 0x0a },
	};

	lt9611_multi_reg_write(lt9611->regmap,
			       sleep_setup, ARRAY_SIZE(sleep_setup));
	lt9611->sleep = true;
}

static int lt9611_power_on(struct lt9611 *lt9611)
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

		/* irq init */
		//{ 0x8251, 0x01 },
		//{ 0x8258, 0x0a }, /* hpd irq */
		//{ 0x8259, 0x00 }, /* hpd debounce width */
		//{ 0x829e, 0xf7 }, /* video check irq */

		/* power consumption for work */
		{ 0x8004, 0xf0 },
		{ 0x8006, 0xf0 },
		{ 0x800a, 0x80 },
		{ 0x800b, 0x46 },
		{ 0x800d, 0xef },
		{ 0x8011, 0xfa },
	};

	if (lt9611->power_on)
		return 0;

	ret = lt9611_multi_reg_write(lt9611->regmap, seq, ARRAY_SIZE(seq));
	if (!ret)
		lt9611->power_on = true;

	return ret;
}

static int lt9611_power_off(struct lt9611 *lt9611)
{
	int ret;

	ret = l9611_write(lt9611->regmap, 0x8130, 0x6a);
	if (!ret)
		lt9611->power_on = false;

	return ret;
}

static void lt9611_reset(struct lt9611 *lt9611)
{
	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 0);
	msleep(100);

	gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
	msleep(100);
}

static void lt9611_assert_5v(struct lt9611 *lt9611)
{
	if (!lt9611->enable_gpio)
		return;

	if (!lt9611->ocb_gpio)
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

	return regulator_set_load(lt9611->supplies[0].consumer, 300000);
}

static int lt9611_regulator_enable(struct lt9611 *lt9611)
{
	int ret;

	ret = regulator_enable(lt9611->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(1000, 10000);

	ret = regulator_enable(lt9611->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static enum drm_connector_status lt9611_bridge_detect(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	unsigned int reg_val = 0;
	int connected = 0;

	lt9611_read(lt9611->regmap, 0x825e, &reg_val);
	connected  = (reg_val & (BIT(2) | BIT(0)));

	lt9611->status = connected ?  connector_status_connected :
				connector_status_disconnected;

	return lt9611->status;
}

static int lt9611_read_edid(struct lt9611 *lt9611)
{
	unsigned int temp;
	int ret = 0;
	int i, j;

	/* memset to clear old buffer, if any */
	memset(lt9611->edid_buf, 0, sizeof(lt9611->edid_buf));

	l9611_write(lt9611->regmap, 0x8503, 0xc9);

	/* 0xA0 is EDID device address */
	l9611_write(lt9611->regmap, 0x8504, 0xa0);
	/* 0x00 is EDID offset address */
	l9611_write(lt9611->regmap, 0x8505, 0x00);

	/* length for read */
	l9611_write(lt9611->regmap, 0x8506, EDID_LEN);
	l9611_write(lt9611->regmap, 0x8514, 0x7f);

	for (i = 0; i < EDID_LOOP; i++) {
		/* offset address */
		l9611_write(lt9611->regmap, 0x8505, i * EDID_LEN);
		l9611_write(lt9611->regmap, 0x8507, 0x36);
		l9611_write(lt9611->regmap, 0x8507, 0x31);
		l9611_write(lt9611->regmap, 0x8507, 0x37);
		usleep_range(5000, 10000);

		lt9611_read(lt9611->regmap, 0x8540, &temp);

		if (temp & KEY_DDC_ACCS_DONE) {
			for (j = 0; j < EDID_LEN; j++) {
				lt9611_read(lt9611->regmap, 0x8583, &temp);
				lt9611->edid_buf[i * EDID_LEN + j] = temp;
			}

		} else if (temp & DDC_NO_ACK) { /* DDC No Ack or Abitration lost */
			dev_err(lt9611->dev, "read edid failed: no ack\n");
			ret = -EIO;
			goto end;

		} else {
			dev_err(lt9611->dev, "read edid failed: access not done\n");
			ret = -EIO;
			goto end;
		}
	}

end:
	l9611_write(lt9611->regmap, 0x8507, 0x1f);
	return ret;
}

static int
lt9611_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct lt9611 *lt9611 = data;
	int ret;

	if (len > 128)
		return -EINVAL;

	/* supports up to 1 extension block */
	/* TODO: add support for more extension blocks */
	if (block > 1)
		return -EINVAL;

	if (block == 0) {
		ret = lt9611_read_edid(lt9611);
		if (ret) {
			dev_err(lt9611->dev, "edid read failed\n");
			return ret;
		}
	}

	block %= 2;
	memcpy(buf, lt9611->edid_buf + (block * 128), len);

	return 0;
}

/* bridge funcs */
static void
lt9611_bridge_atomic_enable(struct drm_bridge *bridge,
			    struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	struct drm_display_mode *mode;
	unsigned int postdiv;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	lt9611_mipi_input_digital(lt9611, mode);
	lt9611_pll_setup(lt9611, mode, &postdiv);
	lt9611_mipi_video_setup(lt9611, mode);
	lt9611_pcr_setup(lt9611, mode, postdiv);

	if (lt9611_power_on(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}

	lt9611_mipi_input_analog(lt9611);
	lt9611_hdmi_set_infoframes(lt9611, connector, mode);
	lt9611_hdmi_tx_digital(lt9611, connector->display_info.is_hdmi);
	lt9611_hdmi_tx_phy(lt9611);

	msleep(500);

	lt9611_video_check(lt9611);

	/* Enable HDMI output */
	l9611_write(lt9611->regmap, 0x8130, 0xea);
}

static void
lt9611_bridge_atomic_disable(struct drm_bridge *bridge,
			     struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	int ret;

	/* Disable HDMI output */
	ret = l9611_write(lt9611->regmap, 0x8130, 0x6a);
	if (ret) {
		dev_err(lt9611->dev, "video on failed\n");
		return;
	}

	if (lt9611_power_off(lt9611)) {
		dev_err(lt9611->dev, "power on failed\n");
		return;
	}
}

static struct mipi_dsi_device *lt9611_attach_dsi(struct lt9611 *lt9611,
						 struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611", 0, lt9611->dev->of_node};
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(lt9611->dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(lt9611->dev, "failed to create dsi device\n");
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

static int lt9611_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	return drm_bridge_attach(bridge->encoder, lt9611->next_bridge,
				 bridge, flags);
}

static enum drm_mode_status lt9611_bridge_mode_valid(struct drm_bridge *bridge,
						     const struct drm_display_info *info,
						     const struct drm_display_mode *mode)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	if (mode->hdisplay > 3840)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > 2160)
		return MODE_BAD_VVALUE;

	if (mode->hdisplay == 3840 &&
	    mode->vdisplay == 2160 &&
	    drm_mode_vrefresh(mode) > 30)
		return MODE_CLOCK_HIGH;

	if (mode->hdisplay > 2000 && !lt9611->dsi1_node) {
		return MODE_PANEL;
	} else {
		return MODE_OK;
	}
}

static void lt9611_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					    struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);
	static const struct reg_sequence reg_cfg[] = {
		{ 0x8102, 0x12 },
		{ 0x8123, 0x40 },
		{ 0x8130, 0xea },
		{ 0x8011, 0xfa },
	};

	if (!lt9611->sleep)
		return;

	lt9611_multi_reg_write(lt9611->regmap,
			       reg_cfg, ARRAY_SIZE(reg_cfg));

	lt9611->sleep = false;
}

static void
lt9611_bridge_atomic_post_disable(struct drm_bridge *bridge,
				  struct drm_bridge_state *old_bridge_state)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_sleep_setup(lt9611);
}

static struct edid *lt9611_bridge_get_edid(struct drm_bridge *bridge,
					   struct drm_connector *connector)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_power_on(lt9611);
	return drm_do_get_edid(connector, lt9611_get_edid_block, lt9611);
}

static void lt9611_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct lt9611 *lt9611 = bridge_to_lt9611(bridge);

	lt9611_enable_hpd_interrupts(lt9611);
}

#define MAX_INPUT_SEL_FORMATS	1

static u32 *
lt9611_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				 struct drm_bridge_state *bridge_state,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state,
				 u32 output_fmt,
				 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	/* This is the DSI-end bus format */
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	*num_input_fmts = 1;

	return input_fmts;
}

static const struct drm_bridge_funcs lt9611_bridge_funcs = {
	.attach = lt9611_bridge_attach,
	.mode_valid = lt9611_bridge_mode_valid,
	.detect = lt9611_bridge_detect,
	.get_edid = lt9611_bridge_get_edid,
	.hpd_enable = lt9611_bridge_hpd_enable,

	.atomic_pre_enable = lt9611_bridge_atomic_pre_enable,
	.atomic_enable = lt9611_bridge_atomic_enable,
	.atomic_disable = lt9611_bridge_atomic_disable,
	.atomic_post_disable = lt9611_bridge_atomic_post_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = lt9611_atomic_get_input_bus_fmts,
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

	lt9611->ac_mode = of_property_read_bool(dev->of_node, "lt,ac-mode");

	// return drm_of_find_panel_or_bridge(dev->of_node, 2, -1, NULL, &lt9611->next_bridge);
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

	lt9611->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(lt9611->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(lt9611->enable_gpio);
	}

	lt9611->ocb_gpio = devm_gpiod_get_optional(dev, "ocb",
						      GPIOD_OUT_LOW);
	if (IS_ERR(lt9611->ocb_gpio)) {
		dev_err(dev, "failed to acquire ocb gpio\n");
		return PTR_ERR(lt9611->ocb_gpio);
	}

	return 0;
}

static int lt9611_read_device_rev(struct lt9611 *lt9611)
{
	unsigned int rev, chip_id0, chip_id1;
	int ret;

	l9611_write(lt9611->regmap, 0x80ee, 0x01);
	ret = lt9611_read(lt9611->regmap, 0x8002, &rev);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);
	else
		dev_err(lt9611->dev, "LT9611 revision: 0x%x\n", rev);
	
	ret = lt9611_read(lt9611->regmap, 0x8000, &chip_id0);
	ret = lt9611_read(lt9611->regmap, 0x8001, &chip_id1);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);
	else
		dev_err(lt9611->dev, "LT9611 chip: 0x%x, 0x%x\n", chip_id0, chip_id1);

	return ret;
}

static int lt9611_hdmi_hw_params(struct device *dev, void *data,
				 struct hdmi_codec_daifmt *fmt,
				 struct hdmi_codec_params *hparms)
{
	struct lt9611 *lt9611 = data;

	if (hparms->sample_rate == 48000)
		l9611_write(lt9611->regmap, 0x840f, 0x2b);
	else if (hparms->sample_rate == 96000)
		l9611_write(lt9611->regmap, 0x840f, 0xab);
	else
		return -EINVAL;

	l9611_write(lt9611->regmap, 0x8435, 0x00);
	l9611_write(lt9611->regmap, 0x8436, 0x18);
	l9611_write(lt9611->regmap, 0x8437, 0x00);

	return 0;
}

static int lt9611_audio_startup(struct device *dev, void *data)
{
	struct lt9611 *lt9611 = data;

	l9611_write(lt9611->regmap, 0x82d6, 0x8c);
	l9611_write(lt9611->regmap, 0x82d7, 0x04);

	l9611_write(lt9611->regmap, 0x8406, 0x08);
	l9611_write(lt9611->regmap, 0x8407, 0x10);

	l9611_write(lt9611->regmap, 0x8434, 0xd5);

	return 0;
}

static void lt9611_audio_shutdown(struct device *dev, void *data)
{
	struct lt9611 *lt9611 = data;

	l9611_write(lt9611->regmap, 0x8406, 0x00);
	l9611_write(lt9611->regmap, 0x8407, 0x00);
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

static void lt9611_Audio_Init(struct lt9611 *lt9611)
{
	if (lt9611->audio_out_intf == I2S) {
		pr_err("%s: %d: Audio inut = I2S 2ch\n", __func__, __LINE__);
		l9611_write(lt9611->regmap, 0x82d7, 0x04);
		l9611_write(lt9611->regmap, 0x8406, 0x08);
		l9611_write(lt9611->regmap, 0x8407, 0x10);

		// 48K sampling frequency
		l9611_write(lt9611->regmap, 0x840f, 0x2b);
		l9611_write(lt9611->regmap, 0x8434, 0xd4);//CTS_N 0xd5: sclk = 32fs, 0xd4: sclk = 64fs

		l9611_write(lt9611->regmap, 0x8435, 0x00); // N value = 6144
		l9611_write(lt9611->regmap, 0x8436, 0x18);
		l9611_write(lt9611->regmap, 0x8437, 0x00);

		// 96K sampling frequency
		//l9611_write(lt9611->regmap, 0x840f, 0xab);
		//l9611_write(lt9611->regmap, 0x8434, 0xd4);

		//l9611_write(lt9611->regmap, 0x8435, 0x00); // N value = 12288
		//l9611_write(lt9611->regmap, 0x8436, 0x30);
		//l9611_write(lt9611->regmap, 0x8437, 0x00);

		// 44.1K sampling frequency
		//l9611_write(lt9611->regmap, 0x840f, 0x0b);
		//l9611_write(lt9611->regmap, 0x8434, 0xd4);

		//l9611_write(lt9611->regmap, 0x8435, 0x00); // N value = 6272
		//l9611_write(lt9611->regmap, 0x8436, 0x18);
		//l9611_write(lt9611->regmap, 0x8437, 0x80);

	}

	if (lt9611->audio_out_intf == SPDIF) {
		pr_err("%s: %d: Audio inut = SPDIF\n", __func__, __LINE__);
		l9611_write(lt9611->regmap, 0x8406, 0x0c);
		l9611_write(lt9611->regmap, 0x8407, 0x10);

		l9611_write(lt9611->regmap, 0x8434, 0xd4); //CTS_N
		l9611_write(lt9611->regmap, 0x8436, 0x20);
	}
}

static const struct hdmi_codec_ops lt9611_codec_ops = {
	.hw_params	= lt9611_hdmi_hw_params,
	.audio_shutdown = lt9611_audio_shutdown,
	.audio_startup	= lt9611_audio_startup,
	.get_dai_id	= lt9611_hdmi_i2s_get_dai_id,
};

static struct hdmi_codec_pdata codec_data = {
	.ops = &lt9611_codec_ops,
	.max_i2s_channels = 8,
	.i2s = 1,
};

static void lt9611_pcr_mk_debug(struct lt9611 *lt9611)
{
	unsigned int m = 0, k1 = 0, k2 = 0, k3 = 0;

	lt9611_read(lt9611->regmap, 0x83b4, &m);
	lt9611_read(lt9611->regmap, 0x83b5, &k1);
	lt9611_read(lt9611->regmap, 0x83b6, &k2);
	lt9611_read(lt9611->regmap, 0x83b7, &k3);

	pr_err("pcr mk:0x%x 0x%x 0x%x 0x%x\n",
			m, k1, k2, k3);
}

static void lt9611_mipi_byte_clk_debug(struct lt9611 *lt9611)
{
	unsigned int reg_val = 0;
	u32 byte_clk;

	/* port A byte clk meter */
	l9611_write(lt9611->regmap, 0x82c7, 0x03); /* port A */
	msleep(50);
	lt9611_read(lt9611->regmap, 0x82cd, &reg_val);

	if ((reg_val & 0x60) == 0x60) {
		byte_clk =  (reg_val & 0x0f) * 65536;
		lt9611_read(lt9611->regmap, 0x82ce, &reg_val);

		byte_clk = byte_clk + reg_val * 256;
		lt9611_read(lt9611->regmap, 0x82cf, &reg_val);
		byte_clk = byte_clk + reg_val;

		pr_err("port A byte clk = %d khz,\n", byte_clk);
	} else
		pr_err("port A byte clk unstable\n");

	/* port B byte clk meter */
	l9611_write(lt9611->regmap, 0x82c7, 0x04); /* port B */
	msleep(50);
	lt9611_read(lt9611->regmap, 0x82cd, &reg_val);

	if ((reg_val & 0x60) == 0x60) {
		byte_clk =  (reg_val & 0x0f) * 65536;
		lt9611_read(lt9611->regmap, 0x82ce, &reg_val);

		byte_clk = byte_clk + reg_val * 256;
		lt9611_read(lt9611->regmap, 0x82cf, &reg_val);
		byte_clk = byte_clk + reg_val;

		pr_err("port B byte clk = %d khz,\n", byte_clk);
	} else
		pr_err("port B byte clk unstable\n");
}

static void lt9611_Dphy_debug(struct lt9611 *lt9611)
{
	unsigned int temp;

	lt9611_read(lt9611->regmap, 0x83bc, &temp);
	if(temp == 0x55)
		pr_err("port A lane PN is right\n");
	else
		pr_err("port A lane PN error 0x83bc = 0x%x\n", temp);
	
	lt9611_read(lt9611->regmap, 0x8399, &temp);
	if(temp == 0xb8)
		pr_err("port A lane 0 sot right \n");
	else
		pr_err("port A lane 0 sot error = 0x%x \n",temp);

	lt9611_read(lt9611->regmap, 0x839b, &temp);
	if(temp == 0xb8)
		pr_err("port A lane 1 sot right \n");
	else
		pr_err("port A lane 1 sot error = 0x%x \n",temp);
	
	lt9611_read(lt9611->regmap, 0x839d, &temp);
	if(temp == 0xb8)
		pr_err("port A lane 2 sot right\n");
	else
		pr_err("port A lane 2 sot error = 0x%x\n",temp);
	
	lt9611_read(lt9611->regmap, 0x839f, &temp);
	if(temp == 0xb8)
		pr_err("port A lane 3 sot right \n");
	else
		pr_err("port A lane 3 sot error = 0x%x \n",temp);

	lt9611_read(lt9611->regmap, 0x8398, &temp);
	pr_err("port A lane 0 settle = 0x%x \n",temp);

	lt9611_read(lt9611->regmap, 0x839a, &temp);
	pr_err("port A lane 1 settle = 0x%x \n",temp);

	lt9611_read(lt9611->regmap, 0x839c, &temp);
	pr_err("port A lane 2 settle = 0x%x \n",temp);

	lt9611_read(lt9611->regmap, 0x839e, &temp);
	pr_err("port A lane 3 settle = 0x%x \n",temp);
}

static void lt9611_Htotal_Sysclk(struct lt9611 *lt9611)
{
	unsigned int reg, reg1;
	u8 loopx;

	for(loopx = 0; loopx < 10; loopx++)
	{
		lt9611_read(lt9611->regmap, 0x8286, &reg);
		lt9611_read(lt9611->regmap, 0x8287, &reg1);

		reg = reg*256+reg1;
		pr_err("Htotal_Sysclk = %d \n",reg);
	}
}

static void lt9611_Pcr_MK_Print(struct lt9611 *lt9611)
{
	u8 loopx;
	unsigned int reg;

	for(loopx = 0; loopx < 8; loopx++)
	{
		lt9611_read(lt9611->regmap, 0x8397, &reg);
		pr_err("%s: %d :0x8397=%d\n", __func__, __LINE__, reg);

		lt9611_read(lt9611->regmap, 0x83b4, &reg);
		pr_err("%s: %d :0x83b4=%d\n", __func__, __LINE__, reg);

		lt9611_read(lt9611->regmap, 0x83b5, &reg);
		pr_err("%s: %d :0x83b5=%d\n", __func__, __LINE__, reg);

		lt9611_read(lt9611->regmap, 0x83b6, &reg);
		pr_err("%s: %d :0x83b6=%d\n", __func__, __LINE__, reg);

		lt9611_read(lt9611->regmap, 0x83b7, &reg);
		pr_err("%s: %d :0x83b7=%d\n", __func__, __LINE__, reg);
	}
}

static void lt9611_LowPower_mode(struct lt9611 *lt9611, bool on)
{
	/* only hpd irq is working for low power consumption */
	/* 1.8V: 15 mA */
	if (on) {
		pr_err("%s: %d : Enter low power mode", __func__, __LINE__);
		l9611_write(lt9611->regmap, 0x8102, 0x49);
		l9611_write(lt9611->regmap, 0x8123, 0x80);
		//0x00 --> 0xc0, tx phy and clk can not power down, otherwise dc det don't work
		l9611_write(lt9611->regmap, 0x8130, 0x00);
		l9611_write(lt9611->regmap, 0x8011, 0x0a);
	} else {
		pr_err("%s: %d : Exit low power mode", __func__, __LINE__);
		l9611_write(lt9611->regmap, 0x8102, 0x12);
		l9611_write(lt9611->regmap, 0x8123, 0x40);
		l9611_write(lt9611->regmap, 0x8130, 0xea);
		l9611_write(lt9611->regmap, 0x8011, 0xfa);
	}
}

static void lt9611_pattern_gcm(struct lt9611 *lt9611)
{
	video_format_id video_format_id = lt9611->video_format_id;
	struct lt9611_video_cfg *cfg;
	u32 v_total, h_total;
	u8 POL;

	cfg = &video_tab[video_format_id];

	POL = (cfg-> h_polarity)*0x10 + (cfg-> v_polarity)*0x20;
	POL = ~POL;
	POL &= 0x30;

	pr_err("%s: %d :POL=%d: video_format_id=%d\n", __func__, __LINE__, POL, video_format_id);

	h_total = cfg->h_active + cfg->h_front_porch + cfg->h_pulse_width + cfg->h_back_porch;
	v_total = cfg->v_active + cfg->v_front_porch + cfg->v_pulse_width + cfg->v_back_porch;

	l9611_write(lt9611->regmap, 0x82a3,(u8)((cfg->h_pulse_width+cfg->h_back_porch)/256)); //de_delay
	l9611_write(lt9611->regmap, 0x82a4,(u8)((cfg->h_pulse_width+cfg->h_back_porch)%256));
	l9611_write(lt9611->regmap, 0x82a5,(u8)((cfg->v_pulse_width+cfg->v_back_porch)%256)); //de_top
	l9611_write(lt9611->regmap, 0x82a6,(u8)(cfg->h_active/256));
	l9611_write(lt9611->regmap, 0x82a7,(u8)(cfg->h_active%256)); //de_cnt
	l9611_write(lt9611->regmap, 0x82a8,(u8)(cfg->v_active/256));
	l9611_write(lt9611->regmap, 0x82a9,(u8)(cfg->v_active%256)); //de_line
	l9611_write(lt9611->regmap, 0x82aa,(u8)(h_total/256));
	l9611_write(lt9611->regmap, 0x82ab,(u8)(h_total%256)); //htotal
	l9611_write(lt9611->regmap, 0x82ac,(u8)(v_total/256));
	l9611_write(lt9611->regmap, 0x82ad,(u8)(v_total%256)); //vtotal
	l9611_write(lt9611->regmap, 0x82ae,(u8)(cfg->h_pulse_width/256));
	l9611_write(lt9611->regmap, 0x82af,(u8)(cfg->h_pulse_width%256)); //hvsa
	l9611_write(lt9611->regmap, 0x82b0,(u8)(cfg->v_pulse_width%256)); //vsa
	l9611_write(lt9611->regmap, 0x8247,(u8)(POL|0x07)); //sync polarity

	pr_err("%s: %d: h_total=%d: h_act= %d hpw = %d: hfp = %d : hbp= %d\n",
		__func__, __LINE__, h_total, cfg->h_active, cfg->h_pulse_width, cfg->h_front_porch, cfg->h_back_porch);
	pr_err("%s: %d: v_total= %d: v_act=%d: vpw=%d: vfp=%d: vbp=%d\n",
		__func__, __LINE__, v_total, cfg->v_active, cfg->v_pulse_width, cfg->v_front_porch, cfg->v_back_porch);
}

static void lt9611_pattern_en(struct lt9611 *lt9611)
{
	l9611_write(lt9611->regmap, 0x824f, 0x80); //[7] = Select ad_txpll_d_clk.
	l9611_write(lt9611->regmap, 0x8250, 0x20);
}

static void lt9611_pattern(struct lt9611 *lt9611)
{
	unsigned int postdiv;

	lt9611_reset(lt9611);

	msleep(100);

	lt9611_read_device_rev(lt9611);
	lt9611_power_on(lt9611);
	lt9611_pattern_en(lt9611);

	lt9611_pll_setup(lt9611, NULL, &postdiv);
	lt9611_pattern_gcm(lt9611);
	lt9611_hdmi_tx_digital(lt9611, false);

	lt9611_hdmi_tx_phy(lt9611);

	lt9611_Audio_Init(lt9611);

	/* Enable HDMI output */
	l9611_write(lt9611->regmap, 0x8123, 0x40);
	l9611_write(lt9611->regmap, 0x82de, 0x20);
	l9611_write(lt9611->regmap, 0x82de, 0xe0);
	l9611_write(lt9611->regmap, 0x8018, 0xdc); /* txpll sw rst */
	l9611_write(lt9611->regmap, 0x8018, 0xfc);
	l9611_write(lt9611->regmap, 0x8016, 0xf1); /* txpll calibration rest */
	l9611_write(lt9611->regmap, 0x8016, 0xf3);
	l9611_write(lt9611->regmap, 0x8011, 0x5a); //Pcr reset
	l9611_write(lt9611->regmap, 0x8011, 0xfa);
	l9611_write(lt9611->regmap, 0x8130, 0xea);
}

void lt9611_on(bool on)
{
	unsigned int postdiv, reg;
	struct lt9611 *lt9611 = this_lt9611;

	if (on) {
		lt9611_power_on(lt9611);

		lt9611_mipi_input_analog(lt9611);
		lt9611_mipi_input_digital(lt9611, NULL);
		lt9611_Audio_Init(lt9611);
		lt9611_hdmi_tx_phy(lt9611);
		msleep(200);

		/* Disable HDMI output */
		l9611_write(lt9611->regmap, 0x8130, 0x00);
		l9611_write(lt9611->regmap, 0x8123, 0x80);

		lt9611_read(lt9611->regmap, 0x825e, &reg);
		if((reg & 0x04) == 0x04) {
			msleep(10);
			lt9611_read(lt9611->regmap, 0x825e, &reg);

			if((reg & 0x04) == 0x04) {
				lt9611_LowPower_mode(lt9611, 0);
				msleep(500);
				lt9611_video_check(lt9611);
				lt9611_pll_setup(lt9611, NULL, &postdiv);
				lt9611_pcr_setup(lt9611, NULL, 0);
				lt9611_mipi_video_setup(lt9611, NULL);
				l9611_write(lt9611->regmap, 0x8326, pcr_m_ex);
				lt9611_pcr_start(lt9611);
				lt9611_hdmi_tx_digital(lt9611, true);

				/* Enable HDMI output */
				l9611_write(lt9611->regmap, 0x8123, 0x40);
				l9611_write(lt9611->regmap, 0x82de, 0x20);
				l9611_write(lt9611->regmap, 0x82de, 0xe0);
				l9611_write(lt9611->regmap, 0x8018, 0xdc);
				l9611_write(lt9611->regmap, 0x8018, 0xfc);
				l9611_write(lt9611->regmap, 0x8016, 0xf1);
				l9611_write(lt9611->regmap, 0x8016, 0xf3);
				l9611_write(lt9611->regmap, 0x8011, 0x5a); //Pcr reset
				l9611_write(lt9611->regmap, 0x8011, 0xfa);
				l9611_write(lt9611->regmap, 0x8130, 0xea);
			}
		}
	} else {
		/* Disable HDMI output */
		l9611_write(lt9611->regmap, 0x8130, 0x00);
		l9611_write(lt9611->regmap, 0x8123, 0x80);
	}
}
EXPORT_SYMBOL(lt9611_on);

static void lt9611_video_on(struct lt9611 *lt9611, bool on)
{
	unsigned int postdiv;

	if (on) {
		lt9611_reset(lt9611);
		lt9611_read_device_rev(lt9611);
		lt9611_on(true);
	} else {
		/* Disable HDMI output */
		l9611_write(lt9611->regmap, 0x8130, 0x6a);
		l9611_write(lt9611->regmap, 0x8130, 0x00);
		l9611_write(lt9611->regmap, 0x8123, 0x80);
	}
}

static void lt9611_enable(struct lt9611 *pdata)
{
	// struct lt9611 *pdata = bridge_to_lt9611(bridge);

	lt9611_video_on(pdata, true);
}

static void lt9611_disable(struct lt9611 *pdata)
{
	// struct lt9611 *pdata = bridge_to_lt9611(bridge);

	lt9611_video_on(pdata, false);
}

static int lt9611_audio_init(struct device *dev, struct lt9611 *lt9611)
{
	codec_data.data = lt9611;
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

/* sysfs */
static int lt9611_dump_debug_info(struct lt9611 *pdata)
{
	if (!pdata->power_on) {
		pr_err("device is not power on\n");
		return -EINVAL;
	}

	lt9611_video_check(pdata);
	lt9611_pcr_mk_debug(pdata);
	lt9611_mipi_byte_clk_debug(pdata);
	lt9611_Dphy_debug(pdata);
	lt9611_Htotal_Sysclk(pdata);
	lt9611_Pcr_MK_Print(pdata);
	lt9611_read_edid(pdata);

	return 0;
}

static ssize_t lt9611_dump_info_wta_attr(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int state;
	struct lt9611 *pdata = dev_get_drvdata(dev);
	state = (int)((unsigned int *)buf)[0];

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	if (buf[0] == '1') {
		lt9611_pattern(pdata);
	} else if (buf[0] == '2') {
		lt9611_dump_debug_info(pdata);
	} else if (buf[0] == '3') {
		lt9611_enable(pdata);
	} else if (buf[0] == '0') {
		lt9611_disable(pdata);
	} else {
		pr_err("pdata is NULL\n");
	}

	return count;
}

static DEVICE_ATTR(dump_info, 0200, NULL, lt9611_dump_info_wta_attr);

static struct attribute *lt9611_sysfs_attrs[] = {
	&dev_attr_dump_info.attr,
	NULL,
};

static struct attribute_group lt9611_sysfs_attr_grp = {
	.attrs = lt9611_sysfs_attrs,
};

static int lt9611_sysfs_init(struct device *dev)
{
	int rc = 0;

	if (!dev) {
		pr_err("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	rc = sysfs_create_group(&dev->kobj, &lt9611_sysfs_attr_grp);
	if (rc)
		pr_err("%s: sysfs group creation failed %d\n", __func__, rc);

	return rc;
}

static void lt9611_sysfs_remove(struct device *dev)
{
	if (!dev) {
		pr_err("%s: Invalid params\n", __func__);
		return;
	}

	sysfs_remove_group(&dev->kobj, &lt9611_sysfs_attr_grp);
}

static int lt9611_probe(struct i2c_client *client)
{
	struct lt9611 *lt9611;
	struct device *dev = &client->dev;
	int ret;
	unsigned int postdiv, reg;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611 = devm_kzalloc(dev, sizeof(*lt9611), GFP_KERNEL);
	if (!lt9611)
		return -ENOMEM;

	lt9611->dev = dev;
	lt9611->client = client;
	lt9611->sleep = false;
	this_lt9611 = lt9611;

	lt9611->regmap = devm_regmap_init_i2c(client, &lt9611_regmap_config);
	if (IS_ERR(lt9611->regmap)) {
		dev_err(lt9611->dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611->regmap);
	}

	ret = lt9611_parse_dt(dev, lt9611);
	if (ret) {
		dev_err(dev, "failed to parse device tree,ret= %d \n", ret);
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

	lt9611_reset(lt9611);

	ret = lt9611_read_device_rev(lt9611);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
		goto err_disable_regulators;
	}

	INIT_WORK(&lt9611->work, lt9611_hpd_work);

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					lt9611_irq_thread_handler,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "lt9611", lt9611);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_disable_regulators;
	}

	i2c_set_clientdata(client, lt9611);

	ret = lt9611_sysfs_init(&client->dev);
	if (ret) {
		pr_err("sysfs init failed\n");
	}

	lt9611->bridge.funcs = &lt9611_bridge_funcs;
	lt9611->bridge.of_node = client->dev.of_node;
	lt9611->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID |
			     DRM_BRIDGE_OP_HPD | DRM_BRIDGE_OP_MODES;
	lt9611->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&lt9611->bridge);

	/* Attach primary DSI */
	lt9611->dsi0 = lt9611_attach_dsi(lt9611, lt9611->dsi0_node);
	if (IS_ERR(lt9611->dsi0)) {
		ret = PTR_ERR(lt9611->dsi0);
		goto err_remove_bridge;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611->dsi1_node) {
		lt9611->dsi1 = lt9611_attach_dsi(lt9611, lt9611->dsi1_node);
		if (IS_ERR(lt9611->dsi1)) {
			ret = PTR_ERR(lt9611->dsi1);
			goto err_remove_bridge;
		}
	}

	//lt9611_enable_hpd_interrupts(lt9611);

	ret = lt9611_audio_init(dev, lt9611);
	if (ret)
		goto err_remove_bridge;

	return 0;

err_remove_bridge:
	drm_bridge_remove(&lt9611->bridge);

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

err_of_put:
	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);

	return ret;
}

static void lt9611_remove(struct i2c_client *client)
{
	struct lt9611 *lt9611 = i2c_get_clientdata(client);

	lt9611_sysfs_remove(&client->dev);

	disable_irq(client->irq);
	cancel_work_sync(&lt9611->work);
	lt9611_audio_exit(lt9611);
	drm_bridge_remove(&lt9611->bridge);

	regulator_bulk_disable(ARRAY_SIZE(lt9611->supplies), lt9611->supplies);

	of_node_put(lt9611->dsi1_node);
	of_node_put(lt9611->dsi0_node);
}

static struct i2c_device_id lt9611_id[] = {
	{ "lontium,lt9611", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, lt9611_id);

static const struct of_device_id lt9611_match_table[] = {
	{ .compatible = "lontium,lt9611" },
	{ }
};
MODULE_DEVICE_TABLE(of, lt9611_match_table);

static struct i2c_driver lt9611_driver = {
	.driver = {
		.name = "lt9611",
		.of_match_table = lt9611_match_table,
	},
	.probe = lt9611_probe,
	.remove = lt9611_remove,
	.id_table = lt9611_id,
};
module_i2c_driver(lt9611_driver);

MODULE_LICENSE("GPL v2");
