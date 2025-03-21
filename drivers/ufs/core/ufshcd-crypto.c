// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <ufs/ufshcd.h>
#include "ufshcd-crypto.h"

/* Blk-crypto modes supported by UFS crypto */
static const struct ufs_crypto_alg_entry {
	enum ufs_crypto_alg ufs_alg;
	enum ufs_crypto_key_size ufs_key_size;
} ufs_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.ufs_alg = UFS_CRYPTO_ALG_AES_XTS,
		.ufs_key_size = UFS_CRYPTO_KEY_SIZE_256,
	},
};

static int ufshcd_program_key(struct ufs_hba *hba,
			      const struct blk_crypto_key *bkey,
			      const union ufs_crypto_cfg_entry *cfg, int slot)
{
	int i;
	u32 slot_offset = hba->crypto_cfg_register + slot * sizeof(*cfg);
	int err = 0;

	ufshcd_hold(hba);

	if (hba->vops && hba->vops->program_key) {
		err = hba->vops->program_key(hba, bkey, cfg, slot);
		goto out;
	}

	/* Ensure that CFGE is cleared before programming the key */
	ufshcd_writel(hba, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));
	for (i = 0; i < 16; i++) {
		ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[i]),
			      slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[17]),
		      slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Dword 16 must be written last */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[16]),
		      slot_offset + 16 * sizeof(cfg->reg_val[0]));
out:
	ufshcd_release(hba);
	return err;
}

static int ufshcd_crypto_keyslot_program(struct blk_crypto_profile *profile,
					 const struct blk_crypto_key *key,
					 unsigned int slot)
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);
	const union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;
	const struct ufs_crypto_alg_entry *alg =
			&ufs_crypto_algs[key->crypto_cfg.crypto_mode];
	u8 data_unit_mask = key->crypto_cfg.data_unit_size / 512;
	int i;
	int cap_idx = -1;
	union ufs_crypto_cfg_entry cfg = {};
	int err;

	BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
	for (i = 0; i < hba->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->ufs_alg &&
		    ccap_array[i].key_size == alg->ufs_key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}

	if (WARN_ON(cap_idx < 0))
		return -EOPNOTSUPP;

	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable = UFS_CRYPTO_CONFIGURATION_ENABLE;

	if (key->crypto_cfg.key_type != BLK_CRYPTO_KEY_TYPE_HW_WRAPPED) {
		if (ccap_array[cap_idx].algorithm_id == UFS_CRYPTO_ALG_AES_XTS) {
			/* In XTS mode, the blk_crypto_key's size is already doubled */
			memcpy(cfg.crypto_key, key->raw, key->size/2);
			memcpy(cfg.crypto_key + UFS_CRYPTO_KEY_MAX_SIZE/2,
			       key->raw + key->size/2, key->size/2);
		} else {
			memcpy(cfg.crypto_key, key->raw, key->size);
		}
	}

	err = ufshcd_program_key(hba, key, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));
	return err;
}

static int ufshcd_clear_keyslot(struct ufs_hba *hba, int slot)
{
	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	union ufs_crypto_cfg_entry cfg = {};

	return ufshcd_program_key(hba, NULL, &cfg, slot);
}

static int ufshcd_crypto_keyslot_evict(struct blk_crypto_profile *profile,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);

	return ufshcd_clear_keyslot(hba, slot);
}

bool ufshcd_crypto_enable(struct ufs_hba *hba)
{
	if (!(hba->caps & UFSHCD_CAP_CRYPTO))
		return false;

	/* Reset might clear all keys, so reprogram all the keys. */
	blk_crypto_reprogram_all_keys(&hba->crypto_profile);
	return true;
}


static int ufshcd_crypto_derive_sw_secret(struct blk_crypto_profile *profile,
				const u8 wrapped_key[], size_t wrapped_key_size,
				u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);

	if (hba->vops && hba->vops->derive_sw_secret)
		return  hba->vops->derive_sw_secret(hba, wrapped_key,
						 wrapped_key_size, sw_secret);

	return -EOPNOTSUPP;
}

static int ufshcd_crypto_generate_key(struct blk_crypto_profile *profile,
				      u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);

	if (hba->vops && hba->vops->generate_key)
		return  hba->vops->generate_key(hba, lt_key);

	return -EOPNOTSUPP;
}

static int ufshcd_crypto_prepare_key(struct blk_crypto_profile *profile,
				     const u8 *lt_key, size_t lt_key_size,
				     u8 eph_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);

	if (hba->vops && hba->vops->prepare_key)
		return  hba->vops->prepare_key(hba, lt_key, lt_key_size, eph_key);

	return -EOPNOTSUPP;
}

