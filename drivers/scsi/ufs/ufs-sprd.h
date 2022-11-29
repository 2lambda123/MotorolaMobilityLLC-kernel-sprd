/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Unisoc. All rights reserved.
 */

#ifndef _UFS_SPRD_H_
#define _UFS_SPRD_H_

struct ufs_sprd_host {
	struct ufs_hba *hba;
	struct scsi_device *sdev_ufs_rpmb;
	void *ufs_priv_data;

	bool ffu_is_process;

	bool debug_en;
	/* Panic when UFS encounters an error. */
	bool err_panic;

	u32 times_pre_pwr;
	u32 times_pre_compare_fail;
	u32 times_post_pwr;
	u32 times_post_compare_fail;
};

struct syscon_ufs {
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

#define AUTO_H8_IDLE_TIME_10MS 0x1001

#define UFSHCI_VERSION_30	0x00000300 /* 3.0 */
#define UFSHCI_VERSION_21	0x00000210 /* 2.1 */

int ufs_sprd_get_syscon_reg(struct device_node *np,
			    struct syscon_ufs *reg, const char *name);

extern const struct ufs_hba_variant_ops ufs_hba_sprd_ums9620_vops;
extern const struct ufs_hba_variant_ops ufs_hba_sprd_ums9621_vops;
extern const struct ufs_hba_variant_ops ufs_hba_sprd_ums9230_vops;

#endif/* _UFS_SPRD_H_ */
