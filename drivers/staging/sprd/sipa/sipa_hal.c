#include <linux/sipa.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_SIPA_TEST
#include <linux/kthread.h>
#endif

#include "sipa_hal.h"
#include "sipa_hal_priv.h"
#include "sipa_priv.h"

struct sipa_hal_context sipa_hal_ctx;

static int alloc_tx_fifo_ram(struct device *dev,
							 struct sipa_hal_context *cfg,
							 enum sipa_cmn_fifo_index index)
{
	dma_addr_t phy_addr;
	struct sipa_common_fifo_cfg_tag *fifo_cfg =
				&cfg->cmn_fifo_cfg[index];

	if (fifo_cfg->tx_fifo.in_iram) {
		if (cfg->phy_virt_res.iram_allocated_size >=
			cfg->phy_virt_res.iram_size)
			return -ENOMEM;

		fifo_cfg->tx_fifo.virtual_addr =
			cfg->phy_virt_res.iram_base +
			cfg->phy_virt_res.iram_allocated_size;

		phy_addr = cfg->phy_virt_res.iram_phy +
				   cfg->phy_virt_res.iram_allocated_size;

		fifo_cfg->tx_fifo.fifo_base_addr_l =
			IPA_GET_LOW32(phy_addr);

		fifo_cfg->tx_fifo.fifo_base_addr_h =
			IPA_GET_HIGH32(phy_addr);

		cfg->phy_virt_res.iram_allocated_size +=
			fifo_cfg->tx_fifo.depth *
			sizeof(struct sipa_node_description_tag);
	} else {
		if (fifo_cfg->tx_fifo.depth != 0)
			fifo_cfg->tx_fifo.virtual_addr = dma_alloc_coherent(dev,
											 fifo_cfg->tx_fifo.depth *
											 sizeof(struct sipa_node_description_tag),
											 &phy_addr, GFP_KERNEL);
		else
			return 0;

		fifo_cfg->tx_fifo.fifo_base_addr_l =
			IPA_GET_LOW32(phy_addr);

		fifo_cfg->tx_fifo.fifo_base_addr_h =
			IPA_GET_HIGH32(phy_addr);

		if (!fifo_cfg->tx_fifo.virtual_addr)
			return -ENOMEM;
	}

	return 0;
}

static int alloc_rx_fifo_ram(struct device *dev,
							 struct sipa_hal_context *cfg,
							 enum sipa_cmn_fifo_index index)
{
	dma_addr_t phy_addr;
	struct sipa_common_fifo_cfg_tag *fifo_cfg =
				&cfg->cmn_fifo_cfg[index];

