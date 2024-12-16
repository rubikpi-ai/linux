/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_QCOM_SPMI_VADC_PM8550VX_H
#define _DT_BINDINGS_QCOM_SPMI_VADC_PM8550VX_H

/* ADC channels for PM8550VX_ADC for PMIC5 Gen3 */
#define PM8550VX_ADC5_GEN3_OFFSET_REF(sid)		((sid) << 8 | 0x00)
#define PM8550VX_ADC5_GEN3_1P25VREF(sid)		((sid) << 8 | 0x01)
#define PM8550VX_ADC5_GEN3_VREF_VADC(sid)		((sid) << 8 | 0x02)
#define PM8550VX_ADC5_GEN3_DIE_TEMP(sid)		((sid) << 8 | 0x03)

#endif /* _DT_BINDINGS_QCOM_SPMI_VADC_PM8550VX_H */
