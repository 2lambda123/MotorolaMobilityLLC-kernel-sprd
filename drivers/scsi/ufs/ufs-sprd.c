// SPDX-License-Identifier: GPL-2.0-only
/*
 * UFS Host Controller driver for Unisoc specific extensions
 *
 * Copyright (C) 2022 Unisoc, Inc.
 *
 */

#include <linux/irqreturn.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <trace/hooks/ufshcd.h>
#include <linux/regulator/driver.h>
#include <../drivers/regulator/internal.h>

#include "ufs.h"
#include "ufshcd.h"
#include "ufshcd-pltfrm.h"
#include "ufs-sprd.h"
#include "ufs-sprd-ioctl.h"
#include "ufs-sprd-rpmb.h"
#include "ufs-sprd-bootdevice.h"
#include "ufs-sprd-debug.h"
#include "ufshpb.h"

int ufs_sprd_get_syscon_reg(struct device_node *np, struct syscon_ufs *reg,
			    const char *name)
{
	struct regmap *regmap;
	u32 syscon_args[2];

	regmap = syscon_regmap_lookup_by_phandle_args(np, name, 2, syscon_args);
	if (IS_ERR(regmap)) {
		pr_err("read ufs syscon %s regmap fail\n", name);
		reg->regmap = NULL;
		reg->reg = 0x0;
		reg->mask = 0x0;
		return -EINVAL;
	}
	reg->regmap = regmap;
	reg->reg = syscon_args[0];
	reg->mask = syscon_args[1];

	return 0;
}

static void ufs_sprd_vh_prepare_command(void *data, struct ufs_hba *hba,
					struct request *rq,
					struct ufshcd_lrb *lrbp,
					int *err)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (unlikely(host->ffu_is_process == TRUE))
		prepare_command_send_in_ffu_state(hba, lrbp, err);

	return;
}

static void ufs_sprd_vh_update_sdev(void *data, struct scsi_device *sdev)
{
	/* Disable UFS fua to prevent write performance degradation */
	sdev->broken_fua = 1;
}

static void ufs_sprd_vh_send_uic_cmd(void *data, struct ufs_hba *hba,
				     struct uic_command *ucmd, int str)
{
	struct ufs_uic_cmd_info uic_tmp = {};

	if (sprd_ufs_debug_is_supported(hba) == TRUE) {
		uic_tmp.argu1 = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_1);
		uic_tmp.argu2 = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2);
		uic_tmp.argu3 = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3);
		if (str == UFS_CMD_SEND) {
			uic_tmp.cmd = ucmd->command;
			ufshcd_common_trace(hba, UFS_TRACE_UIC_SEND, &uic_tmp);
		} else {
			uic_tmp.cmd = ufshcd_readl(hba, REG_UIC_COMMAND);
			ufshcd_common_trace(hba, UFS_TRACE_UIC_CMPL, &uic_tmp);
		}
	}
}

static void ufs_sprd_vh_compl_cmd(void *data,
				  struct ufs_hba *hba,
				  struct ufshcd_lrb *lrbp)
{
	if (sprd_ufs_debug_is_supported(hba) == TRUE) {
		if (lrbp->cmd)
			ufshcd_transfer_event_trace(hba, UFS_TRACE_COMPLETED,
							 lrbp->task_tag);
		else if (lrbp->command_type == UTP_CMD_TYPE_DEV_MANAGE ||
			 lrbp->command_type == UTP_CMD_TYPE_UFS_STORAGE)
			ufshcd_transfer_event_trace(hba, UFS_TRACE_DEV_COMPLETED,
							 lrbp->task_tag);
	}
}

