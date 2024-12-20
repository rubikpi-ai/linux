// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/debugfs.h>
#include <linux/videodev2.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/iopoll.h>
#include "cam_io_util.h"
#include "cam_hw.h"
#include "cam_hw_intf.h"
#include "ofe_core.h"
#include "ofe_soc.h"
#include "cam_soc_util.h"
#include "cam_io_util.h"
#include "cam_ofe_hw_intf.h"
#include "cam_icp_hw_intf.h"
#include "cam_icp_hw_mgr_intf.h"
#include "cam_cpas_api.h"
#include "cam_debug_util.h"
#include "hfi_reg.h"
#include "cam_common_util.h"

static int cam_ofe_cpas_vote(struct cam_ofe_device_core_info *core_info,
	struct cam_icp_cpas_vote *cpas_vote)
{
	int rc = 0;

	if (cpas_vote->ahb_vote_valid)
		rc = cam_cpas_update_ahb_vote(core_info->cpas_handle,
			&cpas_vote->ahb_vote);
	if (rc)
		CAM_ERR(CAM_PERF, "CPAS AHB vote failed rc:%d", rc);

	if (cpas_vote->axi_vote_valid)
		rc = cam_cpas_update_axi_vote(core_info->cpas_handle,
			&cpas_vote->axi_vote);
	if (rc)
		CAM_ERR(CAM_PERF, "CPAS AXI vote failed rc:%d", rc);

	return rc;
}

int cam_ofe_init_hw(void *device_priv,
	void *init_hw_args, uint32_t arg_size)
{
	struct cam_hw_info *ofe_dev = device_priv;
	struct cam_hw_soc_info *soc_info = NULL;
	struct cam_ofe_device_core_info *core_info = NULL;
	struct cam_icp_cpas_vote cpas_vote;
	int rc = 0;

	if (!device_priv) {
		CAM_ERR(CAM_ICP, "Invalid cam_dev_info");
		return -EINVAL;
	}

	soc_info = &ofe_dev->soc_info;
	core_info = (struct cam_ofe_device_core_info *)ofe_dev->core_info;

	if ((!soc_info) || (!core_info)) {
		CAM_ERR(CAM_ICP, "soc_info = %pK core_info = %pK",
			soc_info, core_info);
		return -EINVAL;
	}

	cpas_vote.ahb_vote.type = CAM_VOTE_ABSOLUTE;
	cpas_vote.ahb_vote.vote.level = CAM_LOWSVS_D1_VOTE;
	cpas_vote.axi_vote.num_paths = 1;
	cpas_vote.axi_vote.axi_path[0].path_data_type =
		CAM_OFE_DEFAULT_AXI_PATH;
	cpas_vote.axi_vote.axi_path[0].transac_type =
		CAM_OFE_DEFAULT_AXI_TRANSAC;
	cpas_vote.axi_vote.axi_path[0].camnoc_bw =
		CAM_CPAS_DEFAULT_AXI_BW;
	cpas_vote.axi_vote.axi_path[0].mnoc_ab_bw =
		CAM_CPAS_DEFAULT_AXI_BW;
	cpas_vote.axi_vote.axi_path[0].mnoc_ib_bw =
		CAM_CPAS_DEFAULT_AXI_BW;

	rc = cam_cpas_start(core_info->cpas_handle,
		&cpas_vote.ahb_vote, &cpas_vote.axi_vote);
	if (rc) {
		CAM_ERR(CAM_ICP, "cpas start failed: %d", rc);
		return rc;
	}
	core_info->cpas_start = true;

	rc = cam_ofe_enable_soc_resources(soc_info);
	if (rc) {
		CAM_ERR(CAM_ICP, "soc enable failed rc:%d", rc);
		if (cam_cpas_stop(core_info->cpas_handle))
			CAM_ERR(CAM_ICP, "cpas stop failed");
		else
			core_info->cpas_start = false;
	} else {
		core_info->clk_enable = true;
	}

	return rc;
}

