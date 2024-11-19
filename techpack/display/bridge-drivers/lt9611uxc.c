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

struct lt9611uxc_mode {
	u16 hdisplay;
	u16 htotal;
	u16 vdisplay;
	u16 vtotal;
	u8 vrefresh;
};

struct lt9611uxc {
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

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	bool hpd_supported;
	bool edid_read;
	/* can be accessed from different threads, so protect this with ocm_lock */
	bool hdmi_connected;
	uint8_t fw_version;

	/* external display platform device */
	struct platform_device *ext_pdev;
	struct msm_ext_disp_init_data ext_audio_data;
	struct msm_ext_disp_audio_edid_blk audio_edid_blk;
	u8 raw_sad[MAX_NUMBER_ADB * MAX_AUDIO_DATA_BLOCK_SIZE];
	bool audio_support;

	/* CEC support */
	struct cec_adapter *cec_adapter;
	u8 cec_log_addr;
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
	struct lt9611uxc_mode debug_mode;
};

#define LT9611_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611uxc_ranges[] = {
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

static const struct regmap_config lt9611uxc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611uxc_ranges,
	.num_ranges = ARRAY_SIZE(lt9611uxc_ranges),
};

/*
 * This chip supports only a fixed set of modes.
 * Enumerate them here to check whether the mode is supported.
 */
static struct lt9611uxc_mode lt9611uxc_modes[] = {
	{ 3840, 4400, 2160, 2250, 30 },
	{ 1920, 2200, 1080, 1125, 60 },
	{ 1280, 1650, 720,  750,  60 },
	{ 720,  858,  480,  525,  60 },
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

static struct lt9611uxc *bridge_to_lt9611uxc(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611uxc, bridge);
}

static struct lt9611uxc *connector_to_lt9611uxc(struct drm_connector *connector)
{
	return container_of(connector, struct lt9611uxc, connector);
}

static void lt9611uxc_lock(struct lt9611uxc *lt9611uxc)
{
	mutex_lock(&lt9611uxc->ocm_lock);
	regmap_write(lt9611uxc->regmap, 0x80ee, 0x01);
}

static void lt9611uxc_unlock(struct lt9611uxc *lt9611uxc)
{
	regmap_write(lt9611uxc->regmap, 0x80ee, 0x00);
	msleep(50);
	mutex_unlock(&lt9611uxc->ocm_lock);
}

static int lt9611uxc_setup_audio_infoframes(struct lt9611uxc *lt9611uxc,
		struct msm_ext_disp_audio_setup_params *params)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;
	u8 i = 0;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(lt9611uxc->dev, "Failed to setup audio infoframe: %zd\n", err);
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
		dev_err(lt9611uxc->dev, "Failed to pack audio infoframe: %zd\n", err);
		return err;
	}

	lt9611uxc_lock(lt9611uxc);
	/* write checksum and byte 1 to byte 5 */
	for (i = 0; i < 6; i++)
		regmap_write(lt9611uxc->regmap, 0xC8B4 + i, buffer[3 + i]);

	lt9611uxc_unlock(lt9611uxc);

	return 0;
}

static void lt9611uxc_cea_sad_to_raw_sad(struct cea_sad *sads, u8 sad_count,
		u8 *blk)
{
	int i = 0;

	for (i = 0; i < sad_count; i++) {
		blk[i * 3] = (sads[i].format << 3) + sads[i].channels;
		blk[i * 3 + 1] = sads[i].freq;
		blk[i * 3 + 2] = sads[i].byte2;
	}
}

static int lt9611uxc_get_edid_audio_blk(struct msm_ext_disp_audio_edid_blk *blk,
				struct edid *edid)
{
	struct lt9611uxc *lt9611uxc = container_of(blk, struct lt9611uxc, audio_edid_blk);
	int i = 0;

	/* Short Audio Descriptor */
	struct cea_sad *sads;
	int sad_count = 0;

	/* Speaker Allocation Data Block */
	u8 *sadb = NULL;
	int sadb_size = 0;

	sad_count = drm_edid_to_sad(edid, &sads);
	lt9611uxc_cea_sad_to_raw_sad(sads, sad_count, lt9611uxc->raw_sad);
	sadb_size = drm_edid_to_speaker_allocation(edid, &sadb);
	dev_info(lt9611uxc->dev, "sad_count %d, sadb_size %d\n", sad_count, sadb_size);

	blk->audio_data_blk = lt9611uxc->raw_sad;
	blk->audio_data_blk_size = sad_count * 3; /* SAD is 3B */
	for (i = 0; i < blk->audio_data_blk_size; i++)
		dev_info(lt9611uxc->dev, "%02X\n", blk->audio_data_blk[i]);

	blk->spk_alloc_data_blk = sadb;
	blk->spk_alloc_data_blk_size = sadb_size;

	/* from CEA-861-F spec, the size is always 3 bytes */
	for (i = 0; i < blk->spk_alloc_data_blk_size; i++)
		dev_info(lt9611uxc->dev, "%02X\n", blk->spk_alloc_data_blk[i]);

	return 0;
}

