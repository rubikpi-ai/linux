/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * include/linux/msm_audio.h
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UAPI_LINUX_MSM_AUDIO_H
#define _UAPI_LINUX_MSM_AUDIO_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define AUDIO_IOCTL_MAGIC 'a'

#define IOCTL_MAP_PHYS_ADDR _IOW(AUDIO_IOCTL_MAGIC, 97, int)
#define IOCTL_UNMAP_PHYS_ADDR _IOW(AUDIO_IOCTL_MAGIC, 98, int)
#define IOCTL_MAP_HYP_ASSIGN _IOW(AUDIO_IOCTL_MAGIC, 99, int)
#define IOCTL_UNMAP_HYP_ASSIGN _IOW(AUDIO_IOCTL_MAGIC, 100, int)

#endif
