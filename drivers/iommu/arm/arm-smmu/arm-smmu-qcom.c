// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/dev_printk.h>
#include <linux/adreno-smmu-priv.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/firmware/qcom/qcom_scm.h>

#include "arm-smmu.h"
#include "arm-smmu-qcom.h"

#define QCOM_DUMMY_VAL	-1

struct actlr_config {
	u16 sid;
	u16 mask;
	u32 actlr;
};

#define PREFETCH_DISABLE	0
#define PREFETCH_SHALLOW	BIT(8)
#define PREFETCH_DEEP		(BIT(9) | BIT(8))
#define PREFETCH_DEEP_GFX	(BIT(9) | BIT(8) | BIT(5) | BIT(3))
#define CPRE			BIT(1)		/* Enable context caching in the prefetch buffer */
#define CMTLB			BIT(0)		/* Enable context caching in the macro TLB */

static const struct actlr_config sc7280_apps_actlr_cfg[] = {
	{ 0x0800, 0x24E1, PREFETCH_DISABLE | CMTLB },
	{ 0x2000, 0x0163, PREFETCH_DISABLE | CMTLB },
	{ 0x2080, 0x0461, PREFETCH_DISABLE | CMTLB },
	{ 0x2100, 0x0161, PREFETCH_DISABLE | CMTLB },
	{ 0x0900, 0x0407, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x2180, 0x0027, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1000, 0x07ff, PREFETCH_DEEP | CPRE | CMTLB },
};

static const struct actlr_config sc7280_gfx_actlr_cfg[] = {
	{ 0x0000, 0x07ff, PREFETCH_DEEP_GFX | CPRE | CMTLB },
};

static const struct actlr_config sa7255p_apps_actlr_cfg[] = {
	{ 0x0800, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0801, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0802, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0803, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0840, 0x0480, PREFETCH_DISABLE | CMTLB },
	{ 0x0860, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x2400, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x2401, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x2402, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x2403, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1000, 0x0402, PREFETCH_DISABLE | CMTLB },
	{ 0x1001, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0880, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0881, 0x0404, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0883, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0884, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0887, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1961, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1962, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1963, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1964, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1981, 0x0440, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1982, 0x0440, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1983, 0x0440, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1984, 0x0440, PREFETCH_DEEP | CPRE | CMTLB },
};

