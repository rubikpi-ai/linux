// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>

#include "vpu_iris2.h"
#include "vpu_iris3.h"
#include "iris_core.h"
#include "iris_helpers.h"
#include "vpu_common.h"

int write_register(struct iris_core *core, u32 reg, u32 value)
{
	void __iomem *base_addr;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (!core->power_enabled)
		return -EINVAL;

	base_addr = core->reg_base;
	base_addr += reg;
	writel_relaxed(value, base_addr);

	/* Make sure value is written into the register */
	wmb();

	return ret;
}

int write_register_masked(struct iris_core *core, u32 reg, u32 value, u32 mask)
{
	void __iomem *base_addr;
	u32 prev_val, new_val;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (!core->power_enabled)
		return -EINVAL;

	base_addr = core->reg_base;
	base_addr += reg;

	prev_val = readl_relaxed(base_addr);
	/*
	 * Memory barrier to ensure register read is correct
	 */
	rmb();

	new_val = (prev_val & ~mask) | (value & mask);

	writel_relaxed(new_val, base_addr);
	/*
	 * Memory barrier to make sure value is written into the register.
	 */
	wmb();

	return ret;
}

int read_register(struct iris_core *core, u32 reg, u32 *value)
{
	void __iomem *base_addr;

	if (!core->power_enabled)
		return -EINVAL;

	base_addr = core->reg_base;

	*value = readl_relaxed(base_addr + reg);

	/* Make sure value is read correctly from the register */
	rmb();

	return 0;
}

int read_register_with_poll_timeout(struct iris_core *core, u32 reg,
				    u32 mask, u32 exp_val, u32 sleep_us,
				    u32 timeout_us)
{
	void __iomem *base_addr;
	u32 val = 0;
	int ret;

	if (!core->power_enabled)
		return -EINVAL;

	base_addr = core->reg_base;

	ret = readl_relaxed_poll_timeout(base_addr + reg, val, ((val & mask) == exp_val),
					 sleep_us, timeout_us);
	/*
	 * Memory barrier to make sure value is read correctly from the
	 * register.
	 */
	rmb();

	return ret;
}

int set_preset_registers(struct iris_core *core)
{
	const struct reg_preset_info *reg_prst;
	unsigned int prst_count;
	int cnt, ret = 0;

	reg_prst = core->platform_data->reg_prst_tbl;
	prst_count = core->platform_data->reg_prst_tbl_size;

	if (!reg_prst || !prst_count)
		return 0;

	for (cnt = 0; cnt < prst_count; cnt++) {
		ret = write_register_masked(core, reg_prst[cnt].reg,
					    reg_prst[cnt].value, reg_prst[cnt].mask);
		if (ret)
			return ret;
	}

	return ret;
}

static const struct compat_handle compat_handle[] = {
	{
		.compat                  = "qcom,sm8550-iris",
		.init                    = init_iris3,
	},
	{
		.compat                  = "qcom,qcm6490-iris",
		.init                    = init_iris2,
	},
};

int init_vpu(struct iris_core *core)
{
	struct device *dev = NULL;
	int i, ret = 0;

	dev = core->dev;

	for (i = 0; i < ARRAY_SIZE(compat_handle); i++) {
		if (of_device_is_compatible(dev->of_node, compat_handle[i].compat)) {
			ret = compat_handle[i].init(core);
			if (ret)
				return ret;
			break;
		}
	}

	if (i == ARRAY_SIZE(compat_handle))
		return -EINVAL;

	return ret;
}
