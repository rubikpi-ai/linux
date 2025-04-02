/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/usb/role.h>

#if IS_ENABLED(CONFIG_USB_QCOM_EUD)
bool qcom_eud_vbus_control(struct usb_role_switch *sw);
#else
static inline bool qcom_eud_vbus_control(struct usb_role_switch *sw)
{ return false; }
#endif