static const struct actlr_config sa7255p_gfx_actlr_cfg[] = {
	{ 0x0000, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0001, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0002, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0004, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0005, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0007, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
};

static const struct actlr_config sa8775p_apps_actlr_cfg[] = {
	{ 0x0800, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0801, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0802, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0803, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0840, 0x0480, PREFETCH_DISABLE | CMTLB },
	{ 0x0860, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x3400, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x3401, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x3402, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x3403, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1000, 0x0402, PREFETCH_DISABLE | CMTLB },
	{ 0x1001, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x1800, 0x0402, PREFETCH_DISABLE | CMTLB },
	{ 0x1801, 0x0400, PREFETCH_DISABLE | CMTLB },
	{ 0x0880, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0881, 0x0404, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0883, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0884, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0887, 0x0400, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x2141, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2142, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2143, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2144, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2145, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2146, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2147, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2148, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2149, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x214a, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x214b, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x214c, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x214d, 0x0420, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2181, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2182, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2183, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2184, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2185, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2186, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2187, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2188, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2189, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x218a, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x218b, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x218c, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x218d, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x21ed, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x25cd, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2941, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2942, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2943, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2944, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2945, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2946, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2947, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2948, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2949, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x294a, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x294b, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x294c, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x294d, 0x04a0, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2981, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2982, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2983, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2984, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2985, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2986, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2987, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2988, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x2989, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x298a, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x298b, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x298c, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x298d, 0x0400, PREFETCH_DEEP | CPRE | CMTLB },
};

static const struct actlr_config sa8775p_gfx_actlr_cfg[] = {
	{ 0x0000, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0001, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0002, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0004, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0005, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0007, 0x0c00, PREFETCH_DEEP_GFX | CPRE | CMTLB },
};

static const struct actlr_config sm8550_apps_actlr_cfg[] = {
	{ 0x18a0, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x18e0, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0800, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1800, 0x00c0, PREFETCH_DISABLE | CMTLB },
	{ 0x1820, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1860, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0c01, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c02, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c03, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c04, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c05, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c06, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c07, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c08, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c09, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c0c, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c0d, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c0e, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0c0f, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1961, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1962, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1963, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1964, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1965, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1966, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1967, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1968, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1969, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x196c, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x196d, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x196e, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x196f, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c1, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c2, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c3, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c4, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c5, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c6, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c7, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c8, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19c9, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19cc, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19cd, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19ce, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x19cf, 0x0010, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x1c00, 0x0002, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1c01, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1920, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1923, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1924, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1940, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1941, 0x0004, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1943, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1944, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1947, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
};

static const struct actlr_config sm8550_gfx_actlr_cfg[] = {
	{ 0x0000, 0x03ff, PREFETCH_DEEP_GFX | CPRE | CMTLB },
};

static const struct actlr_config sa8797p_apps_actlr_cfg[] = {
	{ 0x0800, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0801, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0802, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0803, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0808, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0809, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x080a, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x080b, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0840, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0860, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x08c0, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x08c3, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0980, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0983, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x03e0, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x03e1, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x03e4, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x03e5, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x03e8, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x03e9, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x03ec, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x03ed, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0400, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x0401, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0404, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x0405, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0408, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x0409, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x040c, 0x0002, PREFETCH_DISABLE | CMTLB },
	{ 0x040d, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0900, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0904, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0923, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0940, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0941, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0943, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0944, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0945, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0946, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0948, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0949, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x094b, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x094c, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x094d, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x094e, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0950, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0951, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0953, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0954, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0955, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0956, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0958, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x0959, 0x0020, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x095b, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x095c, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x095d, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x095e, 0x0000, PREFETCH_SHALLOW | CPRE | CMTLB },
	{ 0x1401, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1402, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1403, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1404, 0x0020, PREFETCH_DISABLE | CMTLB },
	{ 0x1460, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1461, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1462, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1463, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x1480, 0x0000, PREFETCH_DISABLE | CMTLB },
	{ 0x0301, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0302, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0303, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0304, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0305, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0306, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0307, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0308, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0309, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x030a, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x030b, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x030c, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x030d, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0345, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x034d, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0361, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0362, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0363, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0364, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0366, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0367, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0368, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0369, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x036a, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x036b, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x036c, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0701, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0702, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0703, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0704, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0705, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0706, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0707, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0708, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0709, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x070a, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x070b, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x070c, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x070d, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0745, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x074d, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0761, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0762, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0763, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0764, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0766, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0767, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0768, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0769, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x076a, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x076b, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x076c, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b01, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b02, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b03, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b04, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b05, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b06, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b07, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b08, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b09, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b0a, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b0b, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b0c, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b0d, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b45, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b4d, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b61, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b62, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b63, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b64, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b66, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b67, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b68, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b69, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b6a, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b6b, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0b6c, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f01, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f02, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f03, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f04, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f05, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f06, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f07, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f08, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f09, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f0a, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f0b, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f0c, 0x0040, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f0d, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f45, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f4d, 0x0020, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f61, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f62, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f63, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f64, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f66, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f67, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f68, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f69, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f6a, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f6b, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
	{ 0x0f6c, 0x0000, PREFETCH_DEEP | CPRE | CMTLB },
};

static const struct actlr_config sa8797p_gfx_actlr_cfg[] = {
	{ 0x0000, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0001, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0002, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0004, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0005, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0007, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0008, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x0009, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x000a, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
	{ 0x000b, 0x0000, PREFETCH_DEEP_GFX | CPRE | CMTLB },
};

static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *smmu)
{
	return container_of(smmu, struct qcom_smmu, smmu);
}

static void *qcom_alloc_pages(void *cookie, size_t size, gfp_t gfp)
{
	struct page *p;
	struct qcom_scm_vmperm perms[2];
	u64 src  = BIT(QCOM_SCM_VMID_HLOS);
	int ret;

	struct arm_smmu_domain *domain = (void *)cookie;

	p = alloc_page(gfp);
	if (!p)
		return NULL;

	perms[0].vmid = QCOM_SCM_VMID_HLOS;
	perms[0].perm = QCOM_SCM_PERM_RW;
	perms[1].vmid = domain->secure_vmid;
	perms[1].perm = QCOM_SCM_PERM_READ;
	ret = qcom_scm_assign_mem(page_to_phys(p), PAGE_SIZE,
				  &src, perms, 2);
	if (ret < 0) {
		dev_err(domain->smmu->dev,
			"assign memory failed for vmid=%x ret=%d\n",
			domain->secure_vmid, ret);
		__free_page(p);
		return NULL;
	}

	return page_address(p);
}

static void qcom_free_pages(void *cookie, void *pages, size_t size)
{
	struct qcom_scm_vmperm perms;
	struct page *p;
	u64 src;
	int ret;

	struct arm_smmu_domain *domain = (void *)cookie;

	p = virt_to_page(pages);

	perms.vmid = QCOM_SCM_VMID_HLOS;
	perms.perm = QCOM_SCM_PERM_RWX;
	src = BIT(domain->secure_vmid) | BIT(QCOM_SCM_VMID_HLOS);
	ret = qcom_scm_assign_mem(page_to_phys(p), PAGE_SIZE,
				  &src, &perms, 1);
	/*
	 * For assign failure scenario, it is not safe to use these pages by HLOS.
	 * So returning from here instead of freeing the page.
	 */
	if (ret < 0) {
		dev_err(domain->smmu->dev,
			"assign memory failed to HLOS for vmid=%x ret=%d\n",
			domain->secure_vmid, ret);
		return;
	}

	__free_page(p);
}

static void qcom_smmu_tlb_sync(struct arm_smmu_device *smmu, int page,
				int sync, int status)
{
	unsigned int spin_cnt, delay;
	u32 reg;

	arm_smmu_writel(smmu, page, sync, QCOM_DUMMY_VAL);
	for (delay = 1; delay < TLB_LOOP_TIMEOUT; delay *= 2) {
		for (spin_cnt = TLB_SPIN_COUNT; spin_cnt > 0; spin_cnt--) {
			reg = arm_smmu_readl(smmu, page, status);
			if (!(reg & ARM_SMMU_sTLBGSTATUS_GSACTIVE))
				return;
			cpu_relax();
		}
		udelay(delay);
	}

	qcom_smmu_tlb_sync_debug(smmu);
}

static void qcom_adreno_smmu_write_sctlr(struct arm_smmu_device *smmu, int idx,
		u32 reg)
{
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);

	/*
	 * On the GPU device we want to process subsequent transactions after a
	 * fault to keep the GPU from hanging
	 */
	reg |= ARM_SMMU_SCTLR_HUPCF;

	if (qsmmu->stall_enabled & BIT(idx))
		reg |= ARM_SMMU_SCTLR_CFCFG;

	arm_smmu_cb_write(smmu, idx, ARM_SMMU_CB_SCTLR, reg);
}

static void qcom_adreno_smmu_get_fault_info(const void *cookie,
		struct adreno_smmu_fault_info *info)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	info->fsr = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSR);
	info->fsynr0 = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSYNR0);
	info->fsynr1 = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_FSYNR1);
	info->far = arm_smmu_cb_readq(smmu, cfg->cbndx, ARM_SMMU_CB_FAR);
	info->cbfrsynra = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBFRSYNRA(cfg->cbndx));
	info->ttbr0 = arm_smmu_cb_readq(smmu, cfg->cbndx, ARM_SMMU_CB_TTBR0);
	info->contextidr = arm_smmu_cb_read(smmu, cfg->cbndx, ARM_SMMU_CB_CONTEXTIDR);
}

