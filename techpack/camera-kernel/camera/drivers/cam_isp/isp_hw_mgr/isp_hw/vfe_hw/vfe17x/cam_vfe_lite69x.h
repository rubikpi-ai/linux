/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_VFE_LITE69X_H_
#define _CAM_VFE_LITE69X_H_
#include "cam_vfe_camif_ver3.h"
#include "cam_vfe_top_ver4.h"
#include "cam_vfe_core.h"
#include "cam_vfe_bus_ver3.h"
#include "cam_irq_controller.h"

#define CAM_VFE_69X_NUM_DBG_REG 4

/* Offsets might not match due to csid secure regs at beginning of reg space */

static struct cam_vfe_top_ver4_wr_client_desc vfe690x_wr_client_desc[] = {
	{
		.wm_id = 0,
		.desc = "RDI_0",
	},
	{
		.wm_id = 1,
		.desc = "RDI_1",
	},
	{
		.wm_id = 2,
		.desc = "RDI_2",
	},
	{
		.wm_id = 3,
		.desc = "RDI_3",
	},
	{
		.wm_id = 4,
		.desc = "RDI_4",
	},
	{
		.wm_id = 5,
		.desc = "RDI_5",
	},
};

static struct cam_irq_register_set vfe69x_top_irq_reg_set[3] = {
	{
		.mask_reg_offset   = 0x00001068,
		.clear_reg_offset  = 0x00001070,
		.status_reg_offset = 0x00001078,
		.test_set_val      = BIT(0),
		.test_sub_val      = BIT(0),
	},
	{
		.mask_reg_offset   = 0x0000106C,
		.clear_reg_offset  = 0x00001074,
		.status_reg_offset = 0x0000107C,
	},
};

static struct cam_irq_controller_reg_info vfe69x_top_irq_reg_info = {
	.num_registers = 2,
	.irq_reg_set = vfe69x_top_irq_reg_set,
	.global_irq_cmd_offset = 0x00001064,
	.global_clear_bitmask  = 0x00000001,
	.global_set_bitmask    = 0x00000010,
	.clear_all_bitmask     = 0xFFFFFFFF,
};

static uint32_t vfe69x_top_debug_reg[] = {
	0x000010E4,
	0x000010E8,
	0x000010EC,
	0x000010F0,
};

static struct cam_vfe_top_ver4_reg_offset_common vfe69x_top_common_reg = {
	.hw_version               = 0x00001000,
	.hw_capability            = 0x00001004,
	.diag_config              = 0x00001010,
	.diag_sensor_status_0     = 0x00001014,
	.diag_sensor_status_1     = 0x00001018,
	.bus_violation_status     = 0x000014E4,
	.bus_overflow_status      = 0x000014E8,
	.top_debug_cfg            = 0x000010FC,
	.num_top_debug_reg        = CAM_VFE_69X_NUM_DBG_REG,
	.top_debug                = vfe69x_top_debug_reg,
	.frame_timing_irq_reg_idx = CAM_IFE_IRQ_CAMIF_REG_STATUS1,
};

static struct cam_vfe_ver4_path_reg_data vfe69x_rdi_reg_data[6] = {

	{
		.sof_irq_mask                    = 0x1,
		.eof_irq_mask                    = 0x2,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
	{
		.sof_irq_mask                    = 0x4,
		.eof_irq_mask                    = 0x8,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
	{
		.sof_irq_mask                    = 0x10,
		.eof_irq_mask                    = 0x20,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
	{
		.sof_irq_mask                    = 0x40,
		.eof_irq_mask                    = 0x80,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
	{
		.sof_irq_mask                    = 0x100,
		.eof_irq_mask                    = 0x200,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
	{
		.sof_irq_mask                    = 0x400,
		.eof_irq_mask                    = 0x800,
		.error_irq_mask                  = 0x0,
		.enable_diagnostic_hw            = 0x1,
		.top_debug_cfg_en                = 0x3,
	},
};

static struct cam_vfe_ver4_path_hw_info
	vfe69x_rdi_hw_info[] = {
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[0],
	},
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[1],
	},
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[2],
	},
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[3],
	},
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[4],
	},
	{
		.common_reg     = &vfe69x_top_common_reg,
		.reg_data       = &vfe69x_rdi_reg_data[5],
	},
};

