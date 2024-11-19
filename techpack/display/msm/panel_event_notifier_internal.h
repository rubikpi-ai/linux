/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_panel.h>

enum panel_event_notifier_tag {
	PANEL_EVENT_NOTIFICATION_NONE,
	PANEL_EVENT_NOTIFICATION_PRIMARY,
	PANEL_EVENT_NOTIFICATION_SECONDARY,
	PANEL_EVENT_NOTIFICATION_MAX
};

enum panel_event_notification_type {
	DRM_PANEL_EVENT_NONE,
	DRM_PANEL_EVENT_BLANK,
	DRM_PANEL_EVENT_UNBLANK,
	DRM_PANEL_EVENT_BLANK_LP,
	DRM_PANEL_EVENT_FPS_CHANGE,
	DRM_PANEL_EVENT_MAX
};

struct panel_event_notification_data {
	u32 old_fps;
	u32 new_fps;
	bool early_trigger;
};

struct panel_event_notification {
	enum panel_event_notification_type notif_type;
	struct panel_event_notification_data notif_data;
	struct drm_panel *panel;
};

void panel_event_notification_trigger(enum panel_event_notifier_tag tag,
		 struct panel_event_notification *notification)
{
}