static void qcom_adreno_smmu_set_stall(const void *cookie, bool enabled)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu_domain->smmu);

	if (enabled)
		qsmmu->stall_enabled |= BIT(cfg->cbndx);
	else
		qsmmu->stall_enabled &= ~BIT(cfg->cbndx);
}

static void qcom_adreno_smmu_resume_translation(const void *cookie, bool terminate)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	u32 reg = 0;

	if (terminate)
		reg |= ARM_SMMU_RESUME_TERMINATE;

	arm_smmu_cb_write(smmu, cfg->cbndx, ARM_SMMU_CB_RESUME, reg);
}

#define QCOM_ADRENO_SMMU_GPU_SID 0

static bool qcom_adreno_smmu_is_gpu_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	int i;

	/*
	 * The GPU will always use SID 0 so that is a handy way to uniquely
	 * identify it and configure it for per-instance pagetables
	 */
	for (i = 0; i < fwspec->num_ids; i++) {
		u16 sid = FIELD_GET(ARM_SMMU_SMR_ID, fwspec->ids[i]);

		if (sid == QCOM_ADRENO_SMMU_GPU_SID)
			return true;
	}

	return false;
}

static const struct io_pgtable_cfg *qcom_adreno_smmu_get_ttbr1_cfg(
		const void *cookie)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct io_pgtable *pgtable =
		io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	return &pgtable->cfg;
}

/*
 * Local implementation to configure TTBR0 with the specified pagetable config.
 * The GPU driver will call this to enable TTBR0 when per-instance pagetables
 * are active
 */

static int qcom_adreno_smmu_set_ttbr0_cfg(const void *cookie,
		const struct io_pgtable_cfg *pgtbl_cfg)
{
	struct arm_smmu_domain *smmu_domain = (void *)cookie;
	struct io_pgtable *pgtable = io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	struct arm_smmu_cfg *cfg = &smmu_domain->cfg;
	struct arm_smmu_cb *cb = &smmu_domain->smmu->cbs[cfg->cbndx];

	/* The domain must have split pagetables already enabled */
	if (cb->tcr[0] & ARM_SMMU_TCR_EPD1)
		return -EINVAL;

	/* If the pagetable config is NULL, disable TTBR0 */
	if (!pgtbl_cfg) {
		/* Do nothing if it is already disabled */
		if ((cb->tcr[0] & ARM_SMMU_TCR_EPD0))
			return -EINVAL;

		/* Set TCR to the original configuration */
		cb->tcr[0] = arm_smmu_lpae_tcr(&pgtable->cfg);
		cb->ttbr[0] = FIELD_PREP(ARM_SMMU_TTBRn_ASID, cb->cfg->asid);
	} else {
		u32 tcr = cb->tcr[0];

		/* Don't call this again if TTBR0 is already enabled */
		if (!(cb->tcr[0] & ARM_SMMU_TCR_EPD0))
			return -EINVAL;

		tcr |= arm_smmu_lpae_tcr(pgtbl_cfg);
		tcr &= ~(ARM_SMMU_TCR_EPD0 | ARM_SMMU_TCR_EPD1);

		cb->tcr[0] = tcr;
		cb->ttbr[0] = pgtbl_cfg->arm_lpae_s1_cfg.ttbr;
		cb->ttbr[0] |= FIELD_PREP(ARM_SMMU_TTBRn_ASID, cb->cfg->asid);
	}

	arm_smmu_write_context_bank(smmu_domain->smmu, cb->cfg->cbndx);

	return 0;
}