static struct lt9611uxc *lt9611uxc_audio_get_pdata(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct lt9611uxc *lt9611uxc;

	if (!pdev) {
		pr_err("Invalid pdev\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		pr_err("Invalid ext disp data\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	lt9611uxc = ext_data->intf_data;
	if (!lt9611uxc) {
		pr_err("Invalid intf data\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	return lt9611uxc;
}

static int hdmi_audio_info_setup(struct platform_device *pdev,
		struct msm_ext_disp_audio_setup_params *params)
{
	struct lt9611uxc *lt9611uxc = lt9611uxc_audio_get_pdata(pdev);
	int rc = 0;

	rc = lt9611uxc_setup_audio_infoframes(lt9611uxc, params);

	return 0;
}

static int hdmi_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	struct lt9611uxc *lt9611uxc = lt9611uxc_audio_get_pdata(pdev);

	lt9611uxc_get_edid_audio_blk(&lt9611uxc->audio_edid_blk, lt9611uxc->edid);

	blk->audio_data_blk = lt9611uxc->audio_edid_blk.audio_data_blk;
	blk->audio_data_blk_size = lt9611uxc->audio_edid_blk.audio_data_blk_size;

	blk->spk_alloc_data_blk = lt9611uxc->audio_edid_blk.spk_alloc_data_blk;
	blk->spk_alloc_data_blk_size =
		lt9611uxc->audio_edid_blk.spk_alloc_data_blk_size;

	return 0;
}

static int hdmi_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	struct lt9611uxc *lt9611uxc = lt9611uxc_audio_get_pdata(pdev);

	return IS_ERR(lt9611uxc) ? PTR_ERR(lt9611uxc): lt9611uxc->hdmi_connected;
}

static int hdmi_audio_get_intf_id(struct platform_device *pdev)
{
	struct lt9611uxc *lt9611uxc = lt9611uxc_audio_get_pdata(pdev);

	return IS_ERR(lt9611uxc) ? PTR_ERR(lt9611uxc): EXT_DISPLAY_TYPE_HDMI;
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

static int hdmi_audio_register_ext_disp(struct lt9611uxc *lt9611uxc)
{
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;
	struct device_node *np = NULL;
	const char *phandle = "lt,ext-disp";

	int rc = 0;

	ext = &lt9611uxc->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_HDMI;
	ext->codec.ctrl_id = 1;
	ext->codec.stream_id = 0;
	ext->pdev = lt9611uxc->audio_pdev;
	ext->intf_data = lt9611uxc;

	ops->audio_info_setup   = hdmi_audio_info_setup;
	ops->get_audio_edid_blk = hdmi_audio_get_edid_blk;
	ops->cable_status       = hdmi_audio_get_cable_status;
	ops->get_intf_id        = hdmi_audio_get_intf_id;
	ops->teardown_done      = hdmi_audio_teardown_done;
	ops->acknowledge        = hdmi_audio_ack_done;
	ops->ready              = hdmi_audio_codec_ready;

	if (!lt9611uxc->dev->of_node) {
		dev_err(lt9611uxc->dev, "cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	np = of_parse_phandle(lt9611uxc->dev->of_node, phandle, 0);
	if (!np) {
		dev_err(lt9611uxc->dev, "cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	lt9611uxc->ext_pdev = of_find_device_by_node(np);
	if (!lt9611uxc->ext_pdev) {
		dev_err(lt9611uxc->dev, "cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	rc = msm_ext_disp_register_intf(lt9611uxc->ext_pdev, ext);
	if (rc)
		dev_err(lt9611uxc->dev, "failed to register ext disp\n");

end:
	return rc;
}

static int hdmi_audio_deregister_ext_disp(struct lt9611uxc *lt9611uxc)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "lt,ext-disp";
	struct msm_ext_disp_init_data *ext;

	ext = &lt9611uxc->ext_audio_data;

	if (!lt9611uxc->dev->of_node) {
		dev_err(lt9611uxc->dev, "cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(lt9611uxc->dev->of_node, phandle, 0);
	if (!pd) {
		dev_err(lt9611uxc->dev, "cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	lt9611uxc->ext_pdev = of_find_device_by_node(pd);
	if (!lt9611uxc->ext_pdev) {
		dev_err(lt9611uxc->dev, "cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	rc = msm_ext_disp_deregister_intf(lt9611uxc->ext_pdev, ext);
	if (rc)
		dev_err(lt9611uxc->dev, "failed to deregister ext disp\n");

end:
	return rc;
}

static irqreturn_t lt9611uxc_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611uxc *lt9611uxc = dev_id;
	unsigned int irq_status = 0;
	unsigned int hpd_status = 0;
	unsigned int cec_status = 0;
	unsigned int cec_tx_status = 0;

	lt9611uxc_lock(lt9611uxc);

	regmap_read(lt9611uxc->regmap, 0xb022, &irq_status);
	regmap_read(lt9611uxc->regmap, 0xb023, &hpd_status);
	regmap_read(lt9611uxc->regmap, 0xb024, &cec_status);
	regmap_read(lt9611uxc->regmap, 0xb027, &cec_tx_status);

	if (irq_status)
		regmap_write(lt9611uxc->regmap, 0xb022, 0);

	if (cec_status)
		regmap_write(lt9611uxc->regmap, 0xb024, 0);

	if (cec_tx_status)
		regmap_write(lt9611uxc->regmap, 0xb027, 0);

	if (irq_status & BIT(0)) {
		lt9611uxc->edid_read = !!(hpd_status & BIT(0));
		wake_up_all(&lt9611uxc->wq);
	}

	if (irq_status & BIT(1)) {
		lt9611uxc->hdmi_connected = hpd_status & BIT(1);
		schedule_work(&lt9611uxc->work);
	}

	if (irq_status & BIT(3)) {
		lt9611uxc->cec_status = !!(cec_status & BIT(7));
		if (lt9611uxc->cec_status)
			schedule_work(&lt9611uxc->cec_recv_work);
	}

	if (cec_tx_status) {
		lt9611uxc->cec_tx_status = cec_tx_status;
		schedule_work(&lt9611uxc->cec_transmit_work);
	}

	lt9611uxc_unlock(lt9611uxc);

	return IRQ_HANDLED;
}

static void lt9611uxc_release_edid(struct lt9611uxc *lt9611uxc)
{
	dev_info(lt9611uxc->dev, "release edid\n");
	kfree(lt9611uxc->edid);
	lt9611uxc->edid = NULL;
	lt9611uxc->edid_read = false;
	drm_connector_update_edid_property(&lt9611uxc->connector, NULL);
}

static void lt9611uxc_helper_hotplug_event(struct lt9611uxc *lt9611uxc)
{
	struct drm_device *dev = NULL;
	char name[32], status[32];
	char *event_string = "HOTPLUG=1";
	char *envp[5];

	dev = lt9611uxc->connector.dev;

	scnprintf(name, 32, "name=%s",
		lt9611uxc->connector.name);
	scnprintf(status, 32, "status=%s",
		drm_get_connector_status_name(lt9611uxc->connector.status));
	envp[0] = name;
	envp[1] = status;
	envp[2] = event_string;
	envp[3] = NULL;
	envp[4] = NULL;

	dev_info(lt9611uxc->dev, "[%s]:[%s]\n", name, status);
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE,
		envp);
}
static void lt9611uxc_hpd_work(struct work_struct *work)
{
	struct lt9611uxc *lt9611uxc = container_of(work, struct lt9611uxc, work);
	bool connected;

	if (lt9611uxc->connector.dev) {
		lt9611uxc->connector.status = (lt9611uxc->hdmi_connected) ?
				connector_status_connected: connector_status_disconnected;
		if (lt9611uxc->connector.status == connector_status_disconnected)
			lt9611uxc_release_edid(lt9611uxc);
		lt9611uxc_helper_hotplug_event(lt9611uxc);
	} else {

		mutex_lock(&lt9611uxc->ocm_lock);
		connected = lt9611uxc->hdmi_connected;
		mutex_unlock(&lt9611uxc->ocm_lock);

		drm_bridge_hpd_notify(&lt9611uxc->bridge,
				      connected ?
				      connector_status_connected :
				      connector_status_disconnected);
	}
}

static void lt9611uxc_reset(struct lt9611uxc *lt9611uxc)
{
	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 1);
	msleep(300);
}

static void lt9611uxc_assert_5v(struct lt9611uxc *lt9611uxc)
{
	if (!lt9611uxc->enable_gpio)
		return;

	gpiod_set_value_cansleep(lt9611uxc->enable_gpio, 1);
	msleep(20);
}

static int lt9611uxc_regulator_init(struct lt9611uxc *lt9611uxc)
{
	int ret;

	lt9611uxc->supplies[0].supply = "vdd";
	lt9611uxc->supplies[1].supply = "vcc";

	ret = devm_regulator_bulk_get(lt9611uxc->dev, 2, lt9611uxc->supplies);
	if (ret < 0)
		return ret;

	ret = regulator_set_voltage(lt9611uxc->supplies[1].consumer, 3300000, 3500000);
	if (ret) {
		pr_err("%s:regulator set voltage failed %d", __func__, ret);
		return ret;
	}

	return regulator_set_load(lt9611uxc->supplies[0].consumer, 200000);
}

static int lt9611uxc_regulator_enable(struct lt9611uxc *lt9611uxc)
{
	int ret;

	ret = regulator_enable(lt9611uxc->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(1000, 10000); /* 50000 according to dtsi */

	ret = regulator_enable(lt9611uxc->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611uxc->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static struct lt9611uxc_mode *lt9611uxc_find_mode(const struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lt9611uxc_modes); i++) {
		if (lt9611uxc_modes[i].hdisplay == mode->hdisplay &&
		    lt9611uxc_modes[i].htotal == mode->htotal &&
		    lt9611uxc_modes[i].vdisplay == mode->vdisplay &&
		    lt9611uxc_modes[i].vtotal == mode->vtotal &&
		    lt9611uxc_modes[i].vrefresh == drm_mode_vrefresh(mode)) {
			return &lt9611uxc_modes[i];
		}
	}

	return NULL;
}

static struct mipi_dsi_device *lt9611uxc_attach_dsi(struct lt9611uxc *lt9611uxc,
						    struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611uxc", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611uxc->dev;
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

static void lt9611uxc_choose_best_mode(struct drm_connector *connector)
{
	struct drm_display_mode *t, *cur_mode, *preferred_mode;
	int cur_vrefresh, preferred_vrefresh;
	int target_refresh = 60;

	if (list_empty(&connector->probed_modes))
		return;

	preferred_mode = list_first_entry(&connector->probed_modes,
			struct drm_display_mode, head);
	list_for_each_entry_safe(cur_mode, t, &connector->probed_modes, head) {
		cur_mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		if (cur_mode == preferred_mode)
			continue;

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

static void lt9611uxc_set_preferred_mode(struct drm_connector *connector)
{
	struct lt9611uxc *lt9611uxc = connector_to_lt9611uxc(connector);
	struct drm_display_mode *mode, *last_mode;
	const char *string;

	if (lt9611uxc->fix_mode) {
		list_for_each_entry(mode, &connector->probed_modes, head) {
			mode->type &= ~DRM_MODE_TYPE_PREFERRED;
			if (lt9611uxc->debug_mode.vdisplay == mode->vdisplay &&
				lt9611uxc->debug_mode.hdisplay == mode->hdisplay &&
				lt9611uxc->debug_mode.vrefresh == drm_mode_vrefresh(mode)) {
				mode->type |= DRM_MODE_TYPE_PREFERRED;
			}
		}
	} else {
		if (!of_property_read_string(lt9611uxc->dev->of_node,
			"lt,preferred-mode", &string)) {
			list_for_each_entry(mode,
				&connector->probed_modes, head) {
					mode->type &= ~DRM_MODE_TYPE_PREFERRED;
				if (!strcmp(mode->name, string))
					mode->type |=
						DRM_MODE_TYPE_PREFERRED;
			}
		} else if (lt9611uxc->edid)
			lt9611uxc_choose_best_mode(connector);
		else
			pr_err("EDID is NULL \n");
	}
}

static int lt9611uxc_connector_get_modes(struct drm_connector *connector)
{
	struct lt9611uxc *lt9611uxc = connector_to_lt9611uxc(connector);
	struct cec_notifier *notify = lt9611uxc->cec_notifier;
	unsigned int count;

	lt9611uxc->edid = lt9611uxc->bridge.funcs->get_edid(&lt9611uxc->bridge, connector);
	drm_connector_update_edid_property(connector, lt9611uxc->edid);
	count = drm_add_edid_modes(connector, lt9611uxc->edid);
	lt9611uxc_set_preferred_mode(connector);

	/* TODO: add checks for num_of_edid_ext_blk == 0 case */
	if (lt9611uxc->cec_support)
		cec_notifier_set_phys_addr_from_edid(notify, lt9611uxc->edid);

	return count;
}

static enum drm_connector_status lt9611uxc_connector_detect(struct drm_connector *connector,
							    bool force)
{
	struct lt9611uxc *lt9611uxc = connector_to_lt9611uxc(connector);

	return lt9611uxc->bridge.funcs->detect(&lt9611uxc->bridge);
}

static enum drm_mode_status lt9611uxc_connector_mode_valid(struct drm_connector *connector,
							   struct drm_display_mode *mode)
{
	struct lt9611uxc_mode *lt9611uxc_mode = lt9611uxc_find_mode(mode);

	return lt9611uxc_mode ? MODE_OK : MODE_BAD;
}

static const struct drm_connector_helper_funcs lt9611uxc_bridge_connector_helper_funcs = {
	.get_modes = lt9611uxc_connector_get_modes,
	.mode_valid = lt9611uxc_connector_mode_valid,
};

static const struct drm_connector_funcs lt9611uxc_bridge_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = lt9611uxc_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lt9611uxc_connector_init(struct drm_bridge *bridge, struct lt9611uxc *lt9611uxc)
{
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	lt9611uxc->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&lt9611uxc->connector,
				 &lt9611uxc_bridge_connector_helper_funcs);
	ret = drm_connector_init(bridge->dev, &lt9611uxc->connector,
				 &lt9611uxc_bridge_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	ret = drm_connector_attach_encoder(&lt9611uxc->connector, bridge->encoder);
	if (ret) {
		DRM_ERROR("Failed to link up connector to encoder: %d\n", ret);
		return ret;
	}

	/* Attach primary DSI */
	lt9611uxc->dsi0 = lt9611uxc_attach_dsi(lt9611uxc, lt9611uxc->dsi0_node);
	if (IS_ERR(lt9611uxc->dsi0)) {
		ret = PTR_ERR(lt9611uxc->dsi0);
		drm_bridge_remove(&lt9611uxc->bridge);
		return ret;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611uxc->dsi1_node) {
		lt9611uxc->dsi1 = lt9611uxc_attach_dsi(lt9611uxc, lt9611uxc->dsi1_node);
		if (IS_ERR(lt9611uxc->dsi1)) {
			ret = PTR_ERR(lt9611uxc->dsi1);
			drm_bridge_remove(&lt9611uxc->bridge);
			return ret;
		}
	}

	return ret;
}

static int lt9611uxc_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);
	int ret;

	ret = lt9611uxc_connector_init(bridge, lt9611uxc);
	if (ret < 0)
		return ret;

	return 0;
}

static enum drm_mode_status
lt9611uxc_bridge_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct lt9611uxc_mode *lt9611uxc_mode;
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);

	lt9611uxc_mode = lt9611uxc_find_mode(mode);

	dev_dbg(lt9611uxc->dev, "%s: %d(%d) x %d(%d)-%d\n",
			lt9611uxc_mode ? "MODE_OK": "MODE_BAD",
			mode->hdisplay, mode->htotal,
			mode->vdisplay, mode->vtotal,
			drm_mode_vrefresh(mode));

	return lt9611uxc_mode ? MODE_OK : MODE_BAD;
}

static void lt9611uxc_video_setup(struct lt9611uxc *lt9611uxc,
				  const struct drm_display_mode *mode)
{
	u32 h_total, hactive, hsync_len, hfront_porch;
	u32 v_total, vactive, vsync_len, vfront_porch;

	h_total = mode->htotal;
	v_total = mode->vtotal;

	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;

	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;

	regmap_write(lt9611uxc->regmap, 0xd00d, (u8)(v_total / 256));
	regmap_write(lt9611uxc->regmap, 0xd00e, (u8)(v_total % 256));

	regmap_write(lt9611uxc->regmap, 0xd00f, (u8)(vactive / 256));
	regmap_write(lt9611uxc->regmap, 0xd010, (u8)(vactive % 256));

	regmap_write(lt9611uxc->regmap, 0xd011, (u8)(h_total / 256));
	regmap_write(lt9611uxc->regmap, 0xd012, (u8)(h_total % 256));

	regmap_write(lt9611uxc->regmap, 0xd013, (u8)(hactive / 256));
	regmap_write(lt9611uxc->regmap, 0xd014, (u8)(hactive % 256));

	regmap_write(lt9611uxc->regmap, 0xd015, (u8)(vsync_len % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd016, 0xf, (u8)(hsync_len / 256));
	regmap_write(lt9611uxc->regmap, 0xd017, (u8)(hsync_len % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd018, 0xf, (u8)(vfront_porch / 256));
	regmap_write(lt9611uxc->regmap, 0xd019, (u8)(vfront_porch % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd01a, 0xf, (u8)(hfront_porch / 256));
	regmap_write(lt9611uxc->regmap, 0xd01b, (u8)(hfront_porch % 256));
}

static void lt9611uxc_bridge_enable(struct drm_bridge *bridge)
{
	struct lt9611uxc *lt9611uxc;
	int rc;

	if (!bridge)
		return;

	pr_info("bridge enable\n");

	lt9611uxc = bridge_to_lt9611uxc(bridge);
	if (lt9611uxc->audio_support) {
		pr_info("notify audio(%d)\n", EXT_DISPLAY_CABLE_CONNECT);
		rc = hdmi_audio_register_ext_disp(lt9611uxc);
		if (rc) {
			pr_err("hdmi audio register failed. rc=%d\n", rc);
			return;
		}
		lt9611uxc->ext_audio_data.intf_ops.audio_config(lt9611uxc->ext_pdev,
				&lt9611uxc->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
		lt9611uxc->ext_audio_data.intf_ops.audio_notify(lt9611uxc->ext_pdev,
				&lt9611uxc->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
	}

}

static void lt9611uxc_bridge_disable(struct drm_bridge *bridge)
{
	struct lt9611uxc *lt9611uxc;

	if (!bridge)
		return;

	lt9611uxc = bridge_to_lt9611uxc(bridge);
	dev_info(lt9611uxc->dev, "bridge disable\n");

	if (lt9611uxc->audio_support) {
		dev_info(lt9611uxc->dev, "notify audio(%d)\n", EXT_DISPLAY_CABLE_DISCONNECT);
		lt9611uxc->ext_audio_data.intf_ops.audio_notify(lt9611uxc->ext_pdev,
				&lt9611uxc->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);
		lt9611uxc->ext_audio_data.intf_ops.audio_config(lt9611uxc->ext_pdev,
				&lt9611uxc->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);
		hdmi_audio_deregister_ext_disp(lt9611uxc);
	}
}

static void lt9611uxc_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj_mode)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);

	lt9611uxc_lock(lt9611uxc);
	lt9611uxc_video_setup(lt9611uxc, mode);
	lt9611uxc_unlock(lt9611uxc);

	dev_info(lt9611uxc->dev, "hdisplay=%d, vdisplay=%d, clock=%d \n",
		mode->hdisplay, mode->vdisplay, mode->clock);
	drm_mode_copy(&lt9611uxc->curr_mode, mode);
}

static enum drm_connector_status lt9611uxc_bridge_detect(struct drm_bridge *bridge)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);
	unsigned int reg_val = 0;
	int ret;
	bool connected = true;

	lt9611uxc_lock(lt9611uxc);

	if (lt9611uxc->hpd_supported) {
		ret = regmap_read(lt9611uxc->regmap, 0xb023, &reg_val);

		if (ret)
			dev_err(lt9611uxc->dev, "failed to read hpd status: %d\n", ret);
		else
			connected  = reg_val & BIT(1);
	}
	lt9611uxc->hdmi_connected = connected;

	lt9611uxc_unlock(lt9611uxc);

	return connected ?  connector_status_connected :
				connector_status_disconnected;
}

static int lt9611uxc_wait_for_edid(struct lt9611uxc *lt9611uxc)
{
	return wait_event_interruptible_timeout(lt9611uxc->wq, lt9611uxc->edid_read,
			msecs_to_jiffies(2000));
}

static int lt9611uxc_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct lt9611uxc *lt9611uxc = data;
	int ret;

	if (len > EDID_BLOCK_SIZE)
		return -EINVAL;

	if (block >= EDID_NUM_BLOCKS)
		return -EINVAL;

	lt9611uxc_lock(lt9611uxc);

	regmap_write(lt9611uxc->regmap, 0xb00b, 0x10);

	regmap_write(lt9611uxc->regmap, 0xb00a, block * EDID_BLOCK_SIZE);

	ret = regmap_noinc_read(lt9611uxc->regmap, 0xb0b0, buf, len);
	if (ret)
		dev_err(lt9611uxc->dev, "edid read failed: %d\n", ret);

	lt9611uxc_unlock(lt9611uxc);

	return 0;
};

static struct edid *lt9611uxc_bridge_get_edid(struct drm_bridge *bridge,
					      struct drm_connector *connector)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);
	int ret;

	ret = lt9611uxc_wait_for_edid(lt9611uxc);
	if (ret < 0) {
		dev_err(lt9611uxc->dev, "wait for EDID failed: %d\n", ret);
		return NULL;
	} else if (ret == 0) {
		dev_err(lt9611uxc->dev, "wait for EDID timeout\n");
		return NULL;
	}

	return drm_do_get_edid(connector, lt9611uxc_get_edid_block, lt9611uxc);
}

static const struct drm_bridge_funcs lt9611uxc_bridge_funcs = {
	.attach = lt9611uxc_bridge_attach,
	.mode_valid = lt9611uxc_bridge_mode_valid,
	.mode_set = lt9611uxc_bridge_mode_set,
	.detect = lt9611uxc_bridge_detect,
	.get_edid = lt9611uxc_bridge_get_edid,
	.enable = lt9611uxc_bridge_enable,
	.disable = lt9611uxc_bridge_disable,
};

static int lt9611uxc_parse_dt(struct device *dev,
			      struct lt9611uxc *lt9611uxc)
{
	lt9611uxc->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611uxc->dsi0_node) {
		dev_err(lt9611uxc->dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611uxc->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	lt9611uxc->audio_support = of_property_read_bool(dev->of_node, "lt,audio-support");
	dev_info(lt9611uxc->dev, "audio support = %d\n", lt9611uxc->audio_support);

	return 0;
}

static int lt9611uxc_gpio_init(struct lt9611uxc *lt9611uxc)
{
	struct device *dev = lt9611uxc->dev;

	lt9611uxc->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611uxc->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611uxc->reset_gpio);
	}

	lt9611uxc->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lt9611uxc->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(lt9611uxc->enable_gpio);
	}

	return 0;
}

static int lt9611uxc_read_device_rev(struct lt9611uxc *lt9611uxc)
{
	unsigned int rev0, rev1, rev2;
	int ret;

	lt9611uxc_lock(lt9611uxc);

	ret = regmap_read(lt9611uxc->regmap, 0x8100, &rev0);
	ret |= regmap_read(lt9611uxc->regmap, 0x8101, &rev1);
	ret |= regmap_read(lt9611uxc->regmap, 0x8102, &rev2);
	if (ret)
		dev_err(lt9611uxc->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611uxc->dev, "LT9611 revision: 0x%02x.%02x.%02x\n", rev0, rev1, rev2);

	lt9611uxc_unlock(lt9611uxc);

	return ret;
}

static int lt9611uxc_read_version(struct lt9611uxc *lt9611uxc)
{
	unsigned int rev;
	int ret;

	lt9611uxc_lock(lt9611uxc);

	ret = regmap_read(lt9611uxc->regmap, 0xb021, &rev);
	if (ret)
		dev_err(lt9611uxc->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611uxc->dev, "LT9611 version: 0x%02x\n", rev);

	lt9611uxc_unlock(lt9611uxc);

	return ret < 0 ? ret : rev;
}

int lt9611_read_cec_msg(struct lt9611uxc *lt9611uxc, struct cec_msg *msg)
{
	unsigned int cec_val, cec_len, i;
	unsigned int reg_cec_flag;

	lt9611uxc_lock(lt9611uxc);
	regmap_read(lt9611uxc->regmap, 0xB024, &reg_cec_flag);
	regmap_read(lt9611uxc->regmap, 0xB030, &cec_len);
	msg->len = (cec_len > 16) ? 16 : cec_len;

	/* TODO: implement regmap_bulk_read */
	for (i = 0; i < msg->len; i++) {
		regmap_read(lt9611uxc->regmap, 0xB031 + i, &cec_val);
		msg->msg[i] = cec_val;
	}
	// Set bit7 = 0 to tell LT9611UXC message received.
	reg_cec_flag &= ~0x80;
	regmap_write(lt9611uxc->regmap, 0xB024, reg_cec_flag);
	lt9611uxc_unlock(lt9611uxc);

	/* TODO hexdump */
	for (i = 0; i < msg->len; i++)
		dev_dbg(lt9611uxc->dev, "received msg[%d] = %x", i, msg->msg[i]);
	return 0;
}

void lt9611uxc_cec_transmit_work(struct work_struct *work)
{
	struct lt9611uxc *lt9611uxc = container_of(work, struct lt9611uxc,
					cec_transmit_work);

	dev_dbg(lt9611uxc->dev, "cec_tx_status %x\n", lt9611uxc->cec_tx_status);

	if (lt9611uxc->cec_tx_status & BIT(2))
		cec_transmit_attempt_done(lt9611uxc->cec_adapter, CEC_TX_STATUS_NACK);
	else if (lt9611uxc->cec_tx_status & BIT(0))
		cec_transmit_attempt_done(lt9611uxc->cec_adapter, CEC_TX_STATUS_OK);
}


void lt9611uxc_cec_recv_work(struct work_struct *work)
{
	struct lt9611uxc *lt9611uxc = container_of(work, struct lt9611uxc, cec_recv_work);
	struct cec_msg cec_msg = {};

	if (!lt9611uxc->cec_status) {
		dev_info(lt9611uxc->dev, "cec message is receiving\n");
		return;
	}

	lt9611_read_cec_msg(lt9611uxc, &cec_msg);
	cec_received_msg(lt9611uxc->cec_adapter, &cec_msg);
}

static int lt9611uxc_cec_enable(struct cec_adapter *adap, bool enable)
{
	struct lt9611uxc *lt9611uxc = cec_get_drvdata(adap);

	lt9611uxc->cec_en = enable;
	return 0;
}

static int lt9611uxc_cec_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct lt9611uxc *lt9611uxc = cec_get_drvdata(adap);

	lt9611uxc->cec_log_addr = logical_addr;
	return 0;
}

static int lt9611uxc_cec_transmit(struct cec_adapter *adap, u8 attempts,
		u32 signal_free_time, struct cec_msg *msg)
{
	int i;
	unsigned int reg_cec_flag;
	struct lt9611uxc *lt9611uxc = cec_get_drvdata(adap);
	unsigned int len = (msg->len > CEC_MAX_MSG_SIZE) ? 16 : msg->len;

	lt9611uxc_lock(lt9611uxc);
	regmap_read(lt9611uxc->regmap, 0xB024, &reg_cec_flag);
	regmap_write(lt9611uxc->regmap, 0xB041, len);
	/* TODO check regmap_bulk_write */
	for (i = 0; i < len; i++)
		regmap_write(lt9611uxc->regmap, 0xB042 + i, msg->msg[i]);
	/* set bit 6 = 1 to tell LT9611UXC sending CEC message */
	reg_cec_flag |= 0x40;
	regmap_write(lt9611uxc->regmap, 0xB024, reg_cec_flag);
	lt9611uxc_unlock(lt9611uxc);

	/* TODO: check hexdump */
	for (i = 0; i < len; i++)
		dev_dbg(lt9611uxc->dev, "cec transmit msg[%d] = %x\n", i, msg->msg[i]);

	return 0;
}

struct cec_adap_ops lt9611uxc_cec_ops = {
	.adap_enable = lt9611uxc_cec_enable,
	.adap_log_addr = lt9611uxc_cec_log_addr,
	.adap_transmit = lt9611uxc_cec_transmit,
};

static int lt9611uxc_cec_adap_init(struct lt9611uxc *lt9611uxc)
{
	int ret = 0;
	struct cec_adapter *adap = NULL;
	unsigned int cec_flags = CEC_CAP_DEFAULTS;

	adap = cec_allocate_adapter(&lt9611uxc_cec_ops, lt9611uxc,
			"lt9611_cec", cec_flags, 1);
	if (!adap) {
		dev_err(lt9611uxc->dev, "cec adapter allocate failed\n");
		return -ENOMEM;
	}

	lt9611uxc->cec_notifier = cec_notifier_cec_adap_register(lt9611uxc->dev,
						NULL, adap);
	if (!lt9611uxc->cec_notifier) {
		dev_err(lt9611uxc->dev, "get cec notifier failed\n");
		cec_delete_adapter(adap);
		lt9611uxc->cec_adapter = NULL;
		lt9611uxc->cec_support = false;
		lt9611uxc->cec_en = false;
		return -ENOMEM;
	}

	ret = cec_register_adapter(adap, lt9611uxc->dev);
	if (ret != 0) {
		dev_err(lt9611uxc->dev, "register cec adapter failed\n");
		cec_delete_adapter(adap);
		lt9611uxc->cec_adapter = NULL;
		lt9611uxc->cec_support = false;
		lt9611uxc->cec_en = false;
	} else {
		dev_dbg(lt9611uxc->dev, "CEC adapter registered\n");
		lt9611uxc->cec_en = true;
		lt9611uxc->cec_support = true;
		lt9611uxc->cec_log_addr = CEC_LOG_ADDR_PLAYBACK_1;

		lt9611uxc->cec_adapter = adap;
		cec_s_log_addrs(lt9611uxc->cec_adapter, NULL, false);
	}

	return ret;
}

static int lt9611uxc_hdmi_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *fmt,
				    struct hdmi_codec_params *hparms)
{
	/*
	 * LT9611UXC will automatically detect rate and sample size, so no need
	 * to setup anything here.
	 */
	return 0;
}

static void lt9611uxc_audio_shutdown(struct device *dev, void *data)
{
}

static int lt9611uxc_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
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

static const struct hdmi_codec_ops lt9611uxc_codec_ops = {
	.hw_params	= lt9611uxc_hdmi_hw_params,
	.audio_shutdown = lt9611uxc_audio_shutdown,
	.get_dai_id	= lt9611uxc_hdmi_i2s_get_dai_id,
};

static int lt9611uxc_audio_init(struct device *dev, struct lt9611uxc *lt9611uxc)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &lt9611uxc_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
		.data = lt9611uxc,
	};

	lt9611uxc->audio_pdev =
		platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					      PLATFORM_DEVID_AUTO,
					      &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(lt9611uxc->audio_pdev);
}

static void lt9611uxc_audio_exit(struct lt9611uxc *lt9611uxc)
{
	if (lt9611uxc->audio_pdev) {
		platform_device_unregister(lt9611uxc->audio_pdev);
		lt9611uxc->audio_pdev = NULL;
	}
}

#define LT9611UXC_FW_PAGE_SIZE 32
static void lt9611uxc_firmware_write_page(struct lt9611uxc *lt9611uxc, u16 addr, const u8 *buf)
{
	struct reg_sequence seq_write_prepare[] = {
		REG_SEQ0(0x805a, 0x04),
		REG_SEQ0(0x805a, 0x00),

		REG_SEQ0(0x805e, 0xdf),
		REG_SEQ0(0x805a, 0x20),
		REG_SEQ0(0x805a, 0x00),
		REG_SEQ0(0x8058, 0x21),
	};

	struct reg_sequence seq_write_addr[] = {
		REG_SEQ0(0x805b, (addr >> 16) & 0xff),
		REG_SEQ0(0x805c, (addr >> 8) & 0xff),
		REG_SEQ0(0x805d, addr & 0xff),
		REG_SEQ0(0x805a, 0x10),
		REG_SEQ0(0x805a, 0x00),
	};

	regmap_write(lt9611uxc->regmap, 0x8108, 0xbf);
	msleep(20);
	regmap_write(lt9611uxc->regmap, 0x8108, 0xff);
	msleep(20);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_write_prepare, ARRAY_SIZE(seq_write_prepare));
	regmap_noinc_write(lt9611uxc->regmap, 0x8059, buf, LT9611UXC_FW_PAGE_SIZE);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_write_addr, ARRAY_SIZE(seq_write_addr));
	msleep(20);
}

