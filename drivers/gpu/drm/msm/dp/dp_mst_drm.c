// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "dp_mst_drm.h"

static struct drm_private_state *msm_dp_mst_duplicate_bridge_state(struct drm_private_obj *obj)
{
	struct msm_dp_mst_bridge_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void msm_dp_mst_destroy_bridge_state(struct drm_private_obj *obj,
					    struct drm_private_state *state)
{
	struct msm_dp_mst_bridge_state *priv_state =
		to_msm_dp_mst_bridge_priv_state(state);

	kfree(priv_state);
}

static const struct drm_private_state_funcs msm_dp_mst_bridge_state_funcs = {
	.atomic_duplicate_state = msm_dp_mst_duplicate_bridge_state,
	.atomic_destroy_state = msm_dp_mst_destroy_bridge_state,
};

/**
 * dp_mst_find_vcpi_slots() - Find VCPI slots for this PBN value
 * @mgr: manager to use
 * @pbn: payload bandwidth to convert into slots.
 *
 * Calculate the number of VCPI slots that will be required for the given PBN
 * value.
 *
 * RETURNS:
 * The total slots required for this port, or error.
 */
static int msm_dp_mst_find_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, int pbn)
{
	int num_slots;
	struct drm_dp_mst_topology_state *state;

	state = to_drm_dp_mst_topology_state(mgr->base.state);
	num_slots = DIV_ROUND_UP(pbn, state->pbn_div);

	/* max. time slots - one slot for MTP header */
	if (num_slots > 63)
		return -ENOSPC;
	return num_slots;
}

static int msm_dp_mst_calc_pbn_mode(struct msm_dp_panel *msm_dp_panel)
{
	int pbn, bpp;
	s64 pbn_fp;

	bpp = msm_dp_panel->msm_dp_mode.bpp;

	pbn = drm_dp_calc_pbn_mode(msm_dp_panel->msm_dp_mode.drm_mode.clock, bpp << 4);
	pbn_fp = drm_fixp_from_fraction(pbn, 1);
	msm_dp_panel->mst_caps.pbn_no_overhead = pbn;

	pbn = drm_fixp2int(pbn_fp);
	msm_dp_panel->mst_caps.pbn = pbn;

	return pbn;
}

static void _msm_dp_mst_update_timeslots(struct msm_dp_mst *mst,
					 struct msm_dp_mst_bridge *mst_bridge,
					 struct drm_dp_mst_port *port)
{
	int i;
	struct msm_dp_mst_bridge *msm_dp_bridge;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	int prev_start = 0;
	int prev_slots = 0;

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] update_timeslots failed, null payload\n",
			  mst_bridge->id);
		return;
	}

	for (i = 0; i < MAX_DP_MST_DRM_BRIDGES; i++) {
		msm_dp_bridge = &mst->mst_bridge[i];
		if (mst_bridge == msm_dp_bridge) {
			/*
			 * When a payload was removed make sure to move any payloads after it
			 * to the left so all payloads are aligned to the left.
			 */
			if (payload->vc_start_slot < 0) {
				// cache the payload
				prev_start = msm_dp_bridge->start_slot;
				prev_slots = msm_dp_bridge->num_slots;
				msm_dp_bridge->pbn = 0;
				msm_dp_bridge->start_slot = 1;
				msm_dp_bridge->num_slots = 0;
				msm_dp_bridge->vcpi = 0;
			} else { //add payload
				msm_dp_bridge->pbn = payload->pbn;
				msm_dp_bridge->start_slot = payload->vc_start_slot;
				msm_dp_bridge->num_slots = payload->time_slots;
				msm_dp_bridge->vcpi = payload->vcpi;
			}
		}
	}

	// Now commit all the updated payloads
	for (i = 0; i < MAX_DP_MST_DRM_BRIDGES; i++) {
		msm_dp_bridge = &mst->mst_bridge[i];

		//Shift payloads to the left if there was a removed payload.
		if (payload->vc_start_slot < 0 && msm_dp_bridge->start_slot > prev_start)
			msm_dp_bridge->start_slot -= prev_slots;

		msm_dp_display_set_stream_info(mst->msm_dp, msm_dp_bridge->msm_dp_panel,
					       msm_dp_bridge->id, msm_dp_bridge->start_slot,
					       msm_dp_bridge->num_slots,
					       msm_dp_bridge->pbn, msm_dp_bridge->vcpi);
		drm_dbg_dp(mst->msm_dp->drm_dev,
			   "conn:%d vcpi:%d start_slot:%d num_slots:%d, pbn:%d\n",
			   DP_MST_CONN_ID(msm_dp_bridge), msm_dp_bridge->vcpi,
			   msm_dp_bridge->start_slot,
			   msm_dp_bridge->num_slots, msm_dp_bridge->pbn);
	}
}

