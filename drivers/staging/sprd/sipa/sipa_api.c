/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#include <linux/sipa.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/cdev.h>

#include "sipa_api.h"
#include "sipa_priv.h"
#include "sipa_hal.h"

#ifdef CONFIG_SIPA_TEST
#include "test/sipa_test.h"
#endif

#define DRV_NAME "sipa"
#define DRV_LOCAL_NAME "local_ipa"
#define DRV_REMOTE_NAME "remote_ipa"

#define IPA_TEST	0

static const int s_ep_src_term_map[SIPA_EP_MAX] = {
	SIPA_TERM_USB,
	SIPA_TERM_AP_IP,
	SIPA_TERM_AP_ETH,
	SIPA_TERM_VCP,
	SIPA_TERM_PCIE0,
	SIPA_TERM_PCIE_LOCAL_CTRL0,
	SIPA_TERM_PCIE_LOCAL_CTRL1,
	SIPA_TERM_PCIE_LOCAL_CTRL2,
	SIPA_TERM_PCIE_LOCAL_CTRL3,
	SIPA_TERM_PCIE_REMOTE_CTRL0,
	SIPA_TERM_PCIE_REMOTE_CTRL1,
	SIPA_TERM_PCIE_REMOTE_CTRL2,
	SIPA_TERM_PCIE_REMOTE_CTRL3,
	SIPA_TERM_SDIO0,
	SIPA_TERM_WIFI
};

