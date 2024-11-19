/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_ACTUATOR_SOC_H_
#define _CAM_ACTUATOR_SOC_H_

#include "cam_actuator_dev.h"

/**
 * @a_ctrl: Actuator ctrl structure
 *
 * This API parses actuator device tree
 */
int cam_actuator_parse_dt(struct cam_actuator_ctrl_t *a_ctrl,
	struct device *dev);

/**
 * @a_ctrl: Actuator ctrl structure
 *
 * This API serves for releasing power domain
 */
void cam_actuator_release_power_domain(struct cam_actuator_ctrl_t *a_ctrl);

#endif /* _CAM_ACTUATOR_SOC_H_ */