static void lt9611uxc_firmware_read_page(struct lt9611uxc *lt9611uxc, u16 addr, char *buf)
{
	struct reg_sequence seq_read_page[] = {
		REG_SEQ0(0x805a, 0xa0),
		REG_SEQ0(0x805a, 0x80),
		REG_SEQ0(0x805b, (addr >> 16) & 0xff),
		REG_SEQ0(0x805c, (addr >> 8) & 0xff),
		REG_SEQ0(0x805d, addr & 0xff),
		REG_SEQ0(0x805a, 0x90),
		REG_SEQ0(0x805a, 0x80),
		REG_SEQ0(0x8058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxc->regmap, seq_read_page, ARRAY_SIZE(seq_read_page));
	regmap_noinc_read(lt9611uxc->regmap, 0x805f, buf, LT9611UXC_FW_PAGE_SIZE);
}

static char *lt9611uxc_firmware_read(struct lt9611uxc *lt9611uxc, size_t size)
{
	struct reg_sequence seq_read_setup[] = {
		REG_SEQ0(0x805a, 0x84),
		REG_SEQ0(0x805a, 0x80),
	};

	char *readbuf;
	u16 offset;

	readbuf = kzalloc(ALIGN(size, 32), GFP_KERNEL);
	if (!readbuf)
		return NULL;

	regmap_multi_reg_write(lt9611uxc->regmap, seq_read_setup, ARRAY_SIZE(seq_read_setup));

	for (offset = 0;
	     offset < size;
	     offset += LT9611UXC_FW_PAGE_SIZE)
		lt9611uxc_firmware_read_page(lt9611uxc, offset, &readbuf[offset]);

	return readbuf;
}

static int lt9611uxc_firmware_update(struct lt9611uxc *lt9611uxc)
{
	int ret;
	u16 offset;
	size_t remain;
	char *readbuf;
	const struct firmware *fw;

	struct reg_sequence seq_setup[] = {
		REG_SEQ0(0x805e, 0xdf),
		REG_SEQ0(0x8058, 0x00),
		REG_SEQ0(0x8059, 0x50),
		REG_SEQ0(0x805a, 0x10),
		REG_SEQ0(0x805a, 0x00),
	};


	struct reg_sequence seq_block_erase[] = {
		REG_SEQ0(0x805a, 0x04),
		REG_SEQ0(0x805a, 0x00),
		REG_SEQ0(0x805b, 0x00),
		REG_SEQ0(0x805c, 0x00),
		REG_SEQ0(0x805d, 0x00),
		REG_SEQ0(0x805a, 0x01),
		REG_SEQ0(0x805a, 0x00),
	};

	ret = request_firmware(&fw, "lt9611uxc_fw.bin", lt9611uxc->dev);
	if (ret < 0)
		return ret;

	dev_info(lt9611uxc->dev, "Updating firmware\n");
	lt9611uxc_lock(lt9611uxc);

	regmap_multi_reg_write(lt9611uxc->regmap, seq_setup, ARRAY_SIZE(seq_setup));

	/*
	 * Need erase block 2 timess here. Sometimes, block erase can fail.
	 * This is a workaroud.
	 */
	regmap_multi_reg_write(lt9611uxc->regmap, seq_block_erase, ARRAY_SIZE(seq_block_erase));
	msleep(3000);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_block_erase, ARRAY_SIZE(seq_block_erase));
	msleep(3000);

	for (offset = 0, remain = fw->size;
	     remain >= LT9611UXC_FW_PAGE_SIZE;
	     offset += LT9611UXC_FW_PAGE_SIZE, remain -= LT9611UXC_FW_PAGE_SIZE)
		lt9611uxc_firmware_write_page(lt9611uxc, offset, fw->data + offset);

	if (remain > 0) {
		char buf[LT9611UXC_FW_PAGE_SIZE];

		memset(buf, 0xff, LT9611UXC_FW_PAGE_SIZE);
		memcpy(buf, fw->data + offset, remain);
		lt9611uxc_firmware_write_page(lt9611uxc, offset, buf);
	}
	msleep(20);

	readbuf = lt9611uxc_firmware_read(lt9611uxc, fw->size);
	if (!readbuf) {
		ret = -ENOMEM;
		goto out;
	}

	if (!memcmp(readbuf, fw->data, fw->size)) {
		dev_err(lt9611uxc->dev, "Firmware update failed\n");
		print_hex_dump(KERN_ERR, "fw: ", DUMP_PREFIX_OFFSET,
				16, 1, readbuf, fw->size, false);
		ret = -EINVAL;
	} else {
		dev_info(lt9611uxc->dev, "Firmware updates successfully\n");
		ret = 0;
	}
	kfree(readbuf);

out:
	lt9611uxc_unlock(lt9611uxc);
	lt9611uxc_reset(lt9611uxc);
	release_firmware(fw);

	return ret;
}

