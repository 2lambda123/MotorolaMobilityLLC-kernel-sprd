// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS Host Controller driver for Unisoc specific extensions
 *
 * Copyright (C) 2022 Unisoc, Inc.
 *
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/nvmem-consumer.h>
#include <linux/reset.h>
#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
#include <linux/sprd_sip_svc.h>
#endif

#include "ufshcd.h"
#include "ufshcd-pltfrm.h"
#include "ufs-sprd.h"
#include "ufs-sprd-qogirn6pro.h"
#include "ufs-sprd-ioctl.h"
#include "ufs-sprd-rpmb.h"
#include "ufs-sprd-bootdevice.h"
#include "ufs-sprd-debug.h"

static int ufs_efuse_calib_data(struct platform_device *pdev,
				const char *cell_name)
{
	struct nvmem_cell *cell;
	void *buf;
	u32 calib_data;
	size_t len;

	if (!pdev)
		return -EINVAL;

	cell = nvmem_cell_get(&pdev->dev, cell_name);
	if (IS_ERR_OR_NULL(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR_OR_NULL(buf)) {
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}

	memcpy(&calib_data, buf, min(len, sizeof(u32)));

	kfree(buf);
	nvmem_cell_put(cell);
	return calib_data;
}

static int ufs_sprd_priv_parse_dt(struct device *dev,
				  struct ufs_hba *hba,
				  struct ufs_sprd_host *host)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;
	int ret = 0;

	priv->ufs_lane_calib_data1 = ufs_efuse_calib_data(pdev,
							  "ufs_cali_lane1");
	if (priv->ufs_lane_calib_data1 == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"%s:get ufs_lane_calib_data1 failed!\n", __func__);
		ret =  -EPROBE_DEFER;
		goto out_variant_clear;
	}

	dev_err(&pdev->dev, "%s: ufs_lane_calib_data1: %x\n",
		__func__, priv->ufs_lane_calib_data1);

	priv->ufs_lane_calib_data0 = ufs_efuse_calib_data(pdev,
							  "ufs_cali_lane0");
	if (priv->ufs_lane_calib_data0 == -EPROBE_DEFER) {
		dev_err(&pdev->dev,
			"%s:get ufs_lane_calib_data1 failed!\n", __func__);
		ret =  -EPROBE_DEFER;
		goto out_variant_clear;
	}

	dev_err(&pdev->dev, "%s: ufs_lane_calib_data0: %x\n",
		__func__, priv->ufs_lane_calib_data0);

	priv->vdd_mphy = devm_regulator_get(dev, "vdd-mphy");
	ret = regulator_enable(priv->vdd_mphy);
	if (ret)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &priv->phy_sram_ext_ld_done,
				      "phy_sram_ext_ld_done");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &priv->phy_sram_bypass,
				      "phy_sram_bypass");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &priv->phy_sram_init_done,
				      "phy_sram_init_done");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &priv->aon_apb_ufs_clk_en,
				      "aon_apb_ufs_clk_en");
	if (ret < 0)
		 return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node, &priv->ufsdev_refclk_en,
				      "ufsdev_refclk_en");
	if (ret < 0)
		return -ENODEV;

	ret = ufs_sprd_get_syscon_reg(dev->of_node,
					&priv->usb31pllv_ref2mphy_en,
				      "usb31pllv_ref2mphy_en");
	if (ret < 0)
		return -ENODEV;

	priv->hclk = devm_clk_get(&pdev->dev, "ufs_hclk");
	if (IS_ERR(priv->hclk)) {
		dev_warn(&pdev->dev,
			 "can't get the clock dts config: ufs_pclk\n");
			 priv->hclk = NULL;
	}

	priv->hclk_source = devm_clk_get(&pdev->dev, "ufs_hclk_source");
	if (IS_ERR(priv->hclk_source)) {
		dev_warn(&pdev->dev,
			 "can't get the clock dts config: ufs_hclk_source\n");
			 priv->hclk_source = NULL;
	}

	clk_set_parent(priv->hclk, priv->hclk_source);

	priv->aon_apb_ufs_rst = devm_reset_control_get(dev, "ufsdev_soft_rst");
	if (IS_ERR(priv->aon_apb_ufs_rst)) {
		dev_err(dev, "%s get ufsdev_soft_rst failed, err%ld\n",
			__func__, PTR_ERR(priv->aon_apb_ufs_rst));
		priv->aon_apb_ufs_rst = NULL;
		return -ENODEV;
	}

	priv->ap_ahb_ufs_rst = devm_reset_control_get(dev, "ufs_soft_rst");
	if (IS_ERR(priv->ap_ahb_ufs_rst)) {
		dev_err(dev, "%s get ufs_soft_rst failed, err%ld\n",
			__func__, PTR_ERR(priv->ap_ahb_ufs_rst));
		priv->ap_ahb_ufs_rst = NULL;
		return -ENODEV;
	}

	return 0;

