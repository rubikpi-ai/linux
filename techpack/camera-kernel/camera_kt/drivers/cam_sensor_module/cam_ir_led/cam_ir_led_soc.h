/* Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CAM_IR_LED_SOC_H_
#define _CAM_IR_LED_SOC_H_

#include "cam_ir_led_dev.h"

int cam_ir_led_get_dt_data(struct cam_ir_led_ctrl *fctrl,
	struct cam_hw_soc_info *soc_info);

#endif /*_CAM_IR_LED_SOC_H_*/