	if (fifo_cfg->rx_fifo.in_iram) {
		if (cfg->phy_virt_res.iram_allocated_size >=
			cfg->phy_virt_res.iram_size) {
			IPA_ERR("fifo id = %d don't have iram\n", index);
			return -ENOMEM;
		}

		fifo_cfg->rx_fifo.virtual_addr =
			cfg->phy_virt_res.iram_base +
			cfg->phy_virt_res.iram_allocated_size;

		phy_addr = cfg->phy_virt_res.iram_phy +
				   cfg->phy_virt_res.iram_allocated_size;

		fifo_cfg->rx_fifo.fifo_base_addr_l =
			IPA_GET_LOW32(phy_addr);

		fifo_cfg->rx_fifo.fifo_base_addr_h =
			IPA_GET_HIGH32(phy_addr);

		cfg->phy_virt_res.iram_allocated_size +=
			fifo_cfg->rx_fifo.depth *
			sizeof(struct sipa_node_description_tag);
	} else {
		if (fifo_cfg->rx_fifo.depth != 0)
			fifo_cfg->rx_fifo.virtual_addr = dma_alloc_coherent(dev,
											 fifo_cfg->rx_fifo.depth *
											 sizeof(struct sipa_node_description_tag),
											 &phy_addr, GFP_KERNEL);
		else
			return 0;

		fifo_cfg->rx_fifo.fifo_base_addr_l =
			IPA_GET_LOW32(phy_addr);

		fifo_cfg->rx_fifo.fifo_base_addr_h =
			IPA_GET_HIGH32(phy_addr);

		if (!fifo_cfg->rx_fifo.virtual_addr) {
			IPA_ERR("dma alloc buf failed\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static void free_tx_fifo_ram(struct device *dev,
							 struct sipa_hal_context *cfg,
							 enum sipa_cmn_fifo_index index)
{
	dma_addr_t phy_addr;
	struct sipa_common_fifo_cfg_tag *fifo_cfg =
				&cfg->cmn_fifo_cfg[index];

	if (!fifo_cfg->tx_fifo.in_iram &&
		fifo_cfg->tx_fifo.virtual_addr) {
		phy_addr =
			IPA_STI_64BIT(fifo_cfg->tx_fifo.fifo_base_addr_l,
						  fifo_cfg->tx_fifo.fifo_base_addr_h);
		dma_free_coherent(dev,
						  fifo_cfg->tx_fifo.depth *
						  sizeof(struct sipa_node_description_tag),
						  fifo_cfg->tx_fifo.virtual_addr,
						  phy_addr);
	}
}

static void free_rx_fifo_ram(struct device *dev,
							 struct sipa_hal_context *cfg,
							 enum sipa_cmn_fifo_index index)
{
	dma_addr_t phy_addr;
	struct sipa_common_fifo_cfg_tag *fifo_cfg =
				&cfg->cmn_fifo_cfg[index];

	if (!fifo_cfg->rx_fifo.in_iram &&
		fifo_cfg->rx_fifo.virtual_addr) {
		phy_addr = IPA_STI_64BIT(
					   fifo_cfg->rx_fifo.fifo_base_addr_l,
					   fifo_cfg->rx_fifo.fifo_base_addr_h);
		dma_free_coherent(dev,
						  fifo_cfg->rx_fifo.depth *
						  sizeof(struct sipa_node_description_tag),
						  fifo_cfg->rx_fifo.virtual_addr,
						  phy_addr);
	}
}

static int sipa_init_fifo_addr(struct device *dev,
							   struct sipa_hal_context *cfg)
{
	int i, ret;

	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		ret = alloc_rx_fifo_ram(dev, cfg, i);
		if (ret)
			return -1;
		ret = alloc_tx_fifo_ram(dev, cfg, i);
		if (ret)
			return -1;
	}

	return 0;
}

static u32 sipa_init_fifo_reg_base(struct sipa_hal_context *cfg)
{
	int i;

	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		cfg->cmn_fifo_cfg[i].fifo_reg_base =
			cfg->phy_virt_res.glb_base +
			((i + 1) * SIPA_FIFO_REG_SIZE);
		cfg->cmn_fifo_cfg[i].fifo_phy_addr =
			cfg->phy_virt_res.glb_phy +
			((i + 1) * SIPA_FIFO_REG_SIZE);
	}

	return 0;
}

static void siap_init_hal_cfg(struct sipa_plat_drv_cfg *cfg)
{
	u32 i;
	struct sipa_hal_context *hal_cfg = &sipa_hal_ctx;

	hal_cfg->ipa_intr = cfg->ipa_intr;

	for (i = 0; i < SIPA_FIFO_MAX; i++) {
		hal_cfg->cmn_fifo_cfg[i].cur = cfg->common_fifo_cfg[i].src;
		hal_cfg->cmn_fifo_cfg[i].dst = cfg->common_fifo_cfg[i].dst;
		hal_cfg->cmn_fifo_cfg[i].is_recv =
			cfg->common_fifo_cfg[i].is_recv;
		hal_cfg->cmn_fifo_cfg[i].tx_fifo.depth =
			cfg->common_fifo_cfg[i].tx_fifo.fifo_size;
		hal_cfg->cmn_fifo_cfg[i].tx_fifo.in_iram =
			cfg->common_fifo_cfg[i].tx_fifo.in_iram;
		hal_cfg->cmn_fifo_cfg[i].rx_fifo.depth =
			cfg->common_fifo_cfg[i].rx_fifo.fifo_size;
		hal_cfg->cmn_fifo_cfg[i].rx_fifo.in_iram =
			cfg->common_fifo_cfg[i].rx_fifo.in_iram;
		hal_cfg->cmn_fifo_cfg[i].is_pam =
			cfg->common_fifo_cfg[i].is_pam;
		hal_cfg->cmn_fifo_cfg[i].fifo_id = i;
	}
}

sipa_hal_hdl sipa_hal_init(struct device *dev,
						   struct sipa_plat_drv_cfg *cfg)
{
	int ret;
	struct sipa_hal_context *hal_cfg = &sipa_hal_ctx;