static int qcom_adreno_smmu_alloc_context_bank(struct arm_smmu_domain *smmu_domain,
					       struct arm_smmu_device *smmu,
					       struct device *dev, int start)
{
	int count;

	/*
	 * Assign context bank 0 to the GPU device so the GPU hardware can
	 * switch pagetables
	 */
	if (qcom_adreno_smmu_is_gpu_device(dev)) {
		start = 0;
		count = 1;
	} else {
		start = 1;
		count = smmu->num_context_banks;
	}

	return __arm_smmu_alloc_bitmap(smmu->context_map, start, count);
}

static bool qcom_adreno_can_do_ttbr1(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;

	if (of_device_is_compatible(np, "qcom,msm8996-smmu-v2"))
		return false;

	return true;
}

static void arm_smmu_set_actlr(struct device *dev, struct arm_smmu_device *smmu, int cbndx,
		const struct actlr_config *actlrcfg, size_t actlrcfg_size)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct arm_smmu_smr *smr;
	int i;
	int j;
	u16 id;
	u16 mask;
	int idx;

	for (i = 0; i < actlrcfg_size; ++i) {
		id = (actlrcfg + i)->sid;
		mask = (actlrcfg + i)->mask;

		for_each_cfg_sme(cfg, fwspec, j, idx) {
			smr = &smmu->smrs[idx];
			if (smr_is_subset(*smr, id, mask))
				arm_smmu_cb_write(smmu, cbndx, ARM_SMMU_CB_ACTLR,
						(actlrcfg + i)->actlr);
		}
	}
}

static int qcom_adreno_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	struct adreno_smmu_priv *priv;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	const struct actlr_config *actlrcfg;
	size_t actlrcfg_size;
	int cbndx = smmu_domain->cfg.cbndx;
	u32 val;

	smmu_domain->cfg.flush_walk_prefer_tlbiasid = true;

	/*
	 * For those client where qcom,iommu-vmid is not defined, default arm-smmu pgtable
	 * alloc/free handler will be used.
	 */
	if (of_property_read_u32(dev->of_node, "qcom,iommu-vmid", &val) == 0) {
		smmu_domain->secure_vmid = val;
		pgtbl_cfg->alloc = qcom_alloc_pages;
		pgtbl_cfg->free = qcom_free_pages;
	}

	if (qsmmu->data->actlrcfg_gfx) {
		actlrcfg = qsmmu->data->actlrcfg_gfx;
		actlrcfg_size = qsmmu->data->actlrcfg_gfx_size;
		arm_smmu_set_actlr(dev, smmu, cbndx, actlrcfg, actlrcfg_size);
	}

	/* Only enable split pagetables for the GPU device (SID 0) */
	if (!qcom_adreno_smmu_is_gpu_device(dev))
		return 0;

	/*
	 * All targets that use the qcom,adreno-smmu compatible string *should*
	 * be AARCH64 stage 1 but double check because the arm-smmu code assumes
	 * that is the case when the TTBR1 quirk is enabled
	 */
	if (qcom_adreno_can_do_ttbr1(smmu_domain->smmu) &&
	    (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) &&
	    (smmu_domain->cfg.fmt == ARM_SMMU_CTX_FMT_AARCH64))
		pgtbl_cfg->quirks |= IO_PGTABLE_QUIRK_ARM_TTBR1;

	/*
	 * Initialize private interface with GPU:
	 */

	priv = dev_get_drvdata(dev);
	if (!priv)
		return -ENODATA;
	priv->cookie = smmu_domain;
	priv->get_ttbr1_cfg = qcom_adreno_smmu_get_ttbr1_cfg;
	priv->set_ttbr0_cfg = qcom_adreno_smmu_set_ttbr0_cfg;
	priv->get_fault_info = qcom_adreno_smmu_get_fault_info;
	priv->set_stall = qcom_adreno_smmu_set_stall;
	priv->resume_translation = qcom_adreno_smmu_resume_translation;

	return 0;
}