static struct cam_vfe_top_ver4_debug_reg_info vfe69x_dbg_reg_info[CAM_VFE_69X_NUM_DBG_REG][8] = {
	VFE_DBG_INFO_ARRAY_4bit(
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved"
	),
	{
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
		VFE_DBG_INFO(32, "non-CLC info"),
	},
	VFE_DBG_INFO_ARRAY_4bit(
		"csid_ccif_rdi0_vld_out",
		"csid_ccif_rdi1_vld_out",
		"csid_ccif_rdi3_vld_out",
		"csid_ccif_rdi4_vld_out",
		"csid_ccif_rdi5_vld_out",
		"unused",
		"unused",
		"unused"
	),
	VFE_DBG_INFO_ARRAY_4bit(
		"unused",
		"unused",
		"unused",
		"unused",
		"unused",
		"unused",
		"unused",
		"unused"
	),
};

static struct cam_vfe_top_ver4_hw_info vfe69x_top_hw_info = {
	.common_reg = &vfe69x_top_common_reg,
	.rdi_hw_info =   vfe69x_rdi_hw_info,
	.vfe_full_hw_info = {
		.common_reg     = &vfe69x_top_common_reg,
	},
	.wr_client_desc         = vfe690x_wr_client_desc,
	.num_mux = 5,
	.mux_type = {
		CAM_VFE_CAMIF_VER_4_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
		CAM_VFE_RDI_VER_1_0,
	},
	.num_rdi        = ARRAY_SIZE(vfe69x_rdi_hw_info),
	.debug_reg_info = &vfe69x_dbg_reg_info,
};

static struct cam_irq_register_set vfe690x_bus_irq_reg[1] = {
		{
			.mask_reg_offset   = 0x00001498,
			.clear_reg_offset  = 0x000014A0,
			.status_reg_offset = 0x000014A8,
		},
};

static uint32_t vfe690x_out_port_mid[][4] = {
	{16, 0, 0, 0},
	{17, 0, 0, 0},
	{18, 0, 0, 0},
	{19, 0, 0, 0},
	{20, 0, 0, 0},
	{21, 0, 0, 0},
};