int cam_ofe_deinit_hw(void *device_priv,
	void *init_hw_args, uint32_t arg_size)
{
	struct cam_hw_info *ofe_dev = device_priv;
	struct cam_hw_soc_info *soc_info = NULL;
	struct cam_ofe_device_core_info *core_info = NULL;
	int rc = 0;

	if (!device_priv) {
		CAM_ERR(CAM_ICP, "Invalid cam_dev_info");
		return -EINVAL;
	}

	soc_info = &ofe_dev->soc_info;
	core_info = (struct cam_ofe_device_core_info *)ofe_dev->core_info;
	if ((!soc_info) || (!core_info)) {
		CAM_ERR(CAM_ICP, "soc_info = %pK core_info = %pK",
			soc_info, core_info);
		return -EINVAL;
	}

	rc = cam_ofe_disable_soc_resources(soc_info, core_info->clk_enable);
	if (rc)
		CAM_ERR(CAM_ICP, "soc disable failed: %d", rc);
	else
		core_info->clk_enable = false;

	if (core_info->cpas_start) {
		if (cam_cpas_stop(core_info->cpas_handle))
			CAM_ERR(CAM_ICP, "cpas stop failed");
		else
			core_info->cpas_start = false;
	}

	return rc;
}

static int cam_ofe_handle_pc(struct cam_hw_info *ofe_dev)
{
	struct cam_hw_soc_info *soc_info = NULL;
	struct cam_ofe_device_core_info *core_info = NULL;
	struct cam_ofe_device_hw_info *hw_info = NULL;
	int rc = 0, pwr_ctrl, pwr_status;

	soc_info = &ofe_dev->soc_info;
	core_info = (struct cam_ofe_device_core_info *)ofe_dev->core_info;
	hw_info = core_info->ofe_hw_info;

	if (!core_info->cpas_start) {
		CAM_DBG(CAM_ICP, "CPAS OFE client not started");
		return 0;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_ctrl,
		true, &pwr_ctrl);
	if (rc) {
		CAM_ERR(CAM_ICP, "power ctrl read failed rc=%d", rc);
		return rc;
	}

	if (!(pwr_ctrl & OFE_COLLAPSE_MASK)) {
		rc = cam_cpas_reg_read(core_info->cpas_handle,
			CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_status,
			true, &pwr_status);
		if (rc) {
			CAM_ERR(CAM_ICP, "power status read failed rc=%d", rc);
			return rc;
		}

		cam_cpas_reg_write(core_info->cpas_handle,
			CAM_CPAS_REGBASE_CPASTOP,
			hw_info->pwr_ctrl, true, 0x1);

		if ((pwr_status >> OFE_PWR_ON_MASK))
			CAM_WARN(CAM_PERF, "OFE: pwr_status(%x):pwr_ctrl(%x)",
				pwr_status, pwr_ctrl);
	}

	rc = cam_ofe_get_gdsc_control(soc_info);
	if (rc) {
		CAM_ERR(CAM_ICP, "failed to get gdsc control rc=%d", rc);
		return rc;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_ctrl,
		true, &pwr_ctrl);
	if (rc) {
		CAM_ERR(CAM_ICP, "power ctrl read failed rc=%d", rc);
		return rc;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_status,
		true, &pwr_status);
	if (rc) {
		CAM_ERR(CAM_ICP, "power status read failed rc=%d", rc);
		return rc;
	}

	CAM_DBG(CAM_PERF, "pwr_ctrl=%x pwr_status=%x", pwr_ctrl, pwr_status);

	return 0;
}

static int cam_ofe_handle_resume(struct cam_hw_info *ofe_dev)
{
	struct cam_hw_soc_info *soc_info = NULL;
	struct cam_ofe_device_core_info *core_info = NULL;
	struct cam_ofe_device_hw_info *hw_info = NULL;
	int pwr_ctrl, pwr_status, rc = 0;

	soc_info = &ofe_dev->soc_info;
	core_info = (struct cam_ofe_device_core_info *)ofe_dev->core_info;
	hw_info = core_info->ofe_hw_info;

	if (!core_info->cpas_start) {
		CAM_DBG(CAM_ICP, "CPAS OFE client not started");
		return 0;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_ctrl,
		true, &pwr_ctrl);
	if (rc) {
		CAM_ERR(CAM_ICP, "power ctrl read failed rc=%d", rc);
		return rc;
	}

	if (pwr_ctrl & OFE_COLLAPSE_MASK) {
		CAM_DBG(CAM_PERF, "OFE: pwr_ctrl set(%x)", pwr_ctrl);
		cam_cpas_reg_write(core_info->cpas_handle,
			CAM_CPAS_REGBASE_CPASTOP,
			hw_info->pwr_ctrl, true, 0);
	}

	rc = cam_ofe_transfer_gdsc_control(soc_info);
	if (rc) {
		CAM_ERR(CAM_ICP, "failed to transfer gdsc control rc=%d", rc);
		return rc;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_ctrl,
		true, &pwr_ctrl);
	if (rc) {
		CAM_ERR(CAM_ICP, "power ctrl read failed rc=%d", rc);
		return rc;
	}

	rc = cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, hw_info->pwr_status,
		true, &pwr_status);
	if (rc) {
		CAM_ERR(CAM_ICP, "power status read failed rc=%d", rc);
		return rc;
	}

	CAM_DBG(CAM_PERF, "pwr_ctrl=%x pwr_status=%x", pwr_ctrl, pwr_status);

	return rc;
}

