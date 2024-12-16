// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bug.h>
#include <linux/errno.h>

#include "camss-format.h"

/*
 * camss_format_get_bpp - Map media bus format to bpp
 * @formats: supported media bus formats array
 * @nformats: size of @formats array
 * @code: media bus format code
 *
 * Return number of bpp
 */
u8 camss_format_get_bpp(const struct camss_format_info *formats, unsigned int nformats, u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return formats[i].mbus_bpp;

	WARN(1, "Unknown format\n");

	return formats[0].mbus_bpp;
}

/*
 * camss_format_find_code - Find a format code in an array
 * @code: a pointer to media bus format codes array
 * @n_code: size of @code array
 * @index: index of code in the array
 * @req_code: required code
 *
 * Return media bus format code
 */
u32 camss_format_find_code(u32 *code, unsigned int n_code, unsigned int index, u32 req_code)
{
	int i;

	if (!req_code && index >= n_code)
		return 0;

	for (i = 0; i < n_code; i++) {
		if (req_code) {
			if (req_code == code[i])
				return req_code;
		} else {
			if (i == index)
				return code[i];
		}
	}

	return code[0];
}

/*
 * camss_format_find_format - Find a format in an array
 * @code: media bus format code
 * @pixelformat: V4L2 pixelformat FCC identifier
 * @formats: a pointer to formats array
 * @nformats: size of @formats array
 *
 * Return index of a format or a negative error code otherwise
 */
int camss_format_find_format(u32 code, u32 pixelformat, const struct camss_format_info *formats,
			     unsigned int nformats)
{
	int i;

	for (i = 0; i < nformats; i++) {
		if (formats[i].code == code &&
		    formats[i].pixelformat == pixelformat)
			return i;
	}

	for (i = 0; i < nformats; i++) {
		if (formats[i].code == code)
			return i;
	}

	WARN_ON(1);

	return -EINVAL;
}