static const struct of_device_id qcom_smmu_client_of_match[] __maybe_unused = {
	{ .compatible = "qcom,adreno" },
	{ .compatible = "qcom,adreno-gmu" },
	{ .compatible = "qcom,mdp4" },
	{ .compatible = "qcom,mdss" },
	{ .compatible = "qcom,sc7180-mdss" },
	{ .compatible = "qcom,sc7180-mss-pil" },
	{ .compatible = "qcom,sc7280-mdss" },
	{ .compatible = "qcom,sc7280-mss-pil" },
	{ .compatible = "qcom,sc8180x-mdss" },
	{ .compatible = "qcom,sc8280xp-mdss" },
	{ .compatible = "qcom,sdm670-mdss" },
	{ .compatible = "qcom,sdm845-mdss" },
	{ .compatible = "qcom,sdm845-mss-pil" },
	{ .compatible = "qcom,sm6350-mdss" },
	{ .compatible = "qcom,sm6375-mdss" },
	{ .compatible = "qcom,sm8150-mdss" },
	{ .compatible = "qcom,sm8250-mdss" },
	{ }
};

static int qcom_smmu_init_context(struct arm_smmu_domain *smmu_domain,
		struct io_pgtable_cfg *pgtbl_cfg, struct device *dev)
{
	u32 val;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	const struct actlr_config *actlrcfg;
	size_t actlrcfg_size;
	int cbndx = smmu_domain->cfg.cbndx;

	if (qsmmu->data->actlrcfg) {
		actlrcfg = qsmmu->data->actlrcfg;
		actlrcfg_size = qsmmu->data->actlrcfg_size;
		arm_smmu_set_actlr(dev, smmu, cbndx, actlrcfg, actlrcfg_size);
	}

	smmu_domain->cfg.flush_walk_prefer_tlbiasid = true;

	/*
	 * For those client where qcom,iommu-vmid is not defined, default arm-smmu pgtable
	 * alloc/free handler will be used.
	 */
	if (of_property_read_u32(dev->of_node, "qcom,iommu-vmid", &val) == 0) {
		smmu_domain->secure_vmid = val;
		pgtbl_cfg->alloc = qcom_alloc_pages;
		pgtbl_cfg->free = qcom_free_pages;
	}

	return 0;
}

static int qcom_smmu_cfg_probe(struct arm_smmu_device *smmu)
{
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	unsigned int last_s2cr;
	u32 reg;
	u32 smr;
	int i;

	/*
	 * MSM8998 LPASS SMMU reports 13 context banks, but accessing
	 * the last context bank crashes the system.
	 */
	if (of_device_is_compatible(smmu->dev->of_node, "qcom,msm8998-smmu-v2") &&
	    smmu->num_context_banks == 13) {
		smmu->num_context_banks = 12;
	} else if (of_device_is_compatible(smmu->dev->of_node, "qcom,sdm630-smmu-v2")) {
		if (smmu->num_context_banks == 21) /* SDM630 / SDM660 A2NOC SMMU */
			smmu->num_context_banks = 7;
		else if (smmu->num_context_banks == 14) /* SDM630 / SDM660 LPASS SMMU */
			smmu->num_context_banks = 13;
	}

	/*
	 * Some platforms support more than the Arm SMMU architected maximum of
	 * 128 stream matching groups. For unknown reasons, the additional
	 * groups don't exhibit the same behavior as the architected registers,
	 * so limit the groups to 128 until the behavior is fixed for the other
	 * groups.
	 */
	if (smmu->num_mapping_groups > 128) {
		dev_notice(smmu->dev, "\tLimiting the stream matching groups to 128\n");
		smmu->num_mapping_groups = 128;
	}

	last_s2cr = ARM_SMMU_GR0_S2CR(smmu->num_mapping_groups - 1);

	/*
	 * With some firmware versions writes to S2CR of type FAULT are
	 * ignored, and writing BYPASS will end up written as FAULT in the
	 * register. Perform a write to S2CR to detect if this is the case and
	 * if so reserve a context bank to emulate bypass streams.
	 */
	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_BYPASS) |
	      FIELD_PREP(ARM_SMMU_S2CR_CBNDX, 0xff) |
	      FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, S2CR_PRIVCFG_DEFAULT);
	arm_smmu_gr0_write(smmu, last_s2cr, reg);
	reg = arm_smmu_gr0_read(smmu, last_s2cr);
	if (FIELD_GET(ARM_SMMU_S2CR_TYPE, reg) != S2CR_TYPE_BYPASS) {
		qsmmu->bypass_quirk = true;
		qsmmu->bypass_cbndx = smmu->num_context_banks - 1;

		set_bit(qsmmu->bypass_cbndx, smmu->context_map);

		arm_smmu_cb_write(smmu, qsmmu->bypass_cbndx, ARM_SMMU_CB_SCTLR, 0);

		reg = FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S1_TRANS_S2_BYPASS);
		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(qsmmu->bypass_cbndx), reg);
	}

	for (i = 0; i < smmu->num_mapping_groups; i++) {
		smr = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_SMR(i));

		if (FIELD_GET(ARM_SMMU_SMR_VALID, smr)) {
			/* Ignore valid bit for SMR mask extraction. */
			smr &= ~ARM_SMMU_SMR_VALID;
			smmu->smrs[i].id = FIELD_GET(ARM_SMMU_SMR_ID, smr);
			smmu->smrs[i].mask = FIELD_GET(ARM_SMMU_SMR_MASK, smr);
			smmu->smrs[i].valid = true;

			smmu->s2crs[i].type = S2CR_TYPE_BYPASS;
			smmu->s2crs[i].privcfg = S2CR_PRIVCFG_DEFAULT;
			smmu->s2crs[i].cbndx = 0xff;
		}
	}

	return 0;
}