static int cam_ofe_cmd_reset(struct cam_hw_soc_info *soc_info,
	struct cam_ofe_device_core_info *core_info)
{
	uint32_t retry_cnt = 0, status = 0;
	int pwr_ctrl, pwr_status, rc = 0;
	bool reset_ofe_cdm_fail = false, reset_ofe_top_fail = false;
	struct cam_ofe_device_hw_info *hw_info = NULL;

	CAM_DBG(CAM_ICP, "CAM_ICP_OFE_CMD_RESET");

	if (!core_info->clk_enable || !core_info->cpas_start) {
		CAM_DBG(CAM_ICP, "OFE not powered on clk_en %d cpas_start %d",
			core_info->clk_enable, core_info->cpas_start);
		return 0;
	}

	hw_info = core_info->ofe_hw_info;

	/* Reset OFE CDM core*/
	cam_io_w_mb(hw_info->cdm_rst_val,
		soc_info->reg_map[0].mem_base + hw_info->cdm_rst_cmd);
	while (retry_cnt < HFI_MAX_POLL_TRY) {
		cam_common_read_poll_timeout((soc_info->reg_map[0].mem_base +
			hw_info->cdm_irq_status),
			PC_POLL_DELAY_US, PC_POLL_TIMEOUT_US,
			OFE_RST_DONE_IRQ_STATUS_BIT, OFE_RST_DONE_IRQ_STATUS_BIT,
			&status);

		CAM_DBG(CAM_ICP, "ofe_cdm_irq_status = %u", status);

		if ((status & OFE_RST_DONE_IRQ_STATUS_BIT) == 0x1)
			break;
		retry_cnt++;
	}

	if (retry_cnt == HFI_MAX_POLL_TRY) {
		CAM_ERR(CAM_ICP, "OFE CDM rst failed status 0x%x", status);
		reset_ofe_cdm_fail = true;
	}

	/* Reset OFE core*/
	status = 0;
	retry_cnt = 0;
	cam_io_w_mb(hw_info->top_rst_val,
		soc_info->reg_map[0].mem_base + hw_info->top_rst_cmd);
	while (retry_cnt < HFI_MAX_POLL_TRY) {
		cam_common_read_poll_timeout((soc_info->reg_map[0].mem_base +
			hw_info->top_irq_status),
			PC_POLL_DELAY_US, PC_POLL_TIMEOUT_US,
			OFE_RST_DONE_IRQ_STATUS_BIT, OFE_RST_DONE_IRQ_STATUS_BIT,
			&status);

		CAM_DBG(CAM_ICP, "ofe_top_irq_status = %u", status);

		if ((status & OFE_RST_DONE_IRQ_STATUS_BIT) == 0x1)
			break;
		retry_cnt++;
	}

	if (retry_cnt == HFI_MAX_POLL_TRY) {
		CAM_ERR(CAM_ICP, "OFE top rst failed status 0x%x", status);
		reset_ofe_top_fail = true;
	}

	cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, core_info->ofe_hw_info->pwr_ctrl,
		true, &pwr_ctrl);
	cam_cpas_reg_read(core_info->cpas_handle,
		CAM_CPAS_REGBASE_CPASTOP, core_info->ofe_hw_info->pwr_status,
		true, &pwr_status);
	CAM_DBG(CAM_ICP, "(After) pwr_ctrl = %x pwr_status = %x",
		pwr_ctrl, pwr_status);

	if (reset_ofe_cdm_fail || reset_ofe_top_fail)
		rc = -EAGAIN;
	else
		CAM_DBG(CAM_ICP, "OFE cdm and OFE top reset success");

	return rc;
}

