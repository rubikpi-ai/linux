/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __SOC_QCOM_PMIC_GLINK_ALTMODE_H__
#define __SOC_QCOM_PMIC_GLINK_ALTMODE_H__

#include <linux/usb/typec_dp.h>

int pmic_glink_altmode_register_client(void (*cb)(void *priv, struct typec_displayport_data data,
					int orientation),
					void *priv);

#endif