out_variant_clear:
	return ret;
}

static int ufs_sprd_priv_pre_init(struct device *dev,
				  struct ufs_hba *hba,
				  struct ufs_sprd_host *host)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;
	struct sprd_sip_svc_handle *svc_handle;

	ret = reset_control_assert(priv->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		return -ENODEV;
	}

	usleep_range(1000, 1100);

	ret = reset_control_deassert(priv->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		return -ENODEV;
	}

	ufshcd_writel(hba, CONTROLLER_ENABLE, REG_CONTROLLER_ENABLE);
	if ((ufshcd_readl(hba, REG_UFS_CCAP) & (1 << 27)))
		ufshcd_writel(hba, (CRYPTO_GENERAL_ENABLE | CONTROLLER_ENABLE),
			      REG_CONTROLLER_ENABLE);
	svc_handle = sprd_sip_svc_get_handle();
	if (!svc_handle) {
		pr_err("%s: failed to get svc handle\n", __func__);
		return -ENODEV;
	}

	ret = svc_handle->storage_ops.ufs_crypto_enable();
	pr_err("smc: enable cfg, ret:0x%x", ret);
#endif

	return ret;
}

static int ufs_sprd_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct ufs_sprd_host *host;
	int ret = 0;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->ufs_priv_data = devm_kzalloc(dev,
				 sizeof(struct ufs_sprd_ums9620_data),
				 GFP_KERNEL);
	if (!host->ufs_priv_data)
		return -ENOMEM;

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	hba->caps |= UFSHCD_CAP_CLK_GATING |
		UFSHCD_CAP_CRYPTO |
		UFSHCD_CAP_WB_EN;
	hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION |
		UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS;

	ret = ufs_sprd_priv_parse_dt(dev, hba, host);
	if (ret < 0)
		return ret;

	ret = ufs_sprd_priv_pre_init(dev, hba, host);
	if (ret < 0)
		return ret;

	hba->host->hostt->ioctl = ufshcd_sprd_ioctl;
#ifdef CONFIG_COMPAT
	hba->host->hostt->compat_ioctl = ufshcd_sprd_ioctl;
#endif

	return 0;
}

static void ufs_sprd_exit(struct ufs_hba *hba)
{
	int err = 0;
	struct device *dev = hba->dev;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;

	regmap_update_bits(priv->aon_apb_ufs_clk_en.regmap,
			   priv->aon_apb_ufs_clk_en.reg,
			   priv->aon_apb_ufs_clk_en.mask,
			   0);

	err = regulator_disable(priv->vdd_mphy);
	if (err)
		pr_err("disable vdd_mphy failed ret =0x%x!\n", err);

	devm_kfree(dev, host->ufs_priv_data);
	devm_kfree(dev, host);
	hba->priv = NULL;
}

static u32 ufs_sprd_get_ufs_hci_version(struct ufs_hba *hba)
{
	return UFSHCI_VERSION_30;
}

