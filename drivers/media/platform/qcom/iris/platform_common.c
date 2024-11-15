// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "iris_core.h"
#include "platform_common.h"

int init_platform(struct iris_core *core)
{
	struct platform_data *platform = NULL;

	platform = devm_kzalloc(core->dev, sizeof(*platform),
				GFP_KERNEL);
	if (!platform)
		return -ENOMEM;

	core->platform_data = platform;

	core->platform_data = (struct platform_data *)of_device_get_match_data(core->dev);
	if (!core->platform_data)
		return -ENODEV;

	return 0;
}
