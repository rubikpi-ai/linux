// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/of_address.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "firmware.h"
#include "iris_core.h"

#define MAX_FIRMWARE_NAME_SIZE	128

struct tzbsp_memprot {
	u32 cp_start;
	u32 cp_size;
	u32 cp_nonpixel_start;
	u32 cp_nonpixel_size;
};

static int __protect_cp_mem(struct iris_core *core)
{
	struct tzbsp_memprot memprot;
	int ret;

	memprot.cp_start = core->cap[CP_START].value;
	memprot.cp_size = core->cap[CP_SIZE].value;
	memprot.cp_nonpixel_start = core->cap[CP_NONPIXEL_START].value;
	memprot.cp_nonpixel_size = core->cap[CP_NONPIXEL_SIZE].value;

	ret = qcom_scm_mem_protect_video_var(memprot.cp_start,
					     memprot.cp_size,
					     memprot.cp_nonpixel_start,
					     memprot.cp_nonpixel_size);
	if (ret)
		dev_err(core->dev, "Failed to protect memory(%d)\n", ret);

	return ret;
}

static int __load_fw_to_memory(struct iris_core *core,
			       const char *fw_name)
{
	char firmware_name[MAX_FIRMWARE_NAME_SIZE] = { 0 };
	const struct firmware *firmware = NULL;
	struct device_node *node = NULL;
	struct resource res = { 0 };
	phys_addr_t mem_phys = 0;
	void *mem_virt = NULL;
	size_t res_size = 0;
	ssize_t fw_size = 0;
	struct device *dev;
	int pas_id = 0;
	int ret;

	if (!fw_name || !(*fw_name) || !core)
		return -EINVAL;

	dev = core->dev;

	if (strlen(fw_name) >= MAX_FIRMWARE_NAME_SIZE - 4)
		return -EINVAL;

	scnprintf(firmware_name, ARRAY_SIZE(firmware_name), "%s.mbn", fw_name);

	pas_id = core->platform_data->pas_id;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node)
		return -EINVAL;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		goto err_put_node;

	mem_phys = res.start;
	res_size = (size_t)resource_size(&res);

	ret = request_firmware(&firmware, firmware_name, dev);
	if (ret) {
		dev_err(core->dev, "%s: failed to request fw \"%s\", error %d\n",
			__func__, firmware_name, ret);
		goto err_put_node;
	}

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || res_size < (size_t)fw_size) {
		ret = -EINVAL;
		dev_err(core->dev, "%s: out of bound fw image fw size: %ld, res_size: %lu\n",
			__func__, fw_size, res_size);
		goto err_release_fw;
	}

	mem_virt = memremap(mem_phys, res_size, MEMREMAP_WC);
	if (!mem_virt) {
		dev_err(core->dev, "%s: failed to remap fw memory phys %pa[p]\n",
			__func__, &mem_phys);
		goto err_release_fw;
	}

	ret = qcom_mdt_load(dev, firmware, firmware_name,
			    pas_id, mem_virt, mem_phys, res_size, NULL);
	if (ret) {
		dev_err(core->dev, "%s: error %d loading fw \"%s\"\n",
			__func__, ret, firmware_name);
		goto err_mem_unmap;
	}
	ret = qcom_scm_pas_auth_and_reset(pas_id);
	if (ret) {
		dev_err(core->dev, "%s: error %d authenticating fw \"%s\"\n",
			__func__, ret, firmware_name);
		goto err_mem_unmap;
	}

	return ret;

err_mem_unmap:
	memunmap(mem_virt);
err_release_fw:
	release_firmware(firmware);
err_put_node:
	of_node_put(node);
	return ret;
}

int iris_fw_load(struct iris_core *core)
{
	int ret;

	ret = __load_fw_to_memory(core, core->platform_data->fwname);
	if (ret) {
		dev_err(core->dev, "%s: firmware download failed\n", __func__);
		return -ENOMEM;
	}

	ret = __protect_cp_mem(core);
	if (ret) {
		dev_err(core->dev, "%s: protect memory failed\n", __func__);
		qcom_scm_pas_shutdown(core->platform_data->pas_id);
		return ret;
	}

	return ret;
}

int iris_fw_unload(struct iris_core *core)
{
	int ret;

	ret = qcom_scm_pas_shutdown(core->platform_data->pas_id);
	if (ret)
		dev_err(core->dev, "%s: Firmware unload failed with ret %d\n", __func__, ret);

	return ret;
}

int iris_set_hw_state(struct iris_core *core, bool resume)
{
	return qcom_scm_set_remote_state(resume, 0);
}