struct sipa_common_fifo_info sipa_common_fifo_statics[SIPA_FIFO_MAX] = {
	{
		.tx_fifo = "sprd,usb-ul-tx",
		.rx_fifo = "sprd,usb-ul-rx",
		.relate_ep = SIPA_EP_USB,
		.src_id = SIPA_TERM_USB,
		.dst_id = SIPA_TERM_AP_ETH,
		.is_to_ipa = 1,
		.is_pam = 1,
	},
	{
		.tx_fifo = "sprd,sdio-ul-tx",
		.rx_fifo = "sprd,sdio-ul-rx",
		.relate_ep = SIPA_EP_SDIO,
		.src_id = SIPA_TERM_SDIO0,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,ap-ip-ul-tx",
		.rx_fifo = "sprd,ap-ip-ul-rx",
		.relate_ep = SIPA_EP_AP_IP,
		.src_id = SIPA_TERM_AP_IP,
		.dst_id = SIPA_TERM_VAP0,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,pcie-ul-tx",
		.rx_fifo = "sprd,pcie-ul-rx",
		.relate_ep = SIPA_EP_PCIE,
		.src_id = SIPA_TERM_PCIE0,
		.dst_id = SIPA_TERM_VAP0,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie0-ul-tx",
		.rx_fifo = "sprd,remote-pcie0-ul-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL0,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL0,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie1-ul-tx",
		.rx_fifo = "sprd,remote-pcie1-ul-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL1,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL1,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie2-ul-tx",
		.rx_fifo = "sprd,remote-pcie2-ul-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL2,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL2,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie3-ul-tx",
		.rx_fifo = "sprd,remote-pcie3-ul-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL3,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL3,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,ap-eth-dl-tx",
		.rx_fifo = "sprd,ap-eth-dl-rx",
		.relate_ep = SIPA_EP_AP_ETH,
		.src_id = SIPA_TERM_AP_ETH,
		.dst_id = SIPA_TERM_USB,
		.is_to_ipa = 1,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie0-dl-tx",
		.rx_fifo = "sprd,local-pcie0-dl-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL0,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL0,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie1-dl-tx",
		.rx_fifo = "sprd,local-pcie1-dl-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL1,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL1,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie2-dl-tx",
		.rx_fifo = "sprd,local-pcie2-dl-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL2,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL2,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie3-dl-tx",
		.rx_fifo = "sprd,local-pcie3-dl-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL3,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL3,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,wifi-ul-tx",
		.rx_fifo = "sprd,wifi-ul-rx",
		.relate_ep = SIPA_EP_WIFI,
		.src_id = SIPA_TERM_WIFI,
		.dst_id = SIPA_TERM_AP_ETH,
		.is_to_ipa = 0,
		.is_pam = 1,
	},
	{
		.tx_fifo = "sprd,cp-dl-tx",
		.rx_fifo = "sprd,cp-dl-rx",
		.relate_ep = SIPA_EP_VCP,
		.src_id = SIPA_TERM_VAP0,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 1,
		.is_pam = 1,
	},
	{
		.tx_fifo = "sprd,usb-dl-tx",
		.rx_fifo = "sprd,usb-dl-rx",
		.relate_ep = SIPA_EP_USB,
		.src_id = SIPA_TERM_USB,
		.dst_id = SIPA_TERM_AP_ETH,
		.is_to_ipa = 0,
		.is_pam = 1,
	},
	{
		.tx_fifo = "sprd,sdio-dl-tx",
		.rx_fifo = "sprd,sdio-dl-rx",
		.relate_ep = SIPA_EP_SDIO,
		.src_id = SIPA_TERM_SDIO0,
		.dst_id = SIPA_TERM_AP_ETH,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,ap-ip-dl-tx",
		.rx_fifo = "sprd,ap-ip-dl-rx",
		.relate_ep = SIPA_EP_AP_IP,
		.src_id = SIPA_TERM_AP_IP,
		.dst_id = SIPA_TERM_VAP0,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,pcie-dl-tx",
		.rx_fifo = "sprd,pcie-dl-rx",
		.relate_ep = SIPA_EP_PCIE,
		.src_id = SIPA_TERM_AP_ETH,
		.dst_id = SIPA_TERM_USB,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie0-dl-tx",
		.rx_fifo = "sprd,remote-pcie0-dl-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL0,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL0,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie1-dl-tx",
		.rx_fifo = "sprd,remote-pcie1-dl-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL1,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL1,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie2-dl-tx",
		.rx_fifo = "sprd,remote-pcie2-dl-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL2,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL2,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,remote-pcie3-dl-tx",
		.rx_fifo = "sprd,remote-pcie3-dl-rx",
		.relate_ep = SIPA_EP_REMOTE_PCIE_CTRL3,
		.src_id = SIPA_TERM_PCIE_REMOTE_CTRL3,
		.dst_id = SIPA_TERM_AP_IP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,ap-eth-ul-tx",
		.rx_fifo = "sprd,ap-eth-ul-rx",
		.relate_ep = SIPA_EP_AP_ETH,
		.src_id = SIPA_TERM_AP_ETH,
		.dst_id = SIPA_TERM_USB,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie0-ul-tx",
		.rx_fifo = "sprd,local-pcie0-ul-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL0,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL0,
		.dst_id = SIPA_TERM_VCP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie1-ul-tx",
		.rx_fifo = "sprd,local-pcie1-ul-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL1,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL1,
		.dst_id = SIPA_TERM_VCP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie2-ul-tx",
		.rx_fifo = "sprd,local-pcie2-ul-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL2,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL2,
		.dst_id = SIPA_TERM_VCP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,local-pcie3-ul-tx",
		.rx_fifo = "sprd,local-pcie3-ul-rx",
		.relate_ep = SIPA_EP_PCIE_CTRL3,
		.src_id = SIPA_TERM_PCIE_LOCAL_CTRL3,
		.dst_id = SIPA_TERM_VCP,
		.is_to_ipa = 0,
		.is_pam = 0,
	},
	{
		.tx_fifo = "sprd,wifi-dl-tx",
		.rx_fifo = "sprd,wifi-dl-rx",
		.relate_ep = SIPA_EP_WIFI,
		.src_id = SIPA_TERM_WIFI,
		.dst_id = SIPA_TERM_AP_ETH,
		.is_to_ipa = 1,
		.is_pam = 1,
	},
	{
		.tx_fifo = "sprd,cp-ul-tx",
		.rx_fifo = "sprd,cp-ul-rx",
		.relate_ep = SIPA_EP_VCP,
		.src_id = SIPA_TERM_VAP0,
		.dst_id = SIPA_TERM_VCP,
		.is_to_ipa = 0,
		.is_pam = 1,
	},
};

struct sipa_control s_sipa_ctrl;
static struct sipa_plat_drv_cfg s_sipa_cfg[2];

static const struct file_operations sipa_local_drv_fops = {
	.owner = THIS_MODULE,
	.open = NULL,
	.read = NULL,
	.write = NULL,
	.unlocked_ioctl = NULL,
#ifdef CONFIG_COMPAT
	.compat_ioctl = NULL,
#endif
};