static int qcom_adreno_smmuv2_cfg_probe(struct arm_smmu_device *smmu)
{
	/* Support for 16K pages is advertised on some SoCs, but it doesn't seem to work */
	smmu->features &= ~ARM_SMMU_FEAT_FMT_AARCH64_16K;

	/* TZ protects several last context banks, hide them from Linux */
	if (of_device_is_compatible(smmu->dev->of_node, "qcom,sdm630-smmu-v2") &&
	    smmu->num_context_banks == 5)
		smmu->num_context_banks = 2;

	return 0;
}

static void qcom_smmu_write_s2cr(struct arm_smmu_device *smmu, int idx)
{
	struct arm_smmu_s2cr *s2cr = smmu->s2crs + idx;
	struct qcom_smmu *qsmmu = to_qcom_smmu(smmu);
	u32 cbndx = s2cr->cbndx;
	u32 type = s2cr->type;
	u32 reg;

	if (qsmmu->bypass_quirk) {
		if (type == S2CR_TYPE_BYPASS) {
			/*
			 * Firmware with quirky S2CR handling will substitute
			 * BYPASS writes with FAULT, so point the stream to the
			 * reserved context bank and ask for translation on the
			 * stream
			 */
			type = S2CR_TYPE_TRANS;
			cbndx = qsmmu->bypass_cbndx;
		} else if (type == S2CR_TYPE_FAULT) {
			/*
			 * Firmware with quirky S2CR handling will ignore FAULT
			 * writes, so trick it to write FAULT by asking for a
			 * BYPASS.
			 */
			type = S2CR_TYPE_BYPASS;
			cbndx = 0xff;
		}
	}

	reg = FIELD_PREP(ARM_SMMU_S2CR_TYPE, type) |
	      FIELD_PREP(ARM_SMMU_S2CR_CBNDX, cbndx) |
	      FIELD_PREP(ARM_SMMU_S2CR_PRIVCFG, s2cr->privcfg);
	arm_smmu_gr0_write(smmu, ARM_SMMU_GR0_S2CR(idx), reg);
}

static int qcom_smmu_def_domain_type(struct device *dev)
{
	const struct of_device_id *match =
		of_match_device(qcom_smmu_client_of_match, dev);

	return match ? IOMMU_DOMAIN_IDENTITY : 0;
}

static int qcom_smmu500_reset(struct arm_smmu_device *smmu)
{
	int i;
	u32 reg;

	arm_mmu500_reset(smmu);

	/* Re-enable context caching after reset */
	for (i = 0; i < smmu->num_context_banks; ++i) {
		reg = arm_smmu_cb_read(smmu, i, ARM_SMMU_CB_ACTLR);
		reg |= CPRE;
		arm_smmu_cb_write(smmu, i, ARM_SMMU_CB_ACTLR, reg);
	}

	return 0;
}

static int qcom_sdm845_smmu500_reset(struct arm_smmu_device *smmu)
{
	int ret;

	qcom_smmu500_reset(smmu);

	/*
	 * To address performance degradation in non-real time clients,
	 * such as USB and UFS, turn off wait-for-safe on sdm845 based boards,
	 * such as MTP and db845, whose firmwares implement secure monitor
	 * call handlers to turn on/off the wait-for-safe logic.
	 */
	ret = qcom_scm_qsmmu500_wait_safe_toggle(0);
	if (ret)
		dev_warn(smmu->dev, "Failed to turn off SAFE logic\n");

	return ret;
}

static const struct arm_smmu_impl qcom_smmu_v2_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static const struct arm_smmu_impl qcom_smmu_500_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_smmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
#ifdef CONFIG_ARM_SMMU_QCOM_TBU
	.context_fault = qcom_smmu_context_fault,
	.context_fault_needs_threaded_irq = true,
#endif
};

