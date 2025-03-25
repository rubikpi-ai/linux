/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_SOCINFO_H__
#define __QCOM_SOCINFO_H__

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID		137

#define SMEM_SOCINFO_BUILD_ID_LENGTH	32
#define SMEM_SOCINFO_CHIP_ID_LENGTH	32

#define SMEM_DDR_BUILD_ID		603

#define MAX_IDX_CH 8

/* DDR info SMEM item structure */
struct ddr_part_details {
	u8 revision_id1[2];
	u8 revision_id2[2];
	u8 width[2];
	u8 density[2];
};

struct ddr_freq_table {
	__le32 freq_khz;
	u8 enable;
};

struct ddr_freq_plan_entry {
	struct ddr_freq_table ddr_freq[14];
	u8 num_ddr_freqs;
	__le32 *clk_period_address;
	__le32 max_nom_ddr_freq;
};

struct ddrinfo {
	u8 manuf_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_IDX_CH];
	struct ddr_freq_plan_entry ddr_freq_tbl;
	u8 num_channels;
	u8 num_ranks[2];
	u8 hbb[2][2];
};

/* Socinfo SMEM item structure */
struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	char build_id[SMEM_SOCINFO_BUILD_ID_LENGTH];
	/* Version 2 */
	__le32 raw_id;
	__le32 raw_ver;
	/* Version 3 */
	__le32 hw_plat;
	/* Version 4 */
	__le32 plat_ver;
	/* Version 5 */
	__le32 accessory_chip;
	/* Version 6 */
	__le32 hw_plat_subtype;
	/* Version 7 */
	__le32 pmic_model;
	__le32 pmic_die_rev;
	/* Version 8 */
	__le32 pmic_model_1;
	__le32 pmic_die_rev_1;
	__le32 pmic_model_2;
	__le32 pmic_die_rev_2;
	/* Version 9 */
	__le32 foundry_id;
	/* Version 10 */
	__le32 serial_num;
	/* Version 11 */
	__le32 num_pmics;
	__le32 pmic_array_offset;
	/* Version 12 */
	__le32 chip_family;
	__le32 raw_device_family;
	__le32 raw_device_num;
	/* Version 13 */
	__le32 nproduct_id;
	char chip_id[SMEM_SOCINFO_CHIP_ID_LENGTH];
	/* Version 14 */
	__le32 num_clusters;
	__le32 ncluster_array_offset;
	__le32 num_subset_parts;
	__le32 nsubset_parts_array_offset;
	/* Version 15 */
	__le32 nmodem_supported;
	/* Version 16 */
	__le32  feature_code;
	__le32  pcode;
	__le32  npartnamemap_offset;
	__le32  nnum_partname_mapping;
	/* Version 17 */
	__le32 oem_variant;
	/* Version 18 */
	__le32 num_kvps;
	__le32 kvps_offset;
	/* Version 19 */
	__le32 num_func_clusters;
	__le32 boot_cluster;
	__le32 boot_core;
	/* Version 20 */
	__le32 raw_package_type;
	/* Version 21 */
	__le32 nsubpart_feat_array_offset;
};

#endif
