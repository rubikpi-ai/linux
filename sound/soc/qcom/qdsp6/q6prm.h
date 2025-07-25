/* SPDX-License-Identifier: GPL-2.0 */
//Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.

#ifndef __Q6PRM_H__
#define __Q6PRM_H__

/* Clock ID for Primary I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_PRI_MI2S_IBIT                          0x100
/* Clock ID for Primary I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_PRI_MI2S_EBIT                          0x101
/* Clock ID for Secondary I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_SEC_MI2S_IBIT                          0x102
/* Clock ID for Secondary I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_SEC_MI2S_EBIT                          0x103
/* Clock ID for Tertiary I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_TER_MI2S_IBIT                          0x104
/* Clock ID for Tertiary I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_TER_MI2S_EBIT                          0x105
/* Clock ID for Quartnery I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_QUAD_MI2S_IBIT                         0x106
/* Clock ID for Quartnery I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_QUAD_MI2S_EBIT                         0x107
/* Clock ID for Speaker I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_SPEAKER_I2S_IBIT                       0x108
/* Clock ID for Speaker I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_SPEAKER_I2S_EBIT                       0x109
/* Clock ID for Speaker I2S OSR */
#define Q6PRM_LPASS_CLK_ID_SPEAKER_I2S_OSR                        0x10A

/* Clock ID for QUINARY  I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_QUI_MI2S_IBIT			0x10B
/* Clock ID for QUINARY  I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_QUI_MI2S_EBIT			0x10C
/* Clock ID for SENARY  I2S IBIT */
#define Q6PRM_LPASS_CLK_ID_SEN_MI2S_IBIT			0x10D
/* Clock ID for SENARY  I2S EBIT */
#define Q6PRM_LPASS_CLK_ID_SEN_MI2S_EBIT			0x10E
/* Clock ID for INT0 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT0_MI2S_IBIT                       0x10F
/* Clock ID for INT1 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT1_MI2S_IBIT                       0x110
/* Clock ID for INT2 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT2_MI2S_IBIT                       0x111
/* Clock ID for INT3 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT3_MI2S_IBIT                       0x112
/* Clock ID for INT4 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT4_MI2S_IBIT                       0x113
/* Clock ID for INT5 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT5_MI2S_IBIT                       0x114
/* Clock ID for INT6 I2S IBIT  */
#define Q6PRM_LPASS_CLK_ID_INT6_MI2S_IBIT                       0x115

/* Clock ID for QUINARY MI2S OSR CLK  */
#define Q6PRM_LPASS_CLK_ID_QUI_MI2S_OSR                         0x116

/* Clock ID for Primary TDM IBIT */
#define Q6PRM_LPASS_CLK_ID_PRI_TDM_IBIT				0x200
/* Clock ID for Primary TDM EBIT */
#define Q6PRM_LPASS_CLK_ID_PRI_TDM_EBIT				0x201
/* Clock ID for Secondary TDM IBIT */
#define Q6PRM_LPASS_CLK_ID_SEC_TDM_IBIT				0x202
/* Clock ID for Secondary TDM EBIT */
#define Q6PRM_LPASS_CLK_ID_SEC_TDM_EBIT				0x203
/* Clock ID for Tertiary TDM IBIT */
#define Q6PRM_LPASS_CLK_ID_TER_TDM_IBIT				0x204
/* Clock ID for Tertiary TDM EBIT */
#define Q6PRM_LPASS_CLK_ID_TER_TDM_EBIT				0x205
/* Clock ID for Quartnery TDM IBIT */
#define Q6PRM_LPASS_CLK_ID_QUAD_TDM_IBIT			0x206
/* Clock ID for Quartnery TDM EBIT */
#define Q6PRM_LPASS_CLK_ID_QUAD_TDM_EBIT			0x207
/* Clock ID for QUINARY TDM IBIT */
#define Q6PRM_LPASS_CLK_ID_QUIN_TDM_IBIT			0x208
/* Clock ID for QUINARY TDM EBIT */
#define Q6PRM_LPASS_CLK_ID_QUIN_TDM_EBIT			0x209

/* Clock ID for MCLK1 */
#define Q6PRM_LPASS_CLK_ID_MCLK_1				0x300
/* Clock ID for MCLK2 */
#define Q6PRM_LPASS_CLK_ID_MCLK_2				0x301
/* Clock ID for MCLK3 */
#define Q6PRM_LPASS_CLK_ID_MCLK_3				0x302
/* Clock ID for MCLK4 */
#define Q6PRM_LPASS_CLK_ID_MCLK_4				0x304
/* Clock ID for Internal Digital Codec Core */
#define Q6PRM_LPASS_CLK_ID_INTERNAL_DIGITAL_CODEC_CORE		0x303

#define Q6PRM_LPASS_CLK_ID_WSA_CORE_MCLK			0x305
#define Q6PRM_LPASS_CLK_ID_WSA_CORE_NPL_MCLK			0x306

#define Q6PRM_LPASS_CLK_ID_VA_CORE_MCLK				0x307
#define Q6PRM_LPASS_CLK_ID_VA_CORE_2X_MCLK			0x308

#define Q6PRM_LPASS_CLK_ID_TX_CORE_MCLK				0x30c
#define Q6PRM_LPASS_CLK_ID_TX_CORE_NPL_MCLK			0x30d

#define Q6PRM_LPASS_CLK_ID_RX_CORE_MCLK				0x30e
#define Q6PRM_LPASS_CLK_ID_RX_CORE_NPL_MCLK			0x30f

/* Clock ID for MCLK for WSA2 core */
#define Q6PRM_LPASS_CLK_ID_WSA2_CORE_MCLK 0x310
/* Clock ID for NPL MCLK for WSA2 core */
#define Q6PRM_LPASS_CLK_ID_WSA2_CORE_2X_MCLK 0x311
/* Clock ID for RX Core TX MCLK */
#define Q6PRM_LPASS_CLK_ID_RX_CORE_TX_MCLK 0x312
/* Clock ID for RX CORE TX 2X MCLK */
#define Q6PRM_LPASS_CLK_ID_RX_CORE_TX_2X_MCLK 0x313
/* Clock ID for WSA core TX MCLK */
#define Q6PRM_LPASS_CLK_ID_WSA_CORE_TX_MCLK 0x314
/* Clock ID for WSA core TX 2X MCLK */
#define Q6PRM_LPASS_CLK_ID_WSA_CORE_TX_2X_MCLK 0x315
/* Clock ID for WSA2 core TX MCLK */
#define Q6PRM_LPASS_CLK_ID_WSA2_CORE_TX_MCLK 0x316
/* Clock ID for WSA2 core TX 2X MCLK */
#define Q6PRM_LPASS_CLK_ID_WSA2_CORE_TX_2X_MCLK 0x317
/* Clock ID for RX CORE MCLK2 2X  MCLK */
#define Q6PRM_LPASS_CLK_ID_RX_CORE_MCLK2_2X_MCLK 0x318

#define Q6PRM_LPASS_CLK_SRC_INTERNAL	1
#define Q6PRM_LPASS_CLK_ROOT_DEFAULT	0
#define Q6PRM_HW_CORE_ID_LPASS		1
#define Q6PRM_HW_CORE_ID_DCODEC		2

int q6prm_set_lpass_clock(struct device *dev, int clk_id, int clk_attr,
			  int clk_root, unsigned int freq);
int q6prm_vote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			     const char *client_name, uint32_t *client_handle);
int q6prm_unvote_lpass_core_hw(struct device *dev, uint32_t hw_block_id,
			       uint32_t client_handle);
#endif /* __Q6PRM_H__ */
