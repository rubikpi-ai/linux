/* Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pwm.h>
#include "cam_ir_led_soc.h"
#include "cam_res_mgr_api.h"
#include "linux/i2c.h"

int cam_ir_led_get_dt_data(struct cam_ir_led_ctrl *ictrl,
	struct cam_hw_soc_info *soc_info)
{
	int32_t rc = 0;

	if (!ictrl) {
		CAM_ERR(CAM_IR_LED, "NULL ir_led control structure");
		return -EINVAL;
	}

	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_IR_LED, "get_dt_properties failed rc %d", rc);
		return rc;
	}

	soc_info->soc_private =
		kzalloc(sizeof(struct cam_ir_led_private_soc), GFP_KERNEL);
	if (!soc_info->soc_private) {
		CAM_ERR(CAM_IR_LED, "soc_info->soc_private is NULL");
		rc = -ENOMEM;
		goto release_soc_res;
	}

	if (of_property_read_bool(soc_info->dev->of_node, "pwms")) {
		ictrl->pwm_dev = pwm_get(soc_info->dev, NULL);
		if (ictrl->pwm_dev == NULL)
			CAM_ERR(CAM_IR_LED, "Cannot get PWM device");
		ictrl->ir_led_driver_type = IR_LED_DRIVER_PMIC;
	} else if (of_property_read_bool(soc_info->dev->of_node, "i2c")) {
		struct device_node *PCA9632 = of_parse_phandle(ictrl->pdev->dev.of_node, "i2c", 0);
		if (PCA9632 == NULL)
			CAM_ERR(CAM_IR_LED, "Cannot get PCA9632 device");
		else {
			ictrl->io_master_info.client =
				of_find_i2c_device_by_node(PCA9632);

			if (ictrl->io_master_info.client != NULL) {
				ictrl->io_master_info.master_type = I2C_MASTER;
				ictrl->ir_led_driver_type = IR_LED_DRIVER_I2C;
			} else {
				CAM_ERR(CAM_IR_LED, "Cannot get PCA9632 I2C device");
			}

			of_node_put(PCA9632);
		}
	} else {
		ictrl->ir_led_driver_type = IR_LED_DRIVER_GPIO;
	}

	return rc;

release_soc_res:
	cam_soc_util_release_platform_resource(soc_info);
	return rc;
}