static int _msm_dp_mst_bridge_pre_enable_part1(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;
	int pbn, slots;
	int rc = 0;

	pbn = msm_dp_mst_calc_pbn_mode(dp_bridge->msm_dp_panel);

	slots = msm_dp_mst_find_vcpi_slots(&mst->mst_mgr, pbn);

	drm_dbg_dp(msm_dp->drm_dev, "conn:%d pbn:%d, slots:%d\n", DP_MST_CONN_ID(dp_bridge),
		   pbn, slots);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);
	if (!payload || payload->time_slots <= 0) {
		DRM_ERROR("time slots not allocated for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		rc = -EINVAL;
		return rc;
	}

	drm_dp_mst_update_slots(mst_state, DP_CAP_ANSI_8B10B);

	rc = drm_dp_add_payload_part1(&mst->mst_mgr, mst_state, payload);
	if (rc) {
		DRM_ERROR("payload allocation failure for conn:%d\n", DP_MST_CONN_ID(dp_bridge));
		return rc;
	}

	_msm_dp_mst_update_timeslots(mst, dp_bridge, port);

	return rc;
}

static void _msm_dp_mst_bridge_pre_enable_part2(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;

	drm_dp_check_act_status(&mst->mst_mgr);

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] null payload\n", dp_bridge->id);
		return;
	}

	if (!payload->port) {
		DRM_ERROR("mst bridge [%d] null port\n", dp_bridge->id);
		return;
	}

	if (!payload->port->connector) {
		DRM_ERROR("mst bridge [%d] part-2 failed, null connector\n",
			  dp_bridge->id);
		return;
	}

	if (payload->vc_start_slot == -1) {
		DRM_ERROR("mst bridge [%d] part-2 failed, payload alloc part 1 failed\n",
			  dp_bridge->id);
		return;
	}

	drm_dp_add_payload_part2(&mst->mst_mgr, mst_state->base.state, payload);
	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre enable part-2 complete\n",
		   dp_bridge->id);
}

static void _msm_dp_mst_bridge_pre_disable_part1(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;
	struct msm_dp_mst_connector *mst_conn = to_msm_dp_mst_connector(dp_bridge->connector);
	struct drm_dp_mst_port *port = mst_conn->mst_port;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *payload;

	mst_state = to_drm_dp_mst_topology_state(mst->mst_mgr.base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, port);

	if (!payload) {
		DRM_ERROR("mst bridge [%d] _pre disable part-1 failed, null payload\n",
			  dp_bridge->id);
		return;
	}

	drm_dp_remove_payload(&mst->mst_mgr, mst_state, payload, payload);
	_msm_dp_mst_update_timeslots(mst, dp_bridge, port);

	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre disable part-1 complete\n",
		   dp_bridge->id);
}

static void _msm_dp_mst_bridge_pre_disable_part2(struct msm_dp_mst_bridge *dp_bridge)
{
	struct msm_dp *msm_dp = dp_bridge->display;
	struct msm_dp_mst *mst = msm_dp->msm_dp_mst;

	drm_dp_check_act_status(&mst->mst_mgr);

	drm_dbg_dp(msm_dp->drm_dev, "mst bridge [%d] _pre disable part-2 complete\n",
		   dp_bridge->id);
}

static void msm_dp_mst_bridge_atomic_pre_enable(struct drm_bridge *drm_bridge,
						struct drm_bridge_state *old_bridge_state)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;
	struct msm_dp_mst_bridge_state *msm_dp_bridge_state;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	msm_dp_bridge_state = to_msm_dp_mst_bridge_state(bridge);
	dp = bridge->display;

	/* to cover cases of bridge_disable/bridge_enable without modeset */
	bridge->connector = msm_dp_bridge_state->connector;
	bridge->msm_dp_panel = msm_dp_bridge_state->msm_dp_panel;

	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	msm_dp_display_atomic_prepare(dp);

	rc = _msm_dp_mst_bridge_pre_enable_part1(bridge);
	if (rc) {
		DRM_ERROR("[%d] DP display pre-enable failed, rc=%d\n", bridge->id, rc);
		msm_dp_display_unprepare(dp);
		return;
	}

	msm_dp_display_enable_helper(dp, bridge->msm_dp_panel);

	_msm_dp_mst_bridge_pre_enable_part2(bridge);

	drm_dbg_dp(dp->drm_dev, "conn:%d mode:%s fps:%d vcpi:%d slots:%d to %d\n",
		   DP_MST_CONN_ID(bridge), bridge->drm_mode.name,
		   drm_mode_vrefresh(&bridge->drm_mode),
		   bridge->vcpi, bridge->start_slot,
		   bridge->start_slot + bridge->num_slots);
}