	hal_cfg->dev = dev;

	sipa_glb_ops_init(&hal_cfg->glb_ops);
	sipa_fifo_ops_init(&hal_cfg->fifo_ops);

	siap_init_hal_cfg(cfg);

	ret = request_irq(hal_cfg->ipa_intr, sipa_int_callback_func,
					  IRQF_NO_SUSPEND, "sprd,sipa", hal_cfg);
	if (ret)
		IPA_ERR("request irq err ret = %d\n", ret);

	enable_irq_wake(hal_cfg->ipa_intr);

	hal_cfg->phy_virt_res.glb_phy = cfg->glb_phy;
	hal_cfg->phy_virt_res.glb_size = cfg->glb_size;
	hal_cfg->phy_virt_res.glb_base = devm_ioremap_nocache(
					  dev, cfg->glb_phy, cfg->glb_size);

	if (!hal_cfg->phy_virt_res.glb_base) {
		IPA_ERR("%s: remap glb_base fail\n", __func__);
		return NULL;
	}

	hal_cfg->phy_virt_res.iram_phy = cfg->iram_phy;
	hal_cfg->phy_virt_res.iram_size = cfg->iram_size;
	hal_cfg->phy_virt_res.iram_base = devm_ioremap_nocache(
									 dev, cfg->iram_phy, cfg->iram_size);
	if (!hal_cfg->phy_virt_res.iram_base) {
		IPA_ERR("%s: remap iram_base fail\n", __func__);
		return NULL;
	}

	ret = sipa_init_fifo_addr(dev, hal_cfg);
	if (ret)
		IPA_ERR("init fifo addr err ret = %d\n", ret);

	sipa_init_fifo_reg_base(hal_cfg);

	hal_cfg->glb_ops.enable_wiap_ul_dma(
		hal_cfg->phy_virt_res.glb_base,
		(u32)cfg->wiap_ul_dma);

	hal_cfg->glb_ops.enable_cp_through_pcie(
		hal_cfg->phy_virt_res.glb_base,
		(u32)cfg->need_through_pcie);

	hal_cfg->glb_ops.enable_def_flowctrl_to_src_blk(
		hal_cfg->phy_virt_res.glb_base);

	hal_cfg->glb_ops.set_mode(
		hal_cfg->phy_virt_res.glb_base,
		cfg->is_bypass);

