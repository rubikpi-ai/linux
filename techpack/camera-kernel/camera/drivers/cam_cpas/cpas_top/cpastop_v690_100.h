/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CPASTOP_V690_100_H_
#define _CPASTOP_V690_100_H_

#include "cam_cpas_hw.h"

static struct cam_camnoc_irq_sbm cam_cpas_v690_100_irq_sbm = {
	.sbm_enable = {
		.access_type = CAM_REG_TYPE_READ_WRITE,
		.enable = true,
		.offset = 0x240, /* CAM_NOC_SBM_FAULTINEN0_LOW */
		.value = 0x04 |    /* SBM_FAULTINEN0_LOW_PORT2_MASK */
			0x10 |    /* SBM_FAULTINEN0_LOW_PORT4_MASK */
			0x20,     /* SBM_FAULTINEN0_LOW_PORT5_MASK */
	},
	.sbm_status = {
		.access_type = CAM_REG_TYPE_READ,
		.enable = true,
		.offset = 0x248, /* CAM_NOC_SBM_FAULTINSTATUS0_LOW */
	},
	.sbm_clear = {
		.access_type = CAM_REG_TYPE_WRITE,
		.enable = true,
		.offset = 0x280, /* CAM_NOC_SBM_FLAGOUTCLR0_LOW */
		.value = 0x1,
	}
};

static struct cam_camnoc_irq_err
	cam_cpas_v690_100_irq_err[] = {
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_SLAVE_ERROR,
		.enable = true,
		.sbm_port = 0x1, /* SBM_FAULTINSTATUS0_LOW_PORT0_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x8, /* CAM_NOC_ERL_MAINCTL_LOW */
			.value = 1,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x10, /* CAM_NOC_ERL_ERRVLD_LOW */
		},
		.err_clear = {
			.access_type = CAM_REG_TYPE_WRITE,
			.enable = true,
			.offset = 0x18, /* CAM_NOC_ERL_ERRCLR_LOW */
			.value = 1,
		},
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_IFE_UBWC_ENCODE_ERROR,
		.enable = true,
		.sbm_port = 0x20, /* SBM_FAULTINSTATUS0_LOW_PORT5_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x67A0, /* CAM_NOC_RT_2_NIU_ENCERREN_LOW */
			.value = 0xF,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x6790, /* CAM_NOC_RT_2_NIU_ENCERRSTATUS_LOW */
		},
		.err_clear = {
			.access_type = CAM_REG_TYPE_WRITE,
			.enable = true,
			.offset = 0x6798, /* CAM_NOC_RT_2_NIU_ENCERRCLR_LOW */
			.value = 0X1,
		},
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_IPE0_UBWC_DECODE_ERROR,
		.enable = true,
		.sbm_port = 0x4, /* SBM_FAULTINSTATUS0_LOW_PORT2_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x5F20, /* CAM_NOC_NRT_2_NIU_DECERREN_LOW */
			.value = 0xFF,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x5F10, /* CAM_NOC_NRT_1_NIU_DECERRSTATUS_LOW */
		},
		.err_clear = {
			.access_type = CAM_REG_TYPE_WRITE,
			.enable = true,
			.offset = 0x5F18, /* CAM_NOC_NRT_1_NIU_DECERRCLR_LOW */
			.value = 0X1,
		},
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_IPE_UBWC_ENCODE_ERROR,
		.enable = true,
		.sbm_port = 0x10, /* SBM_FAULTINSTATUS0_LOW_PORT4_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x61A0, /* CAM_NOC_NRT_2_NIU_ENCERREN_LOW */
			.value = 0XF,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x6190, /* CAM_NOC_NRT_2_NIU_ENCERRSTATUS_LOW */
		},
		.err_clear = {
			.access_type = CAM_REG_TYPE_WRITE,
			.enable = true,
			.offset = 0x6198, /* CAM_NOC_NRT_2_NIU_ENCERRCLR_LOW */
			.value = 0x1,
		},
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_AHB_TIMEOUT,
		.enable = false,
		.sbm_port = 0x40, /* SBM_FAULTINSTATUS0_LOW_PORT6_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x288, /* CAM_NOC_SBM_FLAGOUTSET0_LOW */
			.value = 0x1,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x290, /* CAM_NOC_SBM_FLAGOUTSTATUS0_LOW */
		},
		.err_clear = {
			.enable = false, /* CAM_NOC_SBM_FLAGOUTCLR0_LOW */
		},
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_RESERVED1,
		.enable = false,
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_RESERVED2,
		.enable = false,
	},
	{
		.irq_type = CAM_CAMNOC_HW_IRQ_CAMNOC_TEST,
		.enable = false,
		.sbm_port = 0x80, /* SBM_FAULTINSTATUS0_LOW_PORT7_MASK */
		.err_enable = {
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.enable = true,
			.offset = 0x288, /* CAM_NOC_SBM_FLAGOUTSET0_LOW */
			.value = 0x5,
		},
		.err_status = {
			.access_type = CAM_REG_TYPE_READ,
			.enable = true,
			.offset = 0x290, /* CAM_NOC_SBM_FLAGOUTSTATUS0_LOW */
		},
		.err_clear = {
			.enable = false, /* CAM_NOC_SBM_FLAGOUTCLR0_LOW */
		},
	},
};