static const struct file_operations sipa_remote_drv_fops = {
	.owner = THIS_MODULE,
	.open = NULL,
	.read = NULL,
	.write = NULL,
	.unlocked_ioctl = NULL,
#ifdef CONFIG_COMPAT
	.compat_ioctl = NULL,
#endif
};

/**
 * ipa_stop_gsi_channel()- Stops a GSI channel in IPA
 *
 * Return value: 0 on success, negative otherwise
 */
int sipa_pam_connect(const struct sipa_connect_params *in,
					 struct sipa_to_pam_info *out)
{
	u32 i;
	struct sipa_hal_fifo_item fifo_item;
	struct sipa_endpoint *ep = s_sipa_ctrl.eps[in->id];

	if (!ep) {
		pr_err("sipa_pam_connect: ep id:%d not create!", in->id);
		return -ENODEV;
	}

	ep->send_notify = in->send_notify;
	ep->recv_notify = in->recv_notify;
	ep->send_priv = in->send_priv;
	ep->recv_priv = in->recv_priv;
	ep->connected = true;
	memcpy(&ep->send_fifo_param, &in->send_param,
		   sizeof(struct sipa_comm_fifo_params));
	memcpy(&ep->recv_fifo_param, &in->recv_param,
		   sizeof(struct sipa_comm_fifo_params));

	sipa_open_common_fifo(ep->sipa_ctx->hdl, ep->send_fifo.idx,
						  &ep->send_fifo_param,
						  false,
						  (sipa_hal_notify_cb)ep->send_notify, ep);
	sipa_open_common_fifo(ep->sipa_ctx->hdl, ep->recv_fifo.idx,
						  &ep->recv_fifo_param,
						  false,
						  (sipa_hal_notify_cb)ep->recv_notify, ep);

	if (ep->send_fifo_param.data_ptr) {
		for (i = 0; i < ep->send_fifo_param.data_ptr_cnt; i++) {
			fifo_item.addr = ep->send_fifo_param.data_ptr[i];
			fifo_item.len = ep->send_fifo_param.buf_size;
			sipa_hal_init_set_tx_fifo(ep->sipa_ctx->hdl,
									  ep->send_fifo.idx, &fifo_item, 1);
		}
	}

	sipa_hal_init_pam_param(ep->sipa_ctx->is_remote,
							ep->send_fifo.idx, ep->recv_fifo.idx, out);

	return 0;
}
EXPORT_SYMBOL(sipa_pam_connect);

int sipa_pam_init_free_fifo(enum sipa_ep_id id,
							const dma_addr_t *addr, u32 num)
{
	u32 i;
	struct sipa_hal_fifo_item iterms;
	struct sipa_endpoint *ep = s_sipa_ctrl.eps[id];

	for (i = 0; i < num; i++) {
		iterms.addr = addr[i];
		sipa_hal_init_set_tx_fifo(ep->sipa_ctx->hdl,
								  ep->recv_fifo.idx, &iterms, 1);
	}

	return 0;
}
EXPORT_SYMBOL(sipa_pam_init_free_fifo);

int sipa_sw_connect(const struct sipa_connect_params *in)
{
	return 0;
}
EXPORT_SYMBOL(sipa_sw_connect);

int sipa_disconnect(enum sipa_ep_id ep_id)
{
	struct sipa_endpoint *ep = s_sipa_ctrl.eps[ep_id];

	if (!ep) {
		pr_err("sipa_disconnect: ep id:%d not create!", ep_id);
		return -ENODEV;
	}

	ep->connected = false;
	ep->send_notify = NULL;
	ep->send_priv = 0;
	ep->recv_notify = NULL;
	ep->recv_priv = 0;

	return 0;
}
EXPORT_SYMBOL(sipa_disconnect);

int sipa_enable_receive(enum sipa_ep_id ep_id, bool enabled)
{
	struct sipa_endpoint *ep = s_sipa_ctrl.eps[ep_id];

	if (!ep) {
		pr_err("sipa_disconnect: ep id:%d not create!", ep_id);
		return -ENODEV;
	}

	sipa_hal_cmn_fifo_set_receive(ep->sipa_ctx->hdl, ep->recv_fifo.idx,
								  !enabled);

	return 0;
}
EXPORT_SYMBOL(sipa_enable_receive);