	return ((sipa_hal_hdl)hal_cfg);
}
EXPORT_SYMBOL(sipa_hal_init);

int sipa_set_enabled(struct sipa_plat_drv_cfg *cfg)
{
	int ret = 0;

	if (cfg->sys_regmap) {
		ret = regmap_update_bits(cfg->sys_regmap,
						   cfg->enable_reg,
						   cfg->enable_mask,
						   cfg->enable_mask);
		if (ret < 0)
			pr_warn("%s: regmap update bits failed", __func__);
	}
	return ret;
}
EXPORT_SYMBOL(sipa_set_enabled);

int sipa_force_wakeup(struct sipa_plat_drv_cfg *cfg)
{
	int ret = 0;

	if (cfg->wakeup_regmap) {
		ret = regmap_update_bits(cfg->wakeup_regmap,
						   cfg->wakeup_reg,
						   cfg->wakeup_mask,
						   cfg->wakeup_mask);
		if (ret < 0)
			pr_warn("%s: regmap update bits failed", __func__);
	}
	return ret;
}
EXPORT_SYMBOL(sipa_force_wakeup);


int sipa_open_common_fifo(sipa_hal_hdl hdl,
						  enum sipa_cmn_fifo_index fifo,
						  struct sipa_comm_fifo_params *attr,
						  bool force_sw_intr,
						  sipa_hal_notify_cb cb,
						  void *priv)
{
	struct sipa_hal_context *hal_cfg;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	if (unlikely(!hdl)) {
		IPA_ERR("hdl is null\n");
		return -1;
	}
	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	fifo_cfg[fifo].priv = priv;
	fifo_cfg[fifo].fifo_irq_callback = cb;

	IPA_LOG("fifo_id = %d is_pam = %d is_recv = %d\n",
			fifo_cfg[fifo].fifo_id,
			fifo_cfg[fifo].is_pam,
			fifo_cfg[fifo].is_recv);
	hal_cfg->fifo_ops.open(fifo, fifo_cfg, NULL);
	if (!force_sw_intr && fifo_cfg[fifo].is_pam) {
		hal_cfg->fifo_ops.set_hw_interrupt_threshold(
			fifo, fifo_cfg, 1, attr->tx_intr_threshold, NULL);
		hal_cfg->fifo_ops.set_hw_interrupt_timeout(
			fifo, fifo_cfg, 1, attr->tx_intr_delay_us, NULL);
	} else {
		if (attr->tx_intr_threshold)
			hal_cfg->fifo_ops.set_interrupt_threshold(
				fifo, fifo_cfg,	1,
				attr->tx_intr_threshold, NULL);
		if (attr->tx_intr_delay_us)
			hal_cfg->fifo_ops.set_interrupt_timeout(
				fifo, fifo_cfg, 1,
				attr->tx_intr_delay_us, NULL);
	}

	hal_cfg->fifo_ops.set_interrupt_txfifo_full(
		fifo, fifo_cfg, 1, NULL);
	if (fifo_cfg[fifo].is_recv)
		hal_cfg->fifo_ops.enable_remote_flowctrl_interrupt(
			fifo, fifo_cfg, attr->flow_ctrl_cfg,
			attr->tx_enter_flowctrl_watermark,
			attr->tx_leave_flowctrl_watermark,
			attr->rx_enter_flowctrl_watermark,
			attr->rx_leave_flowctrl_watermark);
	else
		hal_cfg->fifo_ops.enable_local_flowctrl_interrupt(
			fifo, fifo_cfg, 1,
			attr->flow_ctrl_irq_mode, NULL);

	if (attr->flowctrl_in_tx_full)
		hal_cfg->fifo_ops.set_interrupt_txfifo_full(fifo,
				fifo_cfg, 1, NULL);
	else
		hal_cfg->fifo_ops.set_interrupt_txfifo_full(fifo,
				fifo_cfg, 0, NULL);

	return 0;
}
EXPORT_SYMBOL(sipa_open_common_fifo);

/*
 * stop : true : stop recv false : start receive
 */
int sipa_hal_cmn_fifo_set_receive(sipa_hal_hdl hdl,
								  enum sipa_cmn_fifo_index fifo_id,
								  bool stop)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	ret = hal_cfg->fifo_ops.ctrl_receive(
			  fifo_id, fifo_cfg, stop);

	if (ret)
		return 0;
	else
		return -1;
}
EXPORT_SYMBOL(sipa_hal_cmn_fifo_set_receive);

int sipa_hal_init_set_tx_fifo(sipa_hal_hdl hdl,
							  enum sipa_cmn_fifo_index fifo_id,
							  struct sipa_hal_fifo_item *items,
							  u32 num)
{
	u32 ret, i;
	struct sipa_hal_context *hal_cfg;
	struct sipa_node_description_tag node;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	memset(&node, 0, sizeof(node));
	for (i = 0; i < num; i++) {
		node.address = (items + i)->addr;
		node.length = (items + i)->len;
		node.dst = (items + i)->dst;
		node.offset = (items + i)->offset;
		ret = hal_cfg->fifo_ops.put_node_to_tx_fifo(
				  fifo_id, fifo_cfg, &node, 0, 1);
		if (ret == 0) {
			IPA_ERR("put node to tx fifo %d fail\n", fifo_id);
			return -1;
		}
	}

	return 0;
}
EXPORT_SYMBOL(sipa_hal_init_set_tx_fifo);