static void ufs_sprd_vh_send_tm_cmd(void *data, struct ufs_hba *hba,
				    int tag, int str)
{
	struct utp_task_req_desc *descp = &hba->utmrdl_base_addr[tag];
	struct ufs_tm_cmd_info tm_tmp = {};

	if (sprd_ufs_debug_is_supported(hba) == TRUE) {
		tm_tmp.tm_func = (u8) (__be32_to_cpu(descp->header.dword_1) >> 16);
		if (str == UFS_TM_SEND) {
			tm_tmp.param1 = descp->upiu_req.input_param1;
			tm_tmp.param2 = descp->upiu_req.input_param2;
			ufshcd_common_trace(hba, UFS_TRACE_TM_SEND, &tm_tmp);
		} else {
			tm_tmp.ocs = le32_to_cpu(descp->header.dword_2) & MASK_OCS;
			tm_tmp.param1 = descp->upiu_rsp.output_param1;
			tm_tmp.param2 = 0;
			if (str == UFS_TM_ERR)
				ufshcd_common_trace(hba, UFS_TRACE_TM_ERR, &tm_tmp);
			else
				ufshcd_common_trace(hba, UFS_TRACE_TM_COMPLETED, &tm_tmp);
		}
	}
}

static void ufs_sprd_vh_check_int_errors(void *data,
					struct ufs_hba *hba,
					bool queue_eh_work)
{
	if (queue_eh_work && sprd_ufs_debug_is_supported(hba) == TRUE)
		ufshcd_common_trace(hba, UFS_TRACE_INT_ERROR, NULL);
}

static void ufs_sprd_vh_update_sysfs(void *data,
					struct ufs_hba *hba)
{
		ufs_sprd_sysfs_add_nodes(hba);
}

static void ufs_sprd_vh_send_cmd(void *data,
				  struct ufs_hba *hba,
				  struct ufshcd_lrb *lrbp)
{
	struct utp_transfer_req_desc *req_desc;
	u32 data_direction;
	u32 dword_0, crypto;
	unsigned char *cdb = NULL;
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct utp_upiu_req *ucd_req_ptr = lrbp->ucd_req_ptr;
	unsigned short cdb_len;

	req_desc = lrbp->utr_descriptor_ptr;
	dword_0 = le32_to_cpu(req_desc->header.dword_0);
	data_direction = dword_0 & (UTP_DEVICE_TO_HOST | UTP_HOST_TO_DEVICE);
	crypto = dword_0 & UTP_REQ_DESC_CRYPTO_ENABLE_CMD;
	if (!data_direction && crypto) {
		dword_0 &= ~(UTP_REQ_DESC_CRYPTO_ENABLE_CMD);
		req_desc->header.dword_0 = cpu_to_le32(dword_0);
	}

	if (lrbp->cmd) {
		cdb = lrbp->cmd->cmnd;
		if (cdb && cdb[0] == UFSHPB_READ) {
			cdb[0] = READ_16;
			cdb[15] = cdb[14];
			cdb[14] = 0;
			cdb[1] = 0x0;
			cdb_len = min_t(unsigned short, cmd->cmd_len, UFS_CDB_SIZE);
			memset(ucd_req_ptr->sc.cdb, 0, UFS_CDB_SIZE);
			memcpy(ucd_req_ptr->sc.cdb, cmd->cmnd, cdb_len);
		}
	}

	if (sprd_ufs_debug_is_supported(hba) == TRUE) {
		if (!!lrbp->cmd)
			ufshcd_transfer_event_trace(hba, UFS_TRACE_SEND, lrbp->task_tag);
		else
			ufshcd_transfer_event_trace(hba, UFS_TRACE_DEV_SEND, lrbp->task_tag);
	}
}


static const struct of_device_id ufs_sprd_of_match[] = {
	{ .compatible = "sprd,ufshc-ums9620", .data = &ufs_hba_sprd_ums9620_vops },
	{ .compatible = "sprd,ufshc-ums9621", .data = &ufs_hba_sprd_ums9621_vops },
	{ .compatible = "sprd,ufshc-ums9230", .data = &ufs_hba_sprd_ums9230_vops },
	{},
};