static int sipa_parse_dts_configuration(
	struct platform_device *pdev,
	struct sipa_plat_drv_cfg *cfg)
{
	int i, ret;
	u32 fifo_info[2];
	struct resource *resource;

	/* get IPA global register base  address */
	resource = platform_get_resource_byname(pdev,
											IORESOURCE_MEM,
											"glb-base");
	if (!resource) {
		pr_err("%s :get resource failed for glb-base!\n",
			   __func__);
		return -ENODEV;
	}
	memcpy(&cfg->phy_virt_res.glb_res,
		   resource,
		   sizeof(struct resource));
	cfg->phy_virt_res.glb_base = devm_ioremap_nocache(
									 &pdev->dev, resource->start, resource_size(resource));

	if (!cfg->is_remote) {
		/* get IPA sys register base  address */
		resource = platform_get_resource_byname(pdev,
												IORESOURCE_MEM,
												"ipa-sys");
		if (!resource) {
			pr_err("%s :get resource failed for glb-base!\n",
				   __func__);
			return -ENODEV;
		}
		memcpy(&cfg->phy_virt_res.sys_res,
			   resource,
			   sizeof(struct resource));
		cfg->phy_virt_res.sys_base = devm_ioremap_nocache(
										 &pdev->dev, resource->start, resource_size(resource));

		/* get IPA iram base  address */
		resource = platform_get_resource_byname(pdev,
												IORESOURCE_MEM,
												"iram-base");
		if (!resource) {
			pr_err("%s :get resource failed for iram-base!\n", __func__);
			return -ENODEV;
		}
		memcpy(&cfg->phy_virt_res.iram_res,
			   resource,
			   sizeof(struct resource));
		cfg->phy_virt_res.iram_base = devm_ioremap_nocache(
										  &pdev->dev, resource->start, resource_size(resource));
	}

	/* get IRQ numbers */
	if (cfg->is_remote) {

		/* ctrl0-tx */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl0-tx");

		cfg->ctrl_tx_intr0 = resource->start;

		/* ctrl0-flow */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl0-flow");

		cfg->ctrl_flowctrl_intr0 = resource->start;

		/* ctrl1-tx */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl1-tx");

		cfg->ctrl_tx_intr1 = resource->start;

		/* ctrl1-flow */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl1-flow");

		cfg->ctrl_flowctrl_intr1 = resource->start;

		/* ctrl2-tx */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl2-tx");

		cfg->ctrl_tx_intr2 = resource->start;

		/* ctrl2-flow */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl2-flow");

		cfg->ctrl_flowctrl_intr2 = resource->start;

		/* ctrl3-tx */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl3-tx");

		cfg->ctrl_tx_intr3 = resource->start;

		/* ctrl3-flow */
		resource = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
												"ctrl3-flow");

		cfg->ctrl_flowctrl_intr3 = resource->start;
	} else {
		cfg->ipa_intr = platform_get_irq_byname(pdev, "local_ipa_irq");
		if (cfg->ipa_intr == -ENXIO) {
			pr_err("%s :get ipa-irq fail!\n",
				   __func__);
			return -ENODEV;
		}
		pr_info("ipa intr num = %d\n", cfg->ipa_intr);
	}

	/* get IPA bypass mode */
	ret = of_property_read_u32(pdev->dev.of_node,
							   "sprd,sipa-bypass-mode",
							   &cfg->is_bypass);
	if (ret)
		pr_debug("%s :using non-bypass mode by default\n",
				 __func__);
	else
		pr_debug("%s : using bypass mode =%d", __func__,
				 cfg->is_bypass);

	/* get IPA fifo memory settings */
	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		/* free fifo info */
		ret = of_property_read_u32_array(pdev->dev.of_node,
										 sipa_common_fifo_statics[i].tx_fifo,
										 (u32 *)fifo_info,
										 2);
		if (!ret) {
			cfg->common_fifo_cfg[i].tx_fifo.in_iram =
				fifo_info[0];
			cfg->common_fifo_cfg[i].tx_fifo.fifo_size =
				fifo_info[1];
		}
		/* filled fifo info */
		ret = of_property_read_u32_array(pdev->dev.of_node,
										 sipa_common_fifo_statics[i].rx_fifo,
										 (u32 *)fifo_info,
										 2);
		if (!ret) {
			cfg->common_fifo_cfg[i].rx_fifo.in_iram =
				fifo_info[0];
			cfg->common_fifo_cfg[i].rx_fifo.fifo_size =
				fifo_info[1];
		}
		if (sipa_common_fifo_statics[i].is_to_ipa)
			cfg->common_fifo_cfg[i].is_recv = false;
		else
			cfg->common_fifo_cfg[i].is_recv = true;

		cfg->common_fifo_cfg[i].src =
			sipa_common_fifo_statics[i].src_id;
		cfg->common_fifo_cfg[i].dst =
			sipa_common_fifo_statics[i].dst_id;
		cfg->common_fifo_cfg[i].is_pam =
			sipa_common_fifo_statics[i].is_pam;
	}

	return 0;
}