int cam_ofe_process_cmd(void *device_priv, uint32_t cmd_type,
	void *cmd_args, uint32_t arg_size)
{
	struct cam_hw_info *ofe_dev = device_priv;
	struct cam_hw_soc_info *soc_info = NULL;
	struct cam_ofe_device_core_info *core_info = NULL;
	struct cam_ofe_device_hw_info *hw_info = NULL;
	int rc = 0;

	if (!device_priv) {
		CAM_ERR(CAM_ICP, "Invalid arguments");
		return -EINVAL;
	}

	if (cmd_type >= CAM_ICP_DEV_CMD_MAX) {
		CAM_ERR(CAM_ICP, "Invalid command : %x", cmd_type);
		return -EINVAL;
	}

	soc_info = &ofe_dev->soc_info;
	core_info = (struct cam_ofe_device_core_info *)ofe_dev->core_info;
	hw_info = core_info->ofe_hw_info;

	switch (cmd_type) {
	case CAM_ICP_DEV_CMD_VOTE_CPAS: {
		struct cam_icp_cpas_vote *cpas_vote = cmd_args;

		if (!cmd_args) {
			CAM_ERR(CAM_ICP, "cmd args NULL");
			return -EINVAL;
		}

		cam_ofe_cpas_vote(core_info, cpas_vote);
		break;
	}

	case CAM_ICP_DEV_CMD_CPAS_START: {
		struct cam_icp_cpas_vote *cpas_vote = cmd_args;

		if (!cmd_args) {
			CAM_ERR(CAM_ICP, "cmd args NULL");
			return -EINVAL;
		}

		if (!core_info->cpas_start) {
			rc = cam_cpas_start(core_info->cpas_handle,
				&cpas_vote->ahb_vote,
				&cpas_vote->axi_vote);
			core_info->cpas_start = true;
		}
		break;
	}

	case CAM_ICP_DEV_CMD_CPAS_STOP:
		if (core_info->cpas_start) {
			cam_cpas_stop(core_info->cpas_handle);
			core_info->cpas_start = false;
		}
		break;
	case CAM_ICP_DEV_CMD_POWER_COLLAPSE:
		rc = cam_ofe_handle_pc(ofe_dev);
		break;
	case CAM_ICP_DEV_CMD_POWER_RESUME:
		rc = cam_ofe_handle_resume(ofe_dev);
		break;
	case CAM_ICP_DEV_CMD_UPDATE_CLK: {
		struct cam_icp_dev_clk_update_cmd *clk_upd_cmd = cmd_args;
		struct cam_ahb_vote ahb_vote;
		uint32_t clk_rate = clk_upd_cmd->curr_clk_rate;
		int32_t clk_level = 0, err = 0;

		CAM_DBG(CAM_PERF, "ofe_src_clk rate = %d", (int)clk_rate);

		if (!core_info->clk_enable) {
			if (clk_upd_cmd->dev_pc_enable) {
				cam_ofe_handle_pc(ofe_dev);
				cam_cpas_reg_write(core_info->cpas_handle,
					CAM_CPAS_REGBASE_CPASTOP,
					hw_info->pwr_ctrl, true, 0x0);
			}
			rc = cam_ofe_toggle_clk(soc_info, true);
			if (rc)
				CAM_ERR(CAM_ICP, "Enable failed");
			else
				core_info->clk_enable = true;
			if (clk_upd_cmd->dev_pc_enable) {
				rc = cam_ofe_handle_resume(ofe_dev);
				if (rc)
					CAM_ERR(CAM_ICP, "OFE resume failed");
			}
		}
		CAM_DBG(CAM_PERF, "clock rate %d", clk_rate);
		rc = cam_ofe_update_clk_rate(soc_info, clk_rate);
		if (rc)
			CAM_ERR(CAM_PERF, "Failed to update clk %d", clk_rate);

		err = cam_soc_util_get_clk_level(soc_info,
			clk_rate, soc_info->src_clk_idx,
			&clk_level);

		if (!err) {
			clk_upd_cmd->clk_level = clk_level;
			ahb_vote.type = CAM_VOTE_ABSOLUTE;
			ahb_vote.vote.level = clk_level;
			cam_cpas_update_ahb_vote(
				core_info->cpas_handle,
				&ahb_vote);
		}
		break;
	}
	case CAM_ICP_DEV_CMD_DISABLE_CLK:
		if (core_info->clk_enable)
			cam_ofe_toggle_clk(soc_info, false);
		core_info->clk_enable = false;
		break;
	case CAM_ICP_DEV_CMD_RESET:
		rc = cam_ofe_cmd_reset(soc_info, core_info);
		break;
	default:
		CAM_ERR(CAM_ICP, "Invalid Cmd Type:%u", cmd_type);
		rc = -EINVAL;
		break;
	}
	return rc;
}

irqreturn_t cam_ofe_irq(int irq_num, void *data)
{
	return IRQ_HANDLED;
}