static struct cam_camnoc_specific
	cam_cpas_v690_100_camnoc_specific[] = {
	{
		//RT_0
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "RT_0",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6230, /* CAMNOC_CAM_NOC_RT_0_NIU_PRIORITYLUT_LOW */
			.value = 0x0,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6234, /* CAMNOC_CAM_NOC_RT_0_NIU_PRIORITYLUT_HIGH */
			.value = 0x0,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6238, /* CAMNOC_CAM_NOC_RT_0_NIU_URGENCY_LOW */
			.value = 0x1003,
		},
		.danger_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6240, /* CAMNOC_CAM_NOC_RT_0_NIU_DANGERLUT_LOW */
			.value = 0x0,
		},
		.safe_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6248, /* CAMNOC_CAM_NOC_RT_0_NIU_SAFELUT_LOW */
			.value = 0x0000FFFF,
		},
	},
	{
		//RT_1
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "RT_1",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6430, /* CAMNOC_CAM_NOC_RT_1_NIU_PRIORITYLUT_LOW */
			.value = 0x66665433,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6434, /* CAMNOC_CAM_NOC_RT_1_NIU_PRIORITYLUT_HIGH */
			.value = 0x66666666,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6438, /* CAMNOC_CAM_NOC_RT_1_NIU_URGENCY_LOW */
			.value = 0x1B30,
		},
		.danger_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6440, /* CAMNOC_CAM_NOC_RT_1_NIU_DANGERLUT_LOW */
			.value = 0xFFFFFF00,
		},
		.safe_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6448, /* CAMNOC_CAM_NOC_RT_1_NIU_SAFELUT_LOW */
			.value = 0x000F,
		},
	},
	{
		//RT_2
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "RT_2",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6630, /* CAMNOC_CAM_NOC_RT_2_NIU_PRIORITYLUT_LOW */
			.value = 0x66665433,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6634, /* CAMNOC_CAM_NOC_RT_2_NIU_PRIORITYLUT_HIGH */
			.value = 0x66666666,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6638, /* CAMNOC_CAM_NOC_RT_2_NIU_URGENCY_LOW */
			.value = 0x1B30,
		},
		.danger_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6640, /* CAMNOC_CAM_NOC_RT_2_NIU_DANGERLUT_LOW */
			.value = 0xFFFFFF00,
		},
		.safe_lut = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6648, /* CAMNOC_CAM_NOC_RT_2_NIU_SAFELUT_LOW */
			.value = 0x000F,
		},
	},
	{
		//NRT_0
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "NRT_0",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5C30, /* CAMNOC_CAM_NOC_NRT_0_NIU_PRIORITYLUT_LOW */
			.value = 0x0,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5C34, /* CAMNOC_CAM_NOC_NRT_0_NIU_PRIORITYLUT_HIGH */
			.value = 0x0,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5C38, /* CAMNOC_CAM_NOC_NRT_0_NIU_URGENCY_LOW */
			.value = 0x3,
		},
	},
	{
		//NRT_1
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "NRT_1",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5E30, /* CAMNOC_CAM_NOC_NRT_1_NIU_PRIORITYLUT_LOW */
			.value = 0x0,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5E34, /* CAMNOC_CAM_NOC_NRT_1_NIU_PRIORITYLUT_HIGH */
			.value = 0x0,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5E38, /* CAMNOC_CAM_NOC_NRT_1_NIU_URGENCY_LOW */
			.value = 0x3,
		},
	},
	{
		//NRT_2
		.port_type = CAM_CAMNOC_IFE_UBWC,
		.port_name = "NRT_2",
		.enable = true,
		.priority_lut_low = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6030, /* CAMNOC_CAM_NOC_NRT_2_NIU_PRIORITYLUT_LOW */
			.value = 0x33333333,
		},
		.priority_lut_high = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6034, /* CAMNOC_CAM_NOC_NRT_2_NIU_PRIORITYLUT_HIGH */
			.value = 0x33333333,
		},
		.urgency = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6038, /* CAMNOC_CAM_NOC_NRT_2_NIU_URGENCY_LOW */
			.value = 0x30,
		},
	},
	{
		.port_type = CAM_CAMNOC_ICP,
		.port_name = "ICP",
		.enable = true,
		.qosgen_mainctl = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x5308, /* CAMNOC_CAM_NOC_XM_ICP_QOSGEN_MAINCTL_LOW */
			.value = 0x50,
		},
		.dynattr_mainctl = {
			.enable = true,
			.access_type = CAM_REG_TYPE_READ_WRITE,
			.masked_value = 0x0,
			.offset = 0x6B08, /* CAMNOC_CAM_NOC_XM_ICP_DYNATTR_MAINCTL_LOW */
			.value = 0x10,
		},
	},
};