static ssize_t lt9611uxc_firmware_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);
	int ret;

	ret = lt9611uxc_firmware_update(lt9611uxc);
	if (ret < 0)
		return ret;
	return len;
}

static ssize_t lt9611uxc_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%02x\n", lt9611uxc->fw_version);
}

static ssize_t edid_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%dx%dx%d\n",
			lt9611uxc->curr_mode.hdisplay, lt9611uxc->curr_mode.vdisplay,
			drm_mode_vrefresh(&lt9611uxc->curr_mode));
};

static ssize_t edid_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	int hdisplay = 0, vdisplay = 0, vrefresh = 0;
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);

	if (!lt9611uxc)
		return -EINVAL;

	if (sscanf(buf, "%d %d %d", &hdisplay, &vdisplay, &vrefresh) != 3)
		goto err;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto err;

	lt9611uxc->fix_mode = true;
	lt9611uxc->debug_mode.hdisplay = hdisplay;
	lt9611uxc->debug_mode.vdisplay = vdisplay;
	lt9611uxc->debug_mode.vrefresh = vrefresh;

	dev_info(lt9611uxc->dev, "fixed mode hdisplay=%d vdisplay=%d, vrefresh=%d\n",
			hdisplay, vdisplay, vrefresh);

	return count;
err:
	lt9611uxc->fix_mode = false;
	return -EINVAL;
}

