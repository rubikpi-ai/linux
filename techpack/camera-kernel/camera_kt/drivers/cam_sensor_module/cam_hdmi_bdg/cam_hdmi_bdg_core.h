/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _CAMHDMIBDGCORE_H_
#define _CAMHDMIBDGCORE_H_

#include <linux/firmware.h>
#include "cam_sensor_dev.h"

#define HDMI_BDG_SENSOR_ID 0x1901
#define HDMI_BDG_HDMI_CONNECTED    0x01
#define HDMI_BDG_HDMI_DISCONNECTED 0x00
#define HDMI_SENSOR_NAME "lt6911gxc"

enum lt6911_fw_status {
	UPDATE_SUCCESS = 0,
	UPDATE_RUNNING = 1,
	UPDATE_FAILED = 2,
};

/**
 * This API upgrade lt6911 firmware.
 */
int cam_hdmi_bdg_upgrade_firmware(void);

/**
 * @s_ctrl: Sensor ctrl structure
 *
 * This API set lt6911 camera struct for irq handle.
 */
int cam_hdmi_bdg_set_cam_ctrl(struct cam_sensor_ctrl_t *s_ctrl);

/**
 * This API unset lt6911 camera struct for irq handle.
 */
void cam_hdmi_bdg_unset_cam_ctrl(void);

int cam_hdmi_bdg_get_src_resolution(bool *signal_stable,
		int *width, int *height, int *id);

uint32_t cam_hdmi_bdg_get_fw_version(void);

/**
 * @brief : API to register HMDI dev to platform framework.
 * @return struct platform_device pointer on on success, or ERR_PTR() on error.
 */
int hdmi_bdg_irq_handler_init(void);

/**
 * @brief : API to remove HMDI dev from platform framework.
 */
void hdmi_bdg_irq_handler_exit(void);

#endif /* _CAMHDMIBDGCORE_H_ */