static struct cam_camnoc_err_logger_info cam690_cpas100_err_logger_offsets = {
	.mainctrl     =  0x0008, /* CAMNOC_CAM_NOC_ERL_MAINCTL_LOW */
	.errvld       =  0x0010, /* CAMNOC_CAM_NOC_ERl_ERRVLD_LOW */
	.errlog0_low  =  0x0020, /* CAMNOC_CAM_NOC_ERL_ERRLOG0_LOW */
	.errlog0_high =  0x0024, /* CAMNOC_CAM_NOC_ERL_ERRLOG0_HIGH */
	.errlog1_low  =  0x0028, /* CAMNOC_CAM_NOC_ERL_ERRLOG1_LOW */
	.errlog1_high =  0x002c, /* CAMNOC_CAM_NOC_ERL_ERRLOG1_HIGH */
	.errlog2_low  =  0x0030, /* CAMNOC_CAM_NOC_ERL_ERRLOG2_LOW */
	.errlog2_high =  0x0034, /* CAMNOC_CAM_NOC_ERL_ERRLOG2_HIGH */
	.errlog3_low  =  0x0038, /* CAMNOC_CAM_NOC_ERL_ERRLOG3_LOW */
	.errlog3_high =  0x003c, /* CAMNOC_CAM_NOC_ERL_ERRLOG3_HIGH */
};

static struct cam_cpas_hw_errata_wa_list cam690_cpas100_errata_wa_list = {
	.camnoc_flush_slave_pending_trans = {
		.enable = false,
		.data.reg_info = {
			.access_type = CAM_REG_TYPE_READ,
			.offset = 0x2300, /* sbm_SenseIn0_Low */
			.mask = 0xE0000, /* Bits 17, 18, 19 */
			.value = 0, /* expected to be 0 */
		},
	},
	.enable_icp_clk_for_qchannel = {
		.enable = true,
	},
};

static struct cam_camnoc_info cam690_cpas100_camnoc_info = {
	.specific = &cam_cpas_v690_100_camnoc_specific[0],
	.specific_size = ARRAY_SIZE(cam_cpas_v690_100_camnoc_specific),
	.irq_sbm = &cam_cpas_v690_100_irq_sbm,
	.irq_err = &cam_cpas_v690_100_irq_err[0],
	.irq_err_size = ARRAY_SIZE(cam_cpas_v690_100_irq_err),
	.err_logger = &cam690_cpas100_err_logger_offsets,
	.errata_wa_list = &cam690_cpas100_errata_wa_list,
	.test_irq_info = {
		.sbm_enable_mask = 0x80,
		.sbm_clear_mask = 0x4,
	}
};

static struct cam_cpas_camnoc_qchannel cam690_cpas100_qchannel_info = {
	.qchannel_ctrl   = 0x5C,
	.qchannel_status = 0x60,
};

static struct cam_cpas_info cam690_cpas100_cpas_info = {
	.hw_caps_info = {
		.num_caps_registers = 1,
		.hw_caps_offsets = {0x8},
	},
	.qchannel_info = {&cam690_cpas100_qchannel_info},
	.num_qchannel = 1,
};

#endif /* _CPASTOP_V690_100_H_ */