static int ufs_sprd_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	struct ufs_hba *hba;
	const struct of_device_id *of_id;

	register_trace_android_vh_ufs_prepare_command(ufs_sprd_vh_prepare_command, NULL);
	register_trace_android_vh_ufs_update_sdev(ufs_sprd_vh_update_sdev, NULL);
	register_trace_android_vh_ufs_send_uic_command(ufs_sprd_vh_send_uic_cmd, NULL);
	register_trace_android_vh_ufs_compl_command(ufs_sprd_vh_compl_cmd, NULL);
	register_trace_android_vh_ufs_send_tm_command(ufs_sprd_vh_send_tm_cmd, NULL);
	register_trace_android_vh_ufs_check_int_errors(ufs_sprd_vh_check_int_errors, NULL);
	register_trace_android_vh_ufs_send_command(ufs_sprd_vh_send_cmd, NULL);
	register_trace_android_vh_ufs_update_sysfs(ufs_sprd_vh_update_sysfs, NULL);

	/* Perform generic probe */
	of_id = of_match_node(ufs_sprd_of_match, pdev->dev.of_node);
	err = ufshcd_pltfrm_init(pdev, of_id->data);
	if (err) {
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);
		goto out;
	}

	hba = platform_get_drvdata(pdev);
	ufs_sprd_rpmb_add(hba);
	sprd_ufs_proc_init(hba);
	ufs_sprd_debug_init(hba);
out:
	return err;
}

static void ufs_sprd_shutdown(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	sprd_ufs_proc_exit();
	ufs_sprd_rpmb_remove(hba);
	ufshcd_pltfrm_shutdown(pdev);
}

static int ufs_sprd_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	sprd_ufs_proc_exit();
	ufs_sprd_rpmb_remove(hba);
	ufshcd_remove(hba);
	return 0;
}

static int ufs_sprd_system_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	bool vcc_aon = hba->vreg_info.vcc->reg->always_on;
	int ret;

	ret = ufshcd_system_suspend(dev);
	if (ret)
		return ret;

	/* Makesure that VCC drop below 0.1V after turning off */
	if (!vcc_aon && hba->vreg_info.vcc->enabled == false)
		usleep_range(30000, 31000);

	return 0;
}

static int ufs_sprd_system_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	bool vcc_state = hba->vreg_info.vcc->enabled;
	bool vcc_aon = hba->vreg_info.vcc->reg->always_on;
	int ret;

	ret = ufshcd_system_resume(dev);
	if (ret)
		return ret;

	/* Makesure UFS VCC power up */
	if (!vcc_aon && vcc_state == false && hba->vreg_info.vcc->enabled == true)
		usleep_range(100, 200);

	return 0;
}

static int ufs_sprd_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	bool vcc_aon = hba->vreg_info.vcc->reg->always_on;
	int ret;

	ret = ufshcd_runtime_suspend(dev);
	if (ret)
		return ret;

	/* Makesure that VCC drop below 0.1V after turning off */
	if (!vcc_aon && hba->vreg_info.vcc->enabled == false)
		usleep_range(30000, 31000);

	return 0;
}

static int ufs_sprd_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	bool vcc_state = hba->vreg_info.vcc->enabled;
	bool vcc_aon = hba->vreg_info.vcc->reg->always_on;
	int ret;

	ret = ufshcd_runtime_resume(dev);
	if (ret)
		return ret;

	/* Makesure UFS VCC power up */
	if (!vcc_aon && vcc_state == false && hba->vreg_info.vcc->enabled == true)
		usleep_range(100, 200);

	return 0;
}

static const struct dev_pm_ops ufs_sprd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufs_sprd_system_suspend, ufs_sprd_system_resume)
	SET_RUNTIME_PM_OPS(ufs_sprd_runtime_suspend, ufs_sprd_runtime_resume, NULL)
	.prepare	 = ufshcd_suspend_prepare,
	.complete	 = ufshcd_resume_complete,
};

static struct platform_driver ufs_sprd_pltform = {
	.probe = ufs_sprd_probe,
	.remove = ufs_sprd_remove,
	.shutdown = ufs_sprd_shutdown,
	.driver = {
		.name = "ufshcd-sprd",
		.pm = &ufs_sprd_pm_ops,
		.of_match_table = of_match_ptr(ufs_sprd_of_match),
	},
};
module_platform_driver(ufs_sprd_pltform);

MODULE_DESCRIPTION("SPRD Specific UFSHCI driver");
MODULE_LICENSE("GPL v2");