static const struct arm_smmu_impl sdm845_smmu_500_impl = {
	.init_context = qcom_smmu_init_context,
	.cfg_probe = qcom_smmu_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_sdm845_smmu500_reset,
	.write_s2cr = qcom_smmu_write_s2cr,
	.tlb_sync = qcom_smmu_tlb_sync,
#ifdef CONFIG_ARM_SMMU_QCOM_TBU
	.context_fault = qcom_smmu_context_fault,
	.context_fault_needs_threaded_irq = true,
#endif
};

static const struct arm_smmu_impl qcom_adreno_smmu_v2_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.cfg_probe = qcom_adreno_smmuv2_cfg_probe,
	.def_domain_type = qcom_smmu_def_domain_type,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.write_sctlr = qcom_adreno_smmu_write_sctlr,
	.tlb_sync = qcom_smmu_tlb_sync,
};

static const struct arm_smmu_impl qcom_adreno_smmu_500_impl = {
	.init_context = qcom_adreno_smmu_init_context,
	.def_domain_type = qcom_smmu_def_domain_type,
	.reset = qcom_smmu500_reset,
	.alloc_context_bank = qcom_adreno_smmu_alloc_context_bank,
	.write_sctlr = qcom_adreno_smmu_write_sctlr,
	.tlb_sync = qcom_smmu_tlb_sync,
#ifdef CONFIG_ARM_SMMU_QCOM_TBU
	.context_fault = qcom_smmu_context_fault,
	.context_fault_needs_threaded_irq = true,
#endif
};

static struct arm_smmu_device *qcom_smmu_create(struct arm_smmu_device *smmu,
		const struct qcom_smmu_match_data *data)
{
	const struct device_node *np = smmu->dev->of_node;
	const struct arm_smmu_impl *impl;
	struct qcom_smmu *qsmmu;
	int ret;

	if (!data)
		return ERR_PTR(-EINVAL);

	if (np && of_device_is_compatible(np, "qcom,adreno-smmu"))
		impl = data->adreno_impl;
	else
		impl = data->impl;

	if (!impl)
		return smmu;

	/* Check to make sure qcom_scm has finished probing */
	if (!qcom_scm_is_available())
		return ERR_PTR(-EPROBE_DEFER);

	qsmmu = devm_krealloc(smmu->dev, smmu, sizeof(*qsmmu), GFP_KERNEL);
	if (!qsmmu)
		return ERR_PTR(-ENOMEM);
	smmu = &qsmmu->smmu;

	qsmmu->smmu.impl = impl;
	qsmmu->data = data;

	INIT_LIST_HEAD(&qsmmu->tbu_list);
	ret = devm_of_platform_populate(smmu->dev);
	if (ret)
		return ERR_PTR(ret);

	return smmu;
}

/* Implementation Defined Register Space 0 register offsets */
static const u32 qcom_smmu_impl0_reg_offset[] = {
	[QCOM_SMMU_TBU_PWR_STATUS]		= 0x2204,
	[QCOM_SMMU_STATS_SYNC_INV_TBU_ACK]	= 0x25dc,
	[QCOM_SMMU_MMU2QSS_AND_SAFE_WAIT_CNTR]	= 0x2670,
};

static const struct qcom_smmu_config qcom_smmu_impl0_cfg = {
	.reg_offset = qcom_smmu_impl0_reg_offset,
};

/*
 * It is not yet possible to use MDP SMMU with the bypass quirk on the msm8996,
 * there are not enough context banks.
 */
static const struct qcom_smmu_match_data msm8996_smmu_data = {
	.impl = NULL,
	.adreno_impl = &qcom_adreno_smmu_v2_impl,
};

static const struct qcom_smmu_match_data qcom_smmu_v2_data = {
	.impl = &qcom_smmu_v2_impl,
	.adreno_impl = &qcom_adreno_smmu_v2_impl,
};

static const struct qcom_smmu_match_data sdm845_smmu_500_data = {
	.impl = &sdm845_smmu_500_impl,
	/*
	 * No need for adreno impl here. On sdm845 the Adreno SMMU is handled
	 * by the separate sdm845-smmu-v2 device.
	 */
	/* Also no debug configuration. */
};

static const struct qcom_smmu_match_data sc7280_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
	.actlrcfg = sc7280_apps_actlr_cfg,
	.actlrcfg_size = ARRAY_SIZE(sc7280_apps_actlr_cfg),
	.actlrcfg_gfx = sc7280_gfx_actlr_cfg,
	.actlrcfg_gfx_size = ARRAY_SIZE(sc7280_gfx_actlr_cfg),
};

static const struct qcom_smmu_match_data sa7255p_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
	.actlrcfg = sa7255p_apps_actlr_cfg,
	.actlrcfg_size = ARRAY_SIZE(sa7255p_apps_actlr_cfg),
	.actlrcfg_gfx = sa7255p_gfx_actlr_cfg,
	.actlrcfg_gfx_size = ARRAY_SIZE(sa7255p_gfx_actlr_cfg),
};

