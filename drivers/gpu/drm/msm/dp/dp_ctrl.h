/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CTRL_H_
#define _DP_CTRL_H_

#include "dp_aux.h"
#include "dp_panel.h"
#include "dp_link.h"
#include "dp_catalog.h"

struct msm_dp_ctrl {
	bool wide_bus_en;
};

struct phy;

int msm_dp_ctrl_on_link(struct msm_dp_ctrl *msm_dp_ctrl, bool mst_active);
int msm_dp_ctrl_on_stream(struct msm_dp_ctrl *msm_dp_ctrl,
			  struct msm_dp_panel *msm_dp_panel, u32 max_streams);
int msm_dp_ctrl_prepare_stream_on(struct msm_dp_ctrl *msm_dp_ctrl, bool force_link_train);
void msm_dp_ctrl_off_link_stream(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_off_link(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_stream_clk_off(struct msm_dp_ctrl *msm_dp_ctrl, struct msm_dp_panel *msm_dp_panel);
void msm_dp_ctrl_push_idle(struct msm_dp_ctrl *msm_dp_ctrl);
irqreturn_t msm_dp_ctrl_isr(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_handle_sink_request(struct msm_dp_ctrl *msm_dp_ctrl);
struct msm_dp_ctrl *msm_dp_ctrl_get(struct device *dev, struct msm_dp_link *link,
			struct msm_dp_panel *panel,	struct drm_dp_aux *aux,
			struct msm_dp_catalog *catalog,
			struct phy *phy);

void msm_dp_ctrl_reset_irq_ctrl(struct msm_dp_ctrl *msm_dp_ctrl, bool enable);
void msm_dp_ctrl_phy_init(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_phy_exit(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_irq_phy_exit(struct msm_dp_ctrl *msm_dp_ctrl);

void msm_dp_ctrl_set_psr(struct msm_dp_ctrl *msm_dp_ctrl, bool enable);
void msm_dp_ctrl_config_psr(struct msm_dp_ctrl *msm_dp_ctrl);

int msm_dp_ctrl_core_clk_enable(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_core_clk_disable(struct msm_dp_ctrl *msm_dp_ctrl);

void msm_dp_ctrl_clear_vsc_sdp_pkt(struct msm_dp_ctrl *msm_dp_ctrl,
		struct msm_dp_panel *msm_dp_panel);
void msm_dp_ctrl_psm_config(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_reinit_phy(struct msm_dp_ctrl *msm_dp_ctrl);
int msm_dp_ctrl_mst_send_act(struct msm_dp_ctrl *msm_dp_ctrl);
void msm_dp_ctrl_mst_stream_channel_slot_setup(struct msm_dp_ctrl *msm_dp_ctrl, u32 max_streams);
void msm_dp_ctrl_set_mst_channel_info(struct msm_dp_ctrl *msm_dp_ctrl,
				      enum msm_dp_stream_id strm,
				      u32 start_slot, u32 tot_slots);
void msm_dp_ctrl_push_vcpf(struct msm_dp_ctrl *msm_dp_ctrl, struct msm_dp_panel *msm_dp_panel);
#endif /* _DP_CTRL_H_ */