int sipa_hal_get_tx_fifo_item(sipa_hal_hdl hdl,
							  enum sipa_cmn_fifo_index fifo_id,
							  struct sipa_hal_fifo_item *item)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_node_description_tag node;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	ret = hal_cfg->fifo_ops.recv_node_from_tx_fifo(
			  fifo_id, fifo_cfg, &node, 0, 1);

	if (ret == 0) {
		IPA_ERR("get node from tx fifo %d fail\n", fifo_id);
		return -1;
	}

	item->addr = node.address;
	item->len = node.length;
	item->dst = node.dst;
	item->offset = node.offset;
	item->src = node.src;
	item->err_code = node.err_code;
	item->netid = node.net_id;
	item->intr = node.intr;

	return 0;
}
EXPORT_SYMBOL(sipa_hal_get_tx_fifo_item);

int sipa_hal_get_cmn_fifo_filled_depth(sipa_hal_hdl hdl,
									   enum sipa_cmn_fifo_index fifo_id,
									   u32 *rx_filled, u32 *tx_filled)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	ret = hal_cfg->fifo_ops.get_filled_depth(
			  fifo_id, fifo_cfg, rx_filled, tx_filled);

	return 0;
}
EXPORT_SYMBOL(sipa_hal_get_cmn_fifo_filled_depth);

int sipa_hal_enable_wiap_dma(sipa_hal_hdl hdl, bool dma)
{
	struct sipa_hal_context *hal_cfg = (struct sipa_hal_context *)hdl;

	return hal_cfg->glb_ops.enable_wiap_ul_dma(
			hal_cfg->phy_virt_res.glb_base,
			dma);
}
EXPORT_SYMBOL(sipa_hal_enable_wiap_dma);

int sipa_hal_put_rx_fifo_item(sipa_hal_hdl hdl,
							  enum sipa_cmn_fifo_index fifo_id,
							  struct sipa_hal_fifo_item *item)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_node_description_tag node;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	memset(&node, 0, sizeof(node));
	node.address = item->addr;
	node.length = item->len;
	node.dst = item->dst;
	node.offset = item->offset;
	node.src = item->src;
	node.err_code = item->err_code;
	node.net_id = item->netid;
	node.intr = item->intr;

	ret = hal_cfg->fifo_ops.put_node_to_rx_fifo(
			  fifo_id, fifo_cfg, &node, 0, 1);
	if (ret == 0) {
		IPA_ERR("put node to rx fifo %d fail\n", fifo_id);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(sipa_hal_put_rx_fifo_item);

bool sipa_hal_is_rx_fifo_empty(sipa_hal_hdl hdl,
							   enum sipa_cmn_fifo_index fifo_id)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	ret = hal_cfg->fifo_ops.get_rx_empty_status(fifo_id, fifo_cfg);

	return ret;
}
EXPORT_SYMBOL(sipa_hal_is_rx_fifo_empty);

bool sipa_hal_is_tx_fifo_empty(sipa_hal_hdl hdl,
							   enum sipa_cmn_fifo_index fifo_id)
{
	u32 ret;
	struct sipa_hal_context *hal_cfg;
	struct sipa_common_fifo_cfg_tag *fifo_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;
	fifo_cfg = hal_cfg->cmn_fifo_cfg;

	ret = hal_cfg->fifo_ops.get_tx_empty_status(fifo_id, fifo_cfg);

	return ret;
}
EXPORT_SYMBOL(sipa_hal_is_tx_fifo_empty);