static int ufshcd_crypto_import_key(struct blk_crypto_profile *profile,
				    const u8 *imp_key, size_t imp_key_size,
				    u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE])
{
	struct ufs_hba *hba =
		container_of(profile, struct ufs_hba, crypto_profile);

	if (hba->vops && hba->vops->import_key)
		return  hba->vops->import_key(hba, imp_key, imp_key_size, lt_key);

	return -EOPNOTSUPP;
}

static const struct blk_crypto_ll_ops ufshcd_crypto_ops = {
	.keyslot_program	= ufshcd_crypto_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_keyslot_evict,
	.derive_sw_secret	= ufshcd_crypto_derive_sw_secret,
	.generate_key		= ufshcd_crypto_generate_key,
	.prepare_key		= ufshcd_crypto_prepare_key,
	.import_key		= ufshcd_crypto_import_key,
};

static enum blk_crypto_mode_num
ufshcd_find_blk_crypto_mode(union ufs_crypto_cap_entry cap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ufs_crypto_algs); i++) {
		BUILD_BUG_ON(UFS_CRYPTO_KEY_SIZE_INVALID != 0);
		if (ufs_crypto_algs[i].ufs_alg == cap.algorithm_id &&
		    ufs_crypto_algs[i].ufs_key_size == cap.key_size) {
			return i;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * ufshcd_hba_init_crypto_capabilities - Read crypto capabilities, init crypto
 *					 fields in hba
 * @hba: Per adapter instance
 *
 * Return: 0 if crypto was initialized or is not supported, else a -errno value.
 */
int ufshcd_hba_init_crypto_capabilities(struct ufs_hba *hba)
{
	int cap_idx;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;

	/*
	 * Don't use crypto if either the hardware doesn't advertise the
	 * standard crypto capability bit *or* if the vendor specific driver
	 * hasn't advertised that crypto is supported.
	 */
	if (!(hba->capabilities & MASK_CRYPTO_SUPPORT) ||
	    !(hba->caps & UFSHCD_CAP_CRYPTO))
		goto out;

	hba->crypto_capabilities.reg_val =
			cpu_to_le32(ufshcd_readl(hba, REG_UFS_CCAP));
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		devm_kcalloc(hba->dev, hba->crypto_capabilities.num_crypto_cap,
			     sizeof(hba->crypto_cap_array[0]), GFP_KERNEL);
	if (!hba->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	/* The actual number of configurations supported is (CFGC+1) */
	err = devm_blk_crypto_profile_init(
			hba->dev, &hba->crypto_profile,
			hba->crypto_capabilities.config_count + 1);
	if (err)
		goto out;

	hba->crypto_profile.ll_ops = ufshcd_crypto_ops;
	/* UFS only supports 8 bytes for any DUN */
	hba->crypto_profile.max_dun_bytes_supported = 8;
	if (hba->quirks & UFSHCD_QUIRK_USES_WRAPPED_CRYPTO_KEYS)
		hba->crypto_profile.key_types_supported =
				BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;
	else
		hba->crypto_profile.key_types_supported =
				BLK_CRYPTO_KEY_TYPE_STANDARD;

	hba->crypto_profile.dev = hba->dev;

	/*
	 * Cache all the UFS crypto capabilities and advertise the supported
	 * crypto modes and data unit sizes to the block layer.
	 */
	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		hba->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(ufshcd_readl(hba,
						 REG_UFS_CRYPTOCAP +
						 cap_idx * sizeof(__le32)));
		blk_mode_num = ufshcd_find_blk_crypto_mode(
						hba->crypto_cap_array[cap_idx]);
		if (blk_mode_num != BLK_ENCRYPTION_MODE_INVALID)
			hba->crypto_profile.modes_supported[blk_mode_num] |=
				hba->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	return 0;

out:
	/* Indicate that init failed by clearing UFSHCD_CAP_CRYPTO */
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
	return err;
}

/**
 * ufshcd_init_crypto - Initialize crypto hardware
 * @hba: Per adapter instance
 */
void ufshcd_init_crypto(struct ufs_hba *hba)
{
	int slot;

	if (!(hba->caps & UFSHCD_CAP_CRYPTO))
		return;

	/* Clear all keyslots - the number of keyslots is (CFGC + 1) */
	for (slot = 0; slot < hba->crypto_capabilities.config_count + 1; slot++)
		ufshcd_clear_keyslot(hba, slot);
}

void ufshcd_crypto_register(struct ufs_hba *hba, struct request_queue *q)
{
	if (hba->caps & UFSHCD_CAP_CRYPTO)
		blk_crypto_register(&hba->crypto_profile, q);
}