static DEVICE_ATTR_RW(lt9611uxc_firmware);
static DEVICE_ATTR_RW(edid_mode);

static struct attribute *lt9611uxc_attrs[] = {
	&dev_attr_lt9611uxc_firmware.attr,
	&dev_attr_edid_mode.attr,
	NULL,
};

static const struct attribute_group lt9611uxc_attr_group = {
	.attrs = lt9611uxc_attrs,
};

static const struct attribute_group *lt9611uxc_attr_groups[] = {
	&lt9611uxc_attr_group,
	NULL,
};

static int lt9611uxc_probe(struct i2c_client *client)
{
	struct lt9611uxc *lt9611uxc;
	struct device *dev = &client->dev;
	int ret;
	bool fw_updated = false;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611uxc = devm_kzalloc(dev, sizeof(*lt9611uxc), GFP_KERNEL);
	if (!lt9611uxc)
		return -ENOMEM;

	lt9611uxc->dev = dev;
	lt9611uxc->client = client;
	mutex_init(&lt9611uxc->ocm_lock);

	lt9611uxc->regmap = devm_regmap_init_i2c(client, &lt9611uxc_regmap_config);
	if (IS_ERR(lt9611uxc->regmap)) {
		dev_err(lt9611uxc->dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611uxc->regmap);
	}

	ret = lt9611uxc_parse_dt(dev, lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611uxc_gpio_init(lt9611uxc);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611uxc_regulator_init(lt9611uxc);
	if (ret < 0)
		goto err_of_put;

	lt9611uxc_assert_5v(lt9611uxc);

	ret = lt9611uxc_regulator_enable(lt9611uxc);
	if (ret)
		goto err_of_put;

	lt9611uxc_reset(lt9611uxc);

	ret = lt9611uxc_read_device_rev(lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
	}

retry:
	ret = lt9611uxc_read_version(lt9611uxc);
	if (ret < 0) {
		dev_err(dev, "failed to read FW version\n");
	} else if (ret == 0) {
		if (!fw_updated) {
			fw_updated = true;
			dev_err(dev, "FW version 0, enforcing firmware update\n");
			ret = lt9611uxc_firmware_update(lt9611uxc);
			if (ret < 0)
				dev_err(dev, "FW update fail\n");
			else
				goto retry;
		} else {
			dev_err(dev, "FW version 0, update failed\n");
			ret = -EOPNOTSUPP;
		}
	} else if (ret < 0x40) {
		dev_info(dev, "FW version 0x%x, HPD not supported\n", ret);
	} else {
		lt9611uxc->hpd_supported = true;
	}
	lt9611uxc->fw_version = ret;

	init_waitqueue_head(&lt9611uxc->wq);
	INIT_WORK(&lt9611uxc->work, lt9611uxc_hpd_work);
	INIT_WORK(&lt9611uxc->cec_recv_work, lt9611uxc_cec_recv_work);
	INIT_WORK(&lt9611uxc->cec_transmit_work, lt9611uxc_cec_transmit_work);

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					lt9611uxc_irq_thread_handler,
					IRQF_ONESHOT, "lt9611uxc", lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
	}

	i2c_set_clientdata(client, lt9611uxc);

	lt9611uxc->bridge.funcs = &lt9611uxc_bridge_funcs;
	lt9611uxc->bridge.of_node = client->dev.of_node;
	lt9611uxc->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID;
	if (lt9611uxc->hpd_supported)
		lt9611uxc->bridge.ops |= DRM_BRIDGE_OP_HPD;

	ret = lt9611uxc_cec_adap_init(lt9611uxc);
	if (ret)
		dev_err(dev, "CEC init failed. ret=%d\n", ret);
	else
		dev_dbg(dev, "CEC init success\n");

	lt9611uxc->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&lt9611uxc->bridge);

	return lt9611uxc_audio_init(dev, lt9611uxc);

