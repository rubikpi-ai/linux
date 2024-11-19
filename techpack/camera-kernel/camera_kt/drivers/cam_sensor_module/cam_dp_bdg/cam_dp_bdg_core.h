/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _CAMDPBDGCORE_H_
#define _CAMDPBDGCORE_H_

#include <linux/firmware.h>
#include "cam_sensor_dev.h"

#define DP_BDG_SENSOR_ID 0x1901
#define DP_BDG_DP_CONNECTED    0x01
#define DP_BDG_DP_DISCONNECTED 0x00
#define DP_SENSOR_NAME "lt7911uxc"

enum lt7911_fw_status {
	LT7911_UPDATE_SUCCESS = 0,
	LT7911_UPDATE_RUNNING = 1,
	LT7911_UPDATE_FAILED  = 2,
};

/**
 * This API upgrade lt7911 firmware.
 */
int cam_dp_bdg_upgrade_firmware(void);

/**
 * @s_ctrl: Sensor ctrl structure
 *
 * This API set lt7911 camera struct for irq handle.
 */
int cam_dp_bdg_set_cam_ctrl(struct cam_sensor_ctrl_t *s_ctrl);

/**
 * This API unset lt7911 camera struct for irq handle.
 */
void cam_dp_bdg_unset_cam_ctrl(void);

int cam_dp_bdg_get_src_resolution(bool *signal_stable,
		int *width, int *height, int *id);

uint32_t cam_dp_bdg_get_fw_version(void);

/**
 * @brief : API to register DP dev to platform framework.
 * @return struct platform_device pointer on on success, or ERR_PTR() on error.
 */
int dp_bdg_irq_handler_init(void);

/**
 * @brief : API to remove DP dev from platform framework.
 */
void dp_bdg_irq_handler_exit(void);

#endif /* _CAMHDMIBDGCORE_H_ */
