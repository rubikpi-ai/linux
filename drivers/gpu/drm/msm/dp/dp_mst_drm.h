// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DP_MST_DRM_H_
#define _DP_MST_DRM_H_

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/version.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fixed.h>
#include <drm/drm_connector.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>

#include "dp_panel.h"
#include "dp_display.h"

#define MAX_DP_MST_DRM_BRIDGES		4

#define DP_MST_CONN_ID(bridge) ((bridge)->connector ? \
		(bridge)->connector->base.id : 0)

struct msm_dp_mst_bridge {
	struct drm_bridge base;
	struct drm_private_obj obj;
	u32 id;

	bool in_use;

	struct msm_dp *display;
	struct drm_encoder *encoder;

	struct drm_display_mode drm_mode;
	struct msm_dp_display_mode msm_dp_mode;
	struct drm_connector *connector;
	struct msm_dp_panel *msm_dp_panel;

	int vcpi;
	int pbn;
	int num_slots;
	int start_slot;
};

struct msm_dp_mst_bridge_state {
	struct drm_private_state base;
	struct drm_connector *connector;
	struct msm_dp_panel *msm_dp_panel;
	int num_slots;
};

struct msm_dp_mst {
	bool mst_initialized;
	struct drm_dp_mst_topology_mgr mst_mgr;
	struct msm_dp_mst_bridge mst_bridge[MAX_DP_MST_DRM_BRIDGES];
	struct msm_dp *msm_dp;
	bool mst_session_hpd_state;
};

struct msm_dp_mst_connector {
	struct drm_connector connector;
	struct drm_dp_mst_port *mst_port;
};

#define to_msm_dp_mst_bridge(x)     container_of((x), struct msm_dp_mst_bridge, base)
#define to_msm_dp_mst_bridge_priv(x) \
		container_of((x), struct msm_dp_mst_bridge, obj)
#define to_msm_dp_mst_bridge_priv_state(x) \
		container_of((x), struct msm_dp_mst_bridge_state, base)
#define to_msm_dp_mst_bridge_state(x) \
		to_msm_dp_mst_bridge_priv_state((x)->obj.state)
#define to_msm_dp_mst_connector(x) \
		container_of((x), struct msm_dp_mst_connector, connector)
int msm_dp_mst_drm_bridge_init(struct msm_dp *dp, struct drm_encoder *encoder);

#endif /* _DP_MST_DRM_H_ */
