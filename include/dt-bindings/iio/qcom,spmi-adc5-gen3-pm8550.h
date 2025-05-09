/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_PM8550_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_PM8550_H

#ifndef PM8550_SID
#define PM8550_SID		1
#endif

/* ADC channels for PM8550_ADC for PMIC5 Gen3 */
#define PM8550_ADC5_GEN3_OFFSET_REF			(PM8550_SID << 8 | 0x00)
#define PM8550_ADC5_GEN3_1P25VREF			(PM8550_SID << 8 | 0x01)
#define PM8550_ADC5_GEN3_VREF_VADC			(PM8550_SID << 8 | 0x02)
#define PM8550_ADC5_GEN3_DIE_TEMP			(PM8550_SID << 8 | 0x03)

#define PM8550_ADC5_GEN3_AMUX_THM1			(PM8550_SID << 8 | 0x04)
#define PM8550_ADC5_GEN3_AMUX_THM2			(PM8550_SID << 8 | 0x05)
#define PM8550_ADC5_GEN3_AMUX_THM3			(PM8550_SID << 8 | 0x06)
#define PM8550_ADC5_GEN3_AMUX_THM4			(PM8550_SID << 8 | 0x07)
#define PM8550_ADC5_GEN3_AMUX_THM5			(PM8550_SID << 8 | 0x08)
#define PM8550_ADC5_GEN3_AMUX_THM6_GPIO2		(PM8550_SID << 8 | 0x09)
#define PM8550_ADC5_GEN3_AMUX1_GPIO3			(PM8550_SID << 8 | 0x0a)
#define PM8550_ADC5_GEN3_AMUX2_GPIO4			(PM8550_SID << 8 | 0x0b)
#define PM8550_ADC5_GEN3_AMUX3_GPIO7			(PM8550_SID << 8 | 0x0c)
#define PM8550_ADC5_GEN3_AMUX4_GPIO12			(PM8550_SID << 8 | 0x0d)

/* 100k pull-up */
#define PM8550_ADC5_GEN3_AMUX_THM1_100K_PU		(PM8550_SID << 8 | 0x44)
#define PM8550_ADC5_GEN3_AMUX_THM2_100K_PU		(PM8550_SID << 8 | 0x45)
#define PM8550_ADC5_GEN3_AMUX_THM3_100K_PU		(PM8550_SID << 8 | 0x46)
#define PM8550_ADC5_GEN3_AMUX_THM4_100K_PU		(PM8550_SID << 8 | 0x47)
#define PM8550_ADC5_GEN3_AMUX_THM5_100K_PU		(PM8550_SID << 8 | 0x48)
#define PM8550_ADC5_GEN3_AMUX_THM6_GPIO2_100K_PU	(PM8550_SID << 8 | 0x49)
#define PM8550_ADC5_GEN3_AMUX1_GPIO3_100K_PU		(PM8550_SID << 8 | 0x4a)
#define PM8550_ADC5_GEN3_AMUX2_GPIO4_100K_PU		(PM8550_SID << 8 | 0x4b)
#define PM8550_ADC5_GEN3_AMUX3_GPIO7_100K_PU		(PM8550_SID << 8 | 0x4c)
#define PM8550_ADC5_GEN3_AMUX4_GPIO12_100K_PU		(PM8550_SID << 8 | 0x4d)

/* 1/3 Divider */
#define PM8550_ADC5_GEN3_AMUX3_GPIO7_DIV3		(PM8550_SID << 8 | 0x8c)
#define PM8550_ADC5_GEN3_AMUX4_GPIO12_DIV3		(PM8550_SID << 8 | 0x8d)

#define PM8550_ADC5_GEN3_VPH_PWR			(PM8550_SID << 8 | 0x8e)

#endif /* _DT_BINDINGS_QCOM_SPMI_VADC_PM8550_H */