static void msm_dp_mst_bridge_atomic_disable(struct drm_bridge *drm_bridge,
					     struct drm_bridge_state *old_bridge_state)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	dp = bridge->display;

	_msm_dp_mst_bridge_pre_disable_part1(bridge);

	msm_dp_display_disable_helper(dp, bridge->msm_dp_panel);

	_msm_dp_mst_bridge_pre_disable_part2(bridge);

	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d disable complete\n", bridge->id,
		   DP_MST_CONN_ID(bridge));
}

static void msm_dp_mst_bridge_atomic_post_disable(struct drm_bridge *drm_bridge,
						  struct drm_bridge_state *old_bridge_state)
{
	int conn = 0;
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp *dp;

	if (!drm_bridge) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);
	if (!bridge->connector) {
		DRM_ERROR("Invalid connector\n");
		return;
	}

	conn = DP_MST_CONN_ID(bridge);

	dp = bridge->display;

	msm_dp_display_atomic_post_disable_helper(dp, bridge->msm_dp_panel);

	if (!dp->mst_active)
		msm_dp_display_unprepare(dp);

	bridge->connector = NULL;
	bridge->msm_dp_panel =  NULL;

	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d post disable complete\n",
		   bridge->id, conn);
}

static void msm_dp_mst_bridge_mode_set(struct drm_bridge *drm_bridge,
				       const struct drm_display_mode *mode,
				       const struct drm_display_mode *adjusted_mode)
{
	struct msm_dp_mst_bridge *bridge;
	struct msm_dp_mst_bridge_state *dp_bridge_state;
	struct msm_dp *dp;

	if (!drm_bridge || !mode || !adjusted_mode) {
		DRM_ERROR("Invalid params\n");
		return;
	}

	bridge = to_msm_dp_mst_bridge(drm_bridge);

	dp_bridge_state = to_msm_dp_mst_bridge_state(bridge);
	bridge->connector = dp_bridge_state->connector;
	bridge->msm_dp_panel = dp_bridge_state->msm_dp_panel;

	dp = bridge->display;

	memset(&bridge->msm_dp_mode, 0x0, sizeof(struct msm_dp_display_mode));
	memcpy(&bridge->drm_mode, adjusted_mode, sizeof(bridge->drm_mode));
	msm_dp_display_mode_set_helper(dp, mode, adjusted_mode, bridge->msm_dp_panel);
	memcpy(&bridge->msm_dp_mode, &bridge->msm_dp_panel->msm_dp_mode,
	       sizeof(struct msm_dp_display_mode));
	drm_dbg_dp(dp->drm_dev, "mst bridge:%d conn:%d mode set complete %s\n", bridge->id,
		   DP_MST_CONN_ID(bridge), mode->name);
}

/* DP MST Bridge APIs */
static const struct drm_bridge_funcs msm_dp_mst_bridge_ops = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset           = drm_atomic_helper_bridge_reset,
	.atomic_pre_enable   = msm_dp_mst_bridge_atomic_pre_enable,
	.atomic_disable      = msm_dp_mst_bridge_atomic_disable,
	.atomic_post_disable = msm_dp_mst_bridge_atomic_post_disable,
	.mode_set     = msm_dp_mst_bridge_mode_set,
};

int msm_dp_mst_drm_bridge_init(struct msm_dp *dp, struct drm_encoder *encoder)
{
	int rc = 0;
	struct msm_dp_mst_bridge *bridge = NULL;
	struct msm_dp_mst_bridge_state *state;
	struct drm_device *dev;
	struct msm_dp_mst *mst = dp->msm_dp_mst;
	int i;

	for (i = 0; i < MAX_DP_MST_DRM_BRIDGES; i++) {
		if (!mst->mst_bridge[i].in_use) {
			bridge = &mst->mst_bridge[i];
			bridge->encoder = encoder;
			bridge->in_use = true;
			bridge->id = i;
			break;
		}
	}

	if (i == MAX_DP_MST_DRM_BRIDGES) {
		DRM_ERROR("mst supports only %d bridges\n", i);
		rc = -EACCES;
		goto end;
	}

	dev = dp->drm_dev;
	bridge->display = dp;
	bridge->base.funcs = &msm_dp_mst_bridge_ops;
	bridge->base.encoder = encoder;
	bridge->base.type = dp->connector_type;
	bridge->base.ops = DRM_BRIDGE_OP_MODES;
	drm_bridge_add(&bridge->base);

	rc = drm_bridge_attach(encoder, &bridge->base, NULL, 0);
	if (rc) {
		DRM_ERROR("failed to attach bridge, rc=%d\n", rc);
		goto end;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		rc = -ENOMEM;
		goto end;
	}

	drm_atomic_private_obj_init(dev, &bridge->obj,
				    &state->base,
				    &msm_dp_mst_bridge_state_funcs);

	drm_dbg_dp(dp->drm_dev, "mst drm bridge init. bridge id:%d\n", i);

	return 0;

end:
	return rc;
}