static int ipa_pre_init(struct sipa_plat_drv_cfg *cfg)
{
	int ret;

	if (cfg->is_remote)
		cfg->name = DRV_REMOTE_NAME;
	else
		cfg->name = DRV_LOCAL_NAME;

	cfg->class = class_create(THIS_MODULE, cfg->name);
	ret = alloc_chrdev_region(&cfg->dev_num, 0, 1, cfg->name);
	if (ret) {
		pr_err("ipa alloc chr dev region err\n");
		return -1;
	}

	if (cfg->is_remote) {
		cfg->dev = device_create(cfg->class, NULL, cfg->dev_num,
								 cfg, DRV_REMOTE_NAME);
		cdev_init(&cfg->cdev, &sipa_remote_drv_fops);
		cfg->cdev.owner = THIS_MODULE;
		cfg->cdev.ops = &sipa_remote_drv_fops;
	} else {
		cfg->dev = device_create(cfg->class, NULL, cfg->dev_num,
								 cfg, DRV_LOCAL_NAME);
		cdev_init(&cfg->cdev, &sipa_local_drv_fops);
		cfg->cdev.owner = THIS_MODULE;
		cfg->cdev.ops = &sipa_remote_drv_fops;
	}

	ret = cdev_add(&cfg->cdev, cfg->dev_num, 1);
	if (ret) {
		pr_err("%s add cdev failed\n", cfg->name);
		return -1;
	}

	return 0;
}

static int create_sipa_ep_from_fifo_idx(enum sipa_cmn_fifo_index fifo_idx,
										struct sipa_plat_drv_cfg *cfg,
										struct sipa_context *ipa)
{
	enum sipa_ep_id ep_id;
	struct sipa_common_fifo *fifo;
	struct sipa_endpoint *ep = NULL;
	struct sipa_common_fifo_info *fifo_info;

	fifo_info = (struct sipa_common_fifo_info *)sipa_common_fifo_statics;
	ep_id = (fifo_info + fifo_idx)->relate_ep;

	ep = s_sipa_ctrl.eps[ep_id];
	if (!ep) {
		ep = kzalloc(sizeof(*ep), GFP_KERNEL);
		if (!ep) {
			pr_err("create_sipa_ep: kzalloc err.\n");
			return -ENOMEM;
		}
		s_sipa_ctrl.eps[ep_id] = ep;
	}

	ep->sipa_ctx = ipa;
	ep->id = (fifo_info + fifo_idx)->relate_ep;
	pr_info("idx = %d ep = %d ep_id = %d is_to_ipa = %d\n",
			fifo_idx, ep->id, ep_id,
			(fifo_info + fifo_idx)->is_to_ipa);

	if (!(fifo_info + fifo_idx)->is_to_ipa) {
		fifo = &ep->recv_fifo;
		fifo->is_receiver = true;
		fifo->rx_fifo.fifo_depth =
			cfg->common_fifo_cfg[fifo_idx].rx_fifo.fifo_size;
		fifo->tx_fifo.fifo_depth =
			cfg->common_fifo_cfg[fifo_idx].tx_fifo.fifo_size;
	} else {
		fifo = &ep->send_fifo;
		fifo->is_receiver = false;
		fifo->rx_fifo.fifo_depth =
			cfg->common_fifo_cfg[fifo_idx].rx_fifo.fifo_size;
		fifo->tx_fifo.fifo_depth =
			cfg->common_fifo_cfg[fifo_idx].tx_fifo.fifo_size;
	}
	fifo->dst_id = (fifo_info + fifo_idx)->dst_id;
	fifo->src_id = (fifo_info + fifo_idx)->src_id;