err_of_put:
	of_node_put(lt9611uxc->dsi1_node);
	of_node_put(lt9611uxc->dsi0_node);

	return ret;
}

static void lt9611uxc_remove(struct i2c_client *client)
{
	struct lt9611uxc *lt9611uxc = i2c_get_clientdata(client);

	disable_irq(client->irq);
	cancel_work_sync(&lt9611uxc->work);
	lt9611uxc_audio_exit(lt9611uxc);
	drm_bridge_remove(&lt9611uxc->bridge);
	/* TODO check order of closing wq */
	cancel_work_sync(&lt9611uxc->cec_recv_work);
	cancel_work_sync(&lt9611uxc->cec_transmit_work);
	if (lt9611uxc->cec_adapter)
		cec_unregister_adapter(lt9611uxc->cec_adapter);

	mutex_destroy(&lt9611uxc->ocm_lock);

	regulator_bulk_disable(ARRAY_SIZE(lt9611uxc->supplies), lt9611uxc->supplies);

	of_node_put(lt9611uxc->dsi1_node);
	of_node_put(lt9611uxc->dsi0_node);
}

static struct i2c_device_id lt9611uxc_id[] = {
	{ "lt,lt9611uxc", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611uxc_match_table[] = {
	{ .compatible = "lt,lt9611uxc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt9611uxc_match_table);

static struct i2c_driver lt9611uxc_driver = {
	.driver = {
		.name = "lt9611uxc",
		.of_match_table = lt9611uxc_match_table,
		.dev_groups = lt9611uxc_attr_groups,
	},
	.probe = lt9611uxc_probe,
	.remove = lt9611uxc_remove,
	.id_table = lt9611uxc_id,
};
module_i2c_driver(lt9611uxc_driver);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_LICENSE("GPL v2");