static const struct qcom_smmu_match_data sa8775p_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
	.actlrcfg = sa8775p_apps_actlr_cfg,
	.actlrcfg_size = ARRAY_SIZE(sa8775p_apps_actlr_cfg),
	.actlrcfg_gfx = sa8775p_gfx_actlr_cfg,
	.actlrcfg_gfx_size = ARRAY_SIZE(sa8775p_gfx_actlr_cfg),
};

static const struct qcom_smmu_match_data sa8797p_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
	.actlrcfg = sa8797p_apps_actlr_cfg,
	.actlrcfg_size = ARRAY_SIZE(sa8797p_apps_actlr_cfg),
	.actlrcfg_gfx = sa8797p_gfx_actlr_cfg,
	.actlrcfg_gfx_size = ARRAY_SIZE(sa8797p_gfx_actlr_cfg),
};

static const struct qcom_smmu_match_data sm8550_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
	.actlrcfg = sm8550_apps_actlr_cfg,
	.actlrcfg_size = ARRAY_SIZE(sm8550_apps_actlr_cfg),
	.actlrcfg_gfx = sm8550_gfx_actlr_cfg,
	.actlrcfg_gfx_size = ARRAY_SIZE(sm8550_gfx_actlr_cfg),
};

static const struct qcom_smmu_match_data qcom_smmu_500_impl0_data = {
	.impl = &qcom_smmu_500_impl,
	.adreno_impl = &qcom_adreno_smmu_500_impl,
	.cfg = &qcom_smmu_impl0_cfg,
};

/*
 * Do not add any more qcom,SOC-smmu-500 entries to this list, unless they need
 * special handling and can not be covered by the qcom,smmu-500 entry.
 */
static const struct of_device_id __maybe_unused qcom_smmu_impl_of_match[] = {
	{ .compatible = "qcom,msm8996-smmu-v2", .data = &msm8996_smmu_data },
	{ .compatible = "qcom,msm8998-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,qcm2290-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,qdu1000-smmu-500", .data = &qcom_smmu_500_impl0_data  },
	{ .compatible = "qcom,sc7180-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sc7180-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sc7280-smmu-500", .data = &sc7280_smmu_500_impl0_data },
	{ .compatible = "qcom,sa7255p-smmu-500", .data = &sa7255p_smmu_500_impl0_data },
	{ .compatible = "qcom,sa8255p-smmu-500", .data = &sa8775p_smmu_500_impl0_data },
	{ .compatible = "qcom,sa8775p-smmu-500", .data = &sa8775p_smmu_500_impl0_data },
	{ .compatible = "qcom,sa8797p-smmu-500", .data = &sa8797p_smmu_500_impl0_data },
	{ .compatible = "qcom,sc8180x-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sc8280xp-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sdm630-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sdm670-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sdm845-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sdm845-smmu-500", .data = &sdm845_smmu_500_data },
	{ .compatible = "qcom,sm6115-smmu-500", .data = &qcom_smmu_500_impl0_data},
	{ .compatible = "qcom,sm6125-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm6350-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sm6350-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm6375-smmu-v2", .data = &qcom_smmu_v2_data },
	{ .compatible = "qcom,sm6375-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8150-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8250-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8350-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8450-smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ .compatible = "qcom,sm8550-smmu-500", .data = &sm8550_smmu_500_impl0_data },
	{ .compatible = "qcom,smmu-500", .data = &qcom_smmu_500_impl0_data },
	{ }
};

#ifdef CONFIG_ACPI
static struct acpi_platform_list qcom_acpi_platlist[] = {
	{ "LENOVO", "CB-01   ", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ "QCOM  ", "QCOMEDK2", 0x8180, ACPI_SIG_IORT, equal, "QCOM SMMU" },
	{ }
};
#endif

struct arm_smmu_device *qcom_smmu_impl_init(struct arm_smmu_device *smmu)
{
	const struct device_node *np = smmu->dev->of_node;
	const struct of_device_id *match;

#ifdef CONFIG_ACPI
	if (np == NULL) {
		/* Match platform for ACPI boot */
		if (acpi_match_platform_list(qcom_acpi_platlist) >= 0)
			return qcom_smmu_create(smmu, &qcom_smmu_500_impl0_data);
	}
#endif

	match = of_match_node(qcom_smmu_impl_of_match, np);
	if (match)
		return qcom_smmu_create(smmu, match->data);

	/*
	 * If you hit this WARN_ON() you are missing an entry in the
	 * qcom_smmu_impl_of_match[] table, and GPU per-process page-
	 * tables will be broken.
	 */
	WARN(of_device_is_compatible(np, "qcom,adreno-smmu"),
	     "Missing qcom_smmu_impl_of_match entry for: %s",
	     dev_name(smmu->dev));

	return smmu;
}