	fifo->idx = fifo_idx;

	return 0;
}

static void destroy_sipa_ep_from_fifo_idx(enum sipa_cmn_fifo_index fifo_idx,
		struct sipa_plat_drv_cfg *cfg,
		struct sipa_context *ipa)
{
	struct sipa_endpoint *ep = NULL;
	enum sipa_ep_id ep_id = sipa_common_fifo_statics[fifo_idx].relate_ep;

	ep = s_sipa_ctrl.eps[ep_id];
	if (!ep)
		return;

	kfree(ep);
	s_sipa_ctrl.eps[ep_id] = NULL;
}


static void destroy_sipa_eps(struct sipa_plat_drv_cfg *cfg,
							 struct sipa_context *ipa)
{
	int i;

	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		if (cfg->common_fifo_cfg[i].tx_fifo.fifo_size > 0)
			destroy_sipa_ep_from_fifo_idx(i, cfg, ipa);
	}
}


static int create_sipa_eps(struct sipa_plat_drv_cfg *cfg,
						   struct sipa_context *ipa)
{
	int i;
	int ret = 0;

	pr_info("%s start\n", __func__);
	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		if (cfg->common_fifo_cfg[i].tx_fifo.fifo_size > 0) {
			ret = create_sipa_ep_from_fifo_idx(i, cfg, ipa);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int sipa_create_skb_xfer(struct sipa_context *ipa,
								struct sipa_plat_drv_cfg *cfg)
{
	int ret = 0;


	ret = create_sipa_skb_sender(ipa,
								 s_sipa_ctrl.eps[SIPA_EP_AP_ETH],
								 SIPA_PKT_ETH,
								 &s_sipa_ctrl.sender[SIPA_PKT_ETH]);
	if (ret) {
		ret = -EFAULT;
		goto sender_fail;
	}

	ret = create_sipa_skb_sender(ipa,
								 s_sipa_ctrl.eps[SIPA_EP_AP_IP],
								 SIPA_PKT_IP,
								 &s_sipa_ctrl.sender[SIPA_PKT_IP]);
	if (ret) {
		ret = -EFAULT;
		goto receiver_fail;
	}

	ret = create_sipa_skb_receiver(ipa, s_sipa_ctrl.eps[SIPA_EP_AP_ETH],
								   &s_sipa_ctrl.receiver[SIPA_PKT_ETH]);

	if (ret) {
		ret = -EFAULT;
		goto receiver_fail;
	}

	ret = create_sipa_skb_receiver(ipa, s_sipa_ctrl.eps[SIPA_EP_AP_IP],
								   &s_sipa_ctrl.receiver[SIPA_PKT_IP]);

	if (ret) {
		ret = -EFAULT;
		goto receiver_fail;
	}

	return 0;

receiver_fail:
	if (s_sipa_ctrl.receiver[SIPA_PKT_IP]) {
		destroy_sipa_skb_receiver(s_sipa_ctrl.receiver[SIPA_PKT_IP]);
		s_sipa_ctrl.receiver[SIPA_PKT_IP] = NULL;
	}
	if (s_sipa_ctrl.receiver[SIPA_PKT_ETH]) {
		destroy_sipa_skb_receiver(s_sipa_ctrl.receiver[SIPA_PKT_ETH]);
		s_sipa_ctrl.receiver[SIPA_PKT_ETH] = NULL;
	}

sender_fail:
	if (s_sipa_ctrl.sender[SIPA_PKT_IP]) {
		destroy_sipa_skb_sender(s_sipa_ctrl.sender[SIPA_PKT_IP]);
		s_sipa_ctrl.sender[SIPA_PKT_IP] = NULL;
	}
	if (s_sipa_ctrl.sender[SIPA_PKT_ETH]) {
		destroy_sipa_skb_sender(s_sipa_ctrl.sender[SIPA_PKT_ETH]);
		s_sipa_ctrl.sender[SIPA_PKT_ETH] = NULL;
	}

	return ret;
}
static int sipa_init(struct sipa_context **ipa_pp,
					 struct sipa_plat_drv_cfg *cfg,
					 struct device *ipa_dev)
{
	int ret = 0;
	struct sipa_context *ipa = NULL;

	ipa = kzalloc(sizeof(struct sipa_context), GFP_KERNEL);
	if (!ipa) {
		pr_err("sipa_init: kzalloc err.\n");
		return -ENOMEM;
	}

	ipa->pdev = ipa_dev;
	ipa->is_remote = cfg->is_remote;
	ipa->bypass_mode = cfg->is_bypass;

	ipa->hdl = sipa_hal_init(ipa_dev, cfg);


	/* init sipa eps */
	ret = create_sipa_eps(cfg, ipa);
	if (ret) {
		ret = -EFAULT;
		goto fail;
	}

	/* init sipa skb transfer layer */
	if (!ipa->is_remote) {
		ret = sipa_create_skb_xfer(ipa, cfg);
		if (ret) {
			ret = -EFAULT;
			goto fail;
		}
	}

	*ipa_pp = ipa;

	return 0;



fail:

	destroy_sipa_eps(cfg, ipa);

	if (ipa)
		kfree(ipa);
	return ret;
}

static int sipa_plat_drv_probe(struct platform_device *pdev_p)
{
	int ret;
	struct device *dev = &pdev_p->dev;
	struct sipa_plat_drv_cfg *cfg;
	int is_remote = 0;
	/*
	* SIPA probe function can be called for multiple times as the same probe
	* function handles multiple compatibilities
	*/
	pr_debug("sipa: IPA driver probing started for %s\n",
			 pdev_p->dev.of_node->name);

	if (of_device_is_compatible(dev->of_node, "sprd,remote-sipa")) {
		is_remote = 1;
	}

	cfg = &s_sipa_cfg[is_remote];
	memset(cfg, 0, sizeof(*cfg));

	cfg->is_remote = is_remote;

	ret = sipa_parse_dts_configuration(pdev_p, cfg);
	if (ret) {
		pr_err("sipa: dts parsing failed\n");
		return ret;
	}

	ret = ipa_pre_init(cfg);
	if (ret) {
		pr_err("sipa: pre init failed\n");
		return ret;
	}

	if (!is_remote) {
		ret = sipa_sys_init(cfg);
		if (ret) {
			pr_err("sipa: sipa_hal_init failed %d\n", ret);
			return ret;
		}
	}


	ret = sipa_init(&s_sipa_ctrl.ctx[is_remote], cfg, dev);
	if (ret) {
		pr_err("sipa: sipa_init failed %d\n", ret);
		return ret;
	}

	return ret;
}

static struct of_device_id sipa_plat_drv_match[] = {
	{ .compatible = "sprd,sipa", },
	{ .compatible = "sprd,remote-sipa", },
	{}
};

/**
 * sipa_ap_suspend() - suspend callback for runtime_pm
 * @dev: pointer to device
 *
 * This callback will be invoked by the runtime_pm framework when an AP suspend
 * operation is invoked.
 *
 * Returns -EAGAIN to runtime_pm framework in case IPA is in use by AP.
 * This will postpone the suspend operation until IPA is no longer used by AP.
*/
static int sipa_ap_suspend(struct device *dev)
{
	return 0;
}

/**
* sipa_ap_resume() - resume callback for runtime_pm
* @dev: pointer to device
*
* This callback will be invoked by the runtime_pm framework when an AP resume
* operation is invoked.
*
* Always returns 0 since resume should always succeed.
*/
static int sipa_ap_resume(struct device *dev)
{
	return 0;
}

/**
 * sipa_get_pdev() - return a pointer to IPA dev struct
 *
 * Return value: a pointer to IPA dev struct
 *
 */
struct device *sipa_get_pdev(void)
{
	struct device *ret = NULL;

	return ret;
}
EXPORT_SYMBOL(sipa_get_pdev);

static const struct dev_pm_ops sipa_pm_ops = {
	.suspend_noirq = sipa_ap_suspend,
	.resume_noirq = sipa_ap_resume,
};

static struct platform_driver sipa_plat_drv = {
	.probe = sipa_plat_drv_probe,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &sipa_pm_ops,
		.of_match_table = sipa_plat_drv_match,
	},
};

static int __init sipa_module_init(void)
{
	pr_debug("SIPA module init\n");

	/* Register as a platform device driver */
	return platform_driver_register(&sipa_plat_drv);
}
subsys_initcall(sipa_module_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum IPA HW device driver");