static struct cam_vfe_bus_ver3_hw_info vfe690x_bus_hw_info = {
	.common_reg = {
		.hw_version                       = 0x00001400,
		.cgc_ovd                          = 0x00001408,
		.if_frameheader_cfg               = {
			0x000014B4,
			0x000014B8,
			0x000014BC,
			0x000014C0,
			0x000014C4,
			0x000014C8,
		},
		.pwr_iso_cfg                      = 0x000014DC,
		.overflow_status_clear            = 0x000014E0,
		.ccif_violation_status            = 0x000014E4,
		.overflow_status                  = 0x000014E8,
		.image_size_violation_status      = 0x000014F0,
		.debug_status_top_cfg             = 0x00001570,
		.debug_status_top                 = 0x00001574,
		.test_bus_ctrl                    = 0x0000157C,
		.irq_reg_info = {
			.num_registers            = 1,
			.irq_reg_set              = vfe680x_bus_irq_reg,
			.global_irq_cmd_offset    = 0x000014B0,
			.global_clear_bitmask     = 0x00000001,
		},
	},
	.num_client = 6,
	.bus_client_reg = {
		/* BUS Client 0 RDI0 */
		{
			.cfg                      = 0x00001680,
			.image_addr               = 0x00001684,
			.frame_incr               = 0x00001688,
			.image_cfg_0              = 0x0000168C,
			.image_cfg_1              = 0x00001690,
			.image_cfg_2              = 0x00001694,
			.packer_cfg               = 0x00001698,
			.frame_header_addr        = 0x000016A0,
			.frame_header_incr        = 0x000016A4,
			.frame_header_cfg         = 0x000016A8,
			.irq_subsample_period     = 0x000016B0,
			.irq_subsample_pattern    = 0x000016B4,
			.mmu_prefetch_cfg         = 0x000016E0,
			.mmu_prefetch_max_offset  = 0x000016E4,
			.system_cache_cfg         = 0x000016E8,
			.addr_status_0            = 0x000016F0,
			.addr_status_1            = 0x000016F4,
			.addr_status_2            = 0x000016F8,
			.addr_status_3            = 0x000016FC,
			.debug_status_cfg         = 0x00001700,
			.debug_status_0           = 0x00001704,
			.debug_status_1           = 0x00001708,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_0,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 1 RDI1 */
		{
			.cfg                      = 0x00001780,
			.image_addr               = 0x00001784,
			.frame_incr               = 0x00001788,
			.image_cfg_0              = 0x0000178C,
			.image_cfg_1              = 0x00001790,
			.image_cfg_2              = 0x00001794,
			.packer_cfg               = 0x00001798,
			.frame_header_addr        = 0x000017A0,
			.frame_header_incr        = 0x000017A4,
			.frame_header_cfg         = 0x000017A8,
			.irq_subsample_period     = 0x000017B0,
			.irq_subsample_pattern    = 0x000017B4,
			.mmu_prefetch_cfg         = 0x000017E0,
			.mmu_prefetch_max_offset  = 0x000017E4,
			.system_cache_cfg         = 0x000017E8,
			.addr_status_0            = 0x000017F0,
			.addr_status_1            = 0x000017F4,
			.addr_status_2            = 0x000017F8,
			.addr_status_3            = 0x000017FC,
			.debug_status_cfg         = 0x00001800,
			.debug_status_0           = 0x00001804,
			.debug_status_1           = 0x00001808,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_1,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 2 RDI2 */
		{
			.cfg                      = 0x00001880,
			.image_addr               = 0x00001884,
			.frame_incr               = 0x00001888,
			.image_cfg_0              = 0x0000188C,
			.image_cfg_1              = 0x00001890,
			.image_cfg_2              = 0x00001894,
			.packer_cfg               = 0x00001898,
			.frame_header_addr        = 0x000018A0,
			.frame_header_incr        = 0x000018A4,
			.frame_header_cfg         = 0x000018A8,
			.irq_subsample_period     = 0x000018B0,
			.irq_subsample_pattern    = 0x000018B4,
			.mmu_prefetch_cfg         = 0x000018E0,
			.mmu_prefetch_max_offset  = 0x000018E4,
			.system_cache_cfg         = 0x000018E8,
			.addr_status_0            = 0x000018F0,
			.addr_status_1            = 0x000018F4,
			.addr_status_2            = 0x000018F8,
			.addr_status_3            = 0x000018FC,
			.debug_status_cfg         = 0x00001900,
			.debug_status_0           = 0x00001904,
			.debug_status_1           = 0x00001908,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_2,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 3 RDI3 */
		{
			.cfg                      = 0x00001980,
			.image_addr               = 0x00001984,
			.frame_incr               = 0x00001988,
			.image_cfg_0              = 0x0000198C,
			.image_cfg_1              = 0x00001990,
			.image_cfg_2              = 0x00001994,
			.packer_cfg               = 0x00001998,
			.frame_header_addr        = 0x000019A0,
			.frame_header_incr        = 0x000019A4,
			.frame_header_cfg         = 0x000019A8,
			.irq_subsample_period     = 0x000019B0,
			.irq_subsample_pattern    = 0x000019B4,
			.mmu_prefetch_cfg         = 0x000019E0,
			.mmu_prefetch_max_offset  = 0x000019E4,
			.system_cache_cfg         = 0x000019E8,
			.addr_status_0            = 0x000019F0,
			.addr_status_1            = 0x000019F4,
			.addr_status_2            = 0x000019F8,
			.addr_status_3            = 0x000019FC,
			.debug_status_cfg         = 0x00001A00,
			.debug_status_0           = 0x00001A04,
			.debug_status_1           = 0x00001A08,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_3,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 4 RDI4 */
		{
			.cfg                      = 0x00001A80,
			.image_addr               = 0x00001A84,
			.frame_incr               = 0x00001A88,
			.image_cfg_0              = 0x00001A8C,
			.image_cfg_1              = 0x00001A90,
			.image_cfg_2              = 0x00001A94,
			.packer_cfg               = 0x00001A98,
			.frame_header_addr        = 0x00001AA0,
			.frame_header_incr        = 0x00001AA4,
			.frame_header_cfg         = 0x00001AA8,
			.irq_subsample_period     = 0x00001AB0,
			.irq_subsample_pattern    = 0x00001AB4,
			.mmu_prefetch_cfg         = 0x00001AE0,
			.mmu_prefetch_max_offset  = 0x00001AE4,
			.system_cache_cfg         = 0x00001AE8,
			.addr_status_0            = 0x00001AF0,
			.addr_status_1            = 0x00001AF4,
			.addr_status_2            = 0x00001AF8,
			.addr_status_3            = 0x00001AFC,
			.debug_status_cfg         = 0x00001B00,
			.debug_status_0           = 0x00001B04,
			.debug_status_1           = 0x00001B08,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_4,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 5 RDI5 */
		{
			.cfg                      = 0x00001B80,
			.image_addr               = 0x00001B84,
			.frame_incr               = 0x00001B88,
			.image_cfg_0              = 0x00001B8C,
			.image_cfg_1              = 0x00001B90,
			.image_cfg_2              = 0x00001B94,
			.packer_cfg               = 0x00001B98,
			.frame_header_addr        = 0x00001BA0,
			.frame_header_incr        = 0x00001BA4,
			.frame_header_cfg         = 0x00001BA8,
			.irq_subsample_period     = 0x00001BB0,
			.irq_subsample_pattern    = 0x00001BB4,
			.mmu_prefetch_cfg         = 0x00001BE0,
			.mmu_prefetch_max_offset  = 0x00001BE4,
			.system_cache_cfg         = 0x00001BE8,
			.addr_status_0            = 0x00001BF0,
			.addr_status_1            = 0x00001BF4,
			.addr_status_2            = 0x00001BF8,
			.addr_status_3            = 0x00001BFC,
			.debug_status_cfg         = 0x00001C00,
			.debug_status_0           = 0x00001C04,
			.debug_status_1           = 0x00001C08,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_5,
			.ubwc_regs                = NULL,
		},
	},
	.num_out = 6,
	.vfe_out_hw_info = {
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI0,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_1,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[0],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				0,
			},
			.name          = {
				"LITE_0",
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI1,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_2,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[1],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				1,
			},
			.name          = {
				"LITE_1",
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI2,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_3,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[2],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				2,
			},
			.name          = {
				"LITE_2",
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI3,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_4,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[3],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				3,
			},
			.name          = {
				"LITE_3",
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI4,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_5,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[3],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				4,
			},
			.name          = {
				"LITE_4",
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI5,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_6,
			.num_wm        = 1,
			.mid           = vfe690x_out_port_mid[3],
			.num_mid       = 1,
			.line_based    = 1,
			.wm_idx        = {
				5,
			},
			.name          = {
				"LITE_5",
			},
		},
	},
	.num_comp_grp    = 6,
	.support_consumed_addr = true,
	.comp_done_mask = {
		BIT(0), BIT(1), BIT(2), BIT(3), BIT(4), BIT(6)
	},
	.top_irq_shift   = 0,
	.max_out_res = CAM_ISP_IFE_OUT_RES_BASE + 33,
};

static struct cam_vfe_irq_hw_info vfe69x_irq_hw_info = {
	.reset_mask    = 0,
	.supported_irq = CAM_VFE_HW_IRQ_CAP_LITE_EXT_CSID,
	.top_irq_reg   = &vfe69x_top_irq_reg_info,
};

static struct cam_vfe_hw_info cam_vfe_lite69x_hw_info = {
	.irq_hw_info                   = &vfe69x_irq_hw_info,

	.bus_version                   = CAM_VFE_BUS_VER_3_0,
	.bus_hw_info                   = &vfe690x_bus_hw_info,

	.top_version                   = CAM_VFE_TOP_VER_4_0,
	.top_hw_info                   = &vfe69x_top_hw_info,
};

#endif /* _CAM_VFE_LITE69X_H_ */