int sipa_hal_free_tx_rx_fifo_buf(sipa_hal_hdl hdl,
								 enum sipa_cmn_fifo_index fifo_id,
								 struct sipa_hal_fifo_item *item)
{
	struct sipa_hal_context *hal_cfg;

	hal_cfg = (struct sipa_hal_context *)hdl;

	free_rx_fifo_ram(hal_cfg->dev, hal_cfg, fifo_id);
	free_tx_fifo_ram(hal_cfg->dev, hal_cfg, fifo_id);

	return 0;
}
EXPORT_SYMBOL(sipa_hal_free_tx_rx_fifo_buf);

int sipa_hal_init_pam_param(enum sipa_cmn_fifo_index dl_idx,
							enum sipa_cmn_fifo_index ul_idx,
							struct sipa_to_pam_info *out)
{
	struct sipa_common_fifo_cfg_tag *dl_fifo_cfg, *ul_fifo_cfg;
	struct sipa_hal_context *hal_cfg = &sipa_hal_ctx;

	dl_fifo_cfg = &hal_cfg->cmn_fifo_cfg[dl_idx];
	ul_fifo_cfg = &hal_cfg->cmn_fifo_cfg[ul_idx];

	out->dl_fifo.tx_fifo_base_addr =
		IPA_STI_64BIT(dl_fifo_cfg->tx_fifo.fifo_base_addr_l,
					  dl_fifo_cfg->tx_fifo.fifo_base_addr_h);
	out->dl_fifo.rx_fifo_base_addr =
		IPA_STI_64BIT(dl_fifo_cfg->rx_fifo.fifo_base_addr_l,
					  dl_fifo_cfg->rx_fifo.fifo_base_addr_h);

	out->ul_fifo.tx_fifo_base_addr =
		IPA_STI_64BIT(ul_fifo_cfg->tx_fifo.fifo_base_addr_l,
					  ul_fifo_cfg->tx_fifo.fifo_base_addr_h);
	out->ul_fifo.rx_fifo_base_addr =
		IPA_STI_64BIT(ul_fifo_cfg->rx_fifo.fifo_base_addr_l,
					  ul_fifo_cfg->rx_fifo.fifo_base_addr_h);

	out->dl_fifo.fifo_sts_addr = dl_fifo_cfg->fifo_phy_addr;
	out->ul_fifo.fifo_sts_addr = ul_fifo_cfg->fifo_phy_addr;

	out->dl_fifo.fifo_depth = dl_fifo_cfg->tx_fifo.depth;
	out->ul_fifo.fifo_depth = ul_fifo_cfg->tx_fifo.depth;

	return 0;
}
EXPORT_SYMBOL(sipa_hal_init_pam_param);

int sipa_swap_hash_table(struct sipa_hash_table *new_tbl,
						 struct sipa_hash_table *old_tbl)
{
	u32 len, addrl, addrh;
	struct sipa_hal_context *hal_cfg = &sipa_hal_ctx;

	if (old_tbl) {
		hal_cfg->glb_ops.get_hash_table(hal_cfg->phy_virt_res.glb_base,
										&addrl, &addrh, &len);
		old_tbl->tbl_phy_addr = IPA_STI_64BIT(addrl, addrh);
		old_tbl->depth = len;
	}

	if (new_tbl) {
		addrl = IPA_GET_LOW32(new_tbl->tbl_phy_addr);
		addrh = IPA_GET_HIGH32(new_tbl->tbl_phy_addr);
		hal_cfg->glb_ops.hash_table_switch(
			hal_cfg->phy_virt_res.glb_base,
			addrl, addrh, new_tbl->depth);
	}

	return 0;
}
EXPORT_SYMBOL(sipa_swap_hash_table);

void sipa_test_enable_periph_int_to_sw(void)
{
	struct sipa_hal_context *hal_cfg = &sipa_hal_ctx;

	hal_cfg->glb_ops.map_interrupt_src_en(
		hal_cfg->phy_virt_res.glb_base, 1, 0x3ffff);
}
EXPORT_SYMBOL(sipa_test_enable_periph_int_to_sw);