static int ufs_sprd_hw_init(struct ufs_hba *hba)
{
	int ret;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;

	dev_info(host->hba->dev, "ufs hardware reset!\n");

	regmap_update_bits(priv->phy_sram_ext_ld_done.regmap,
			   priv->phy_sram_ext_ld_done.reg,
			   priv->phy_sram_ext_ld_done.mask,
			   priv->phy_sram_ext_ld_done.mask);

	regmap_update_bits(priv->phy_sram_bypass.regmap,
			   priv->phy_sram_bypass.reg,
			   priv->phy_sram_bypass.mask,
			   priv->phy_sram_bypass.mask);

	ret = reset_control_assert(priv->aon_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufsdev_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	ret = reset_control_assert(priv->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s assert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	usleep_range(1000, 1100);

	ret = reset_control_deassert(priv->aon_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufsdev_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

	ret = reset_control_deassert(priv->ap_ahb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "%s deassert ufs_soft_rst failed, ret = %d!\n",
				__func__, ret);
		goto out;
	}

out:
	return ret;
}

static int ufs_sprd_phy_sram_init_done(struct ufs_hba *hba)
{
	int ret = 0;
	uint32_t val = 0;
	uint32_t retry = 10;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;

	do {
		ret = regmap_read(priv->phy_sram_init_done.regmap,
				  priv->phy_sram_init_done.reg, &val);
		if (ret < 0)
			return ret;

		if ((val&0x1) == 0x1) {
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRLSB), 0x1c);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRMSB), 0x40);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRLSB), 0x04);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRMSB), 0x00);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGRDWRSEL), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRLSB), 0x1c);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGADDRMSB), 0x41);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRLSB), 0x04);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGWRMSB), 0x00);
			ufshcd_dme_set(hba, UIC_ARG_MIB(CBCREGRDWRSEL), 0x01);
			ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);

			return 0;
		} else {
			udelay(1000);
			retry--;
		}
	} while (retry > 0);
		return -1;
}

static int ufs_sprd_phy_init(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;

	ufshcd_dme_set(hba, UIC_ARG_MIB(CBREFCLKCTRL2), 0x90);
	ufshcd_dme_set(hba, UIC_ARG_MIB(CBCRCTRL), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(RXSQCONTROL,
		       UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(RXSQCONTROL,
		       UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(CBRATESEL), 0x01);

	ret = ufs_sprd_phy_sram_init_done(hba);
	if (ret)
		return ret;

	regmap_update_bits(priv->phy_sram_ext_ld_done.regmap,
			   priv->phy_sram_ext_ld_done.reg,
			   priv->phy_sram_ext_ld_done.mask,
			   0);

	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data0 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x40);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data0 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data1 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x41);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data1 >> 24) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data0 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x02);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x10);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x40);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data0 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xaf);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data1 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb1);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x02);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb8);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0xb0);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x11);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8116), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8117), 0x41);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8118),
		       (priv->ufs_lane_calib_data1 >> 16) & 0xff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x8119), 0x00);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0x811c), 0x01);
	ufshcd_dme_set(hba, UIC_ARG_MIB(0xd085), 0x01);

	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYDISABLE), 0x0);

	return ret;
}

static int ufs_sprd_hce_enable_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status)
{
	int err = 0;
	int ret = 0;
	struct sprd_sip_svc_handle *svc_handle;

	switch (status) {
	case PRE_CHANGE:
		/* Do hardware reset before host controller enable. */
		err = ufs_sprd_hw_init(hba);
		if (err) {
			dev_err(hba->dev, "%s: ufs hardware init failed!\n", __func__);
			return err;
		}

#if IS_ENABLED(CONFIG_SCSI_UFS_CRYPTO)
		ufshcd_writel(hba, CONTROLLER_ENABLE, REG_CONTROLLER_ENABLE);
		svc_handle = sprd_sip_svc_get_handle();
		if (!svc_handle) {
			pr_err("%s: failed to get svc handle\n", __func__);
			return -ENODEV;
		}

		ret = svc_handle->storage_ops.ufs_crypto_enable();
		pr_err("smc: enable cfg, ret:0x%x", ret);
#endif
		break;
	case POST_CHANGE:
		err = ufs_sprd_phy_init(hba);
		if (err)
			dev_err(hba->dev, "Phy setup failed (%d)\n", err);
		break;
	default:
		dev_err(hba->dev, "%s: invalid status %d\n", __func__, status);
		err = -EINVAL;
		break;
	}

	return err;
}

static int ufs_sprd_pwr_change_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status,
				      struct ufs_pa_layer_attr *dev_max_params,
				      struct ufs_pa_layer_attr *dev_req_params)
{
	int err = 0;

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		err = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		memcpy(dev_req_params, dev_max_params,
		       sizeof(struct ufs_pa_layer_attr));
		if (dev_req_params->gear_rx == UFS_HS_G4)
			ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TXHSADAPTTYPE), 0x0);
		break;
	case POST_CHANGE:
		if (ufshcd_is_auto_hibern8_supported(hba))
			hba->ahit = AUTO_H8_IDLE_TIME_10MS;
		break;
	default:
		err = -EINVAL;
		break;
	}

out:
	return err;
}

static void ufs_sprd_hibern8_notify(struct ufs_hba *hba,
				    enum uic_cmd_dme cmd,
				    enum ufs_notify_change_status status)
{
	u32 set;
	unsigned long flags;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct ufs_sprd_ums9620_data *priv =
		(struct ufs_sprd_ums9620_data *) host->ufs_priv_data;

	switch (status) {
	case PRE_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
			set &= ~UIC_COMMAND_COMPL;
			ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}

		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			regmap_update_bits(priv->ufsdev_refclk_en.regmap,
				   priv->ufsdev_refclk_en.reg,
				   priv->ufsdev_refclk_en.mask,
				   priv->ufsdev_refclk_en.mask);

			regmap_update_bits(priv->usb31pllv_ref2mphy_en.regmap,
				   priv->usb31pllv_ref2mphy_en.reg,
				   priv->usb31pllv_ref2mphy_en.mask,
				   priv->usb31pllv_ref2mphy_en.mask);
		}
		break;
	case POST_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
			set |= UIC_COMMAND_COMPL;
			ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}

		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			regmap_update_bits(priv->ufsdev_refclk_en.regmap,
					   priv->ufsdev_refclk_en.reg,
					   priv->ufsdev_refclk_en.mask,
					   0);

			regmap_update_bits(priv->usb31pllv_ref2mphy_en.regmap,
					   priv->usb31pllv_ref2mphy_en.reg,
					   priv->usb31pllv_ref2mphy_en.mask,
					   0);
		}
		break;
	default:
		break;
	}
}

static int ufs_sprd_device_reset(struct ufs_hba *hba)
{
	if (sprd_ufs_debug_is_supported(hba) == TRUE)
		ufshcd_common_trace(hba, UFS_TRACE_RESET_AND_RESTORE, NULL);

	return 0;
}

static void ufs_sprd_fixup_dev_quirks(struct ufs_hba *hba)
{
	/* vendor UFS UID info decode. */
	ufshcd_decode_ufs_uid(hba);
}

static int ufs_sprd_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op,
					enum ufs_notify_change_status status)
{
	unsigned long flags;

	/* disable auto h8 before ssu */
	if (status == PRE_CHANGE) {
		if (ufshcd_is_auto_hibern8_supported(hba)) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			ufshcd_writel(hba, 0, REG_AUTO_HIBERNATE_IDLE_TIMER);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}
	}

	return 0;
}

static void ufs_sprd_dbg_register_dump(struct ufs_hba *hba)
{
	sprd_ufs_debug_err_dump(hba);
}

const struct ufs_hba_variant_ops ufs_hba_sprd_ums9620_vops = {
	.name = "sprd,ufshc-ums9620",
	.init = ufs_sprd_init,
	.exit = ufs_sprd_exit,
	.get_ufs_hci_version = ufs_sprd_get_ufs_hci_version,
	.hce_enable_notify = ufs_sprd_hce_enable_notify,
	.pwr_change_notify = ufs_sprd_pwr_change_notify,
	.hibern8_notify = ufs_sprd_hibern8_notify,
	.fixup_dev_quirks = ufs_sprd_fixup_dev_quirks,
	.dbg_register_dump = ufs_sprd_dbg_register_dump,
	.device_reset = ufs_sprd_device_reset,
	.suspend = ufs_sprd_suspend,
};
EXPORT_SYMBOL(ufs_hba_sprd_ums9620_vops);
