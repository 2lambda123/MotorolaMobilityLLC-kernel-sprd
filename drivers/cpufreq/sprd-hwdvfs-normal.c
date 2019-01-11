/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/topology.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/printk.h>
#include "sprd-cpufreqhw.h"
#include "sprd-hwdvfs-normal.h"

static const struct of_device_id sprd_cpudvfs_of_match[] = {
	{
		.compatible = "sprd,sharkl5-cpudvfs",
	},
};
MODULE_DEVICE_TABLE(of, sprd_cpudvfs_of_match);

static int cpudvfs_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct sprd_cpudvfs_device *platdev = sprd_hardware_dvfs_device_get();
	struct cpudvfs_archdata *pri;

	if (!platdev) {
		pr_err("No cpu dvfs device found.\n");
		return -ENODEV;
	}
	pri = (struct cpudvfs_archdata *)platdev->archdata;
	pri->i2c_client = client;

	return 0;
}

static const struct of_device_id cpudvfs_i2c_of_match[] = {
	{.compatible = "sprd,cpudvfs-regulator-sharkl5",},
	{},
};
MODULE_DEVICE_TABLE(of, cpudvfs_i2c_of_match);

static struct i2c_driver cpudvfs_i2c_driver = {
	.driver = {
		.name = "cpudvfs_i2c_drv",
		.owner = THIS_MODULE,
		.of_match_table = cpudvfs_i2c_of_match,
	},
	.probe = cpudvfs_i2c_probe,
};

static
void cpu_dvfs_bits_update(struct cpudvfs_archdata *pdev,
			  u32 reg, u32 mask, u32 val)
{
	u32 tmp;

	tmp = readl((pdev->membase + reg)) & ~mask;
	tmp |= val & mask;

	writel(tmp, (pdev->membase + reg));
}

static
int  fill_in_dvfs_tbl_entry(void *clu,
			    int entry_num, u32 *entry_data, u32 nr)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 col = 0, val = 0;
	u32 bit, mask;

	if (!entry_data) {
		pr_err("Empty table entry data\n");
		return -EINVAL;
	}

	if (entry_num < 0 || entry_num > cluster->tbl_row_num) {
		pr_err("The table entry number is beyond the scope.\n");
		return -EINVAL;
	}

	if (nr != cluster->tbl_column_num || nr == 0) {
		pr_err("Incorrect %s cluster map table column number\n",
		       cluster->name);
		return -EINVAL;
	}

	for (col = 0; col < nr; ++col) {
		bit = cluster->column_entry_bit[col];
		mask = cluster->column_entry_mask[col];
		val |= (entry_data[col] & mask) << bit;
	}

	writel(val, pdev->membase + cluster->map_tbl_regs[entry_num]);

	return 0;
}

static  int dvfs_map_tbl_init(void *clu)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	u32 row, column, size, num;
	int ret;
	u32 *tbl;

	if (!cluster->existed) {
		pr_info("This platform does not use %s cluster\n",
			cluster->name);
		return 0;
	}

	if (!cluster->opp_map_tbl) {
		num = cluster->tbl_row_num * cluster->tbl_column_num;
		size = num * sizeof(u32);
		tbl = kzalloc(size, GFP_KERNEL);
		if (!tbl)
			return -ENOMEM;

		cluster->opp_map_tbl = tbl;
	}

	for (row = 0; row < cluster->tbl_row_num; ++row) {
		for (column = 0; column < cluster->tbl_column_num; ++column) {
			of_property_read_u32_index(cluster->of_node,
						   cluster->dts_tbl_name,
					row * cluster->tbl_column_num + column,
			(u32 *)&(tbl + row * cluster->tbl_column_num)[column]);
		}
		ret = fill_in_dvfs_tbl_entry(cluster,
					     row,
				(tbl + row * cluster->tbl_column_num),
				 cluster->tbl_column_num);
		if (ret) {
			pr_err("Error in filling in the dvfs table\n");
			goto table_free;
		}
	}

	return cluster->tbl_row_num;

table_free:
	kfree(cluster->opp_map_tbl);
	return ret;
}

static int sprd_hw_dvfs_map_table_init(void *data)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;
	int idx = 0, ret;

	cluster = pdev->cluster_array[0];

	if (!cluster) {
		pr_err("No cluster found\n");
		return -EINVAL;
	}

	while (cluster) {
		ret = cluster->driver->map_tbl_init(cluster);
		if (ret < 0)
			return ret;
		idx++;
		cluster = pdev->cluster_array[idx];
	}

	return 0;
}

static int sprd_dvfs_module_eb(void *data)
{
	int ret;
	struct regmap *aon_apb = NULL;
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;

	aon_apb = pdev->aon_apb_reg_base;
	ret = regmap_update_bits(aon_apb, pdev->module_eb_reg,
				 1 << pdev->module_eb_bit,
				 1 << pdev->module_eb_bit);

	if (ret) {
		pr_err("Failed to enable dvfs module\n");
		return ret;
	}

	pdev->module_eb = true;

	return 0;
}

static int sprd_mpll_relock_enable(void *data, u32 num, bool enable)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct mpll_cfg *mpll;

	if (num > pdev->mpll_num) {
		pr_err("Invalid mpll number\n");
		return -EINVAL;
	}

	mpll = &pdev->mplls[num];

	if (!mpll) {
		pr_err("MPLL resource is empty\n");
		return -ENODEV;
	}

	if (enable) {
		cpu_dvfs_bits_update(pdev, mpll->relock_reg,
				     1 << mpll->relock_bit,
				     1 << mpll->relock_bit);
		mpll->relock_eb = 1;
	} else {
		cpu_dvfs_bits_update(pdev, mpll->relock_reg,
				     1 << mpll->relock_bit,
				     ~(1 << mpll->relock_bit));
		mpll->relock_eb = 0;
	}

	return 0;
}

static int sprd_mpll_pd_enable(void *data, u32 num, bool enable)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct mpll_cfg *mpll;

	if (num > pdev->mpll_num) {
		pr_err("Invalid mpll number\n");
		return -EINVAL;
	}

	mpll = &pdev->mplls[num];

	if (!mpll) {
		pr_err("MPLL resource is empty\n");
		return -ENODEV;
	}

	if (enable) {
		cpu_dvfs_bits_update(pdev, mpll->pd_reg,
				     1 << mpll->pd_bit, 1 << mpll->pd_bit);
		mpll->pd_eb = 1;
	} else {
		cpu_dvfs_bits_update(pdev, mpll->pd_reg,
				     1 << mpll->pd_bit, ~(1 << mpll->pd_bit));
		mpll->pd_eb = 0;
	}

	return 0;
}

static
int host_cluster_auto_tuning_enable(void *clu, bool enable)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 addr0, bit0;
	u32 addr1, bit1;
	int ret;

	if (cluster->id >= pdev->host_cluster_num) {
		pr_err("Incorrect host cluster number\n");
		return -EINVAL;
	}

	if (cluster->id >= pdev->dcdc_num) {
		pr_err("The cluster number(%d) is beyond dcdc number(%d)",
		       cluster->id, pdev->dcdc_num);
		return -EINVAL;
	}

	addr0 = pdev->pwr[cluster->id].dvfs_ctl_reg;
	bit0 = 1 << pdev->pwr[cluster->id].dvfs_ctl_bit;

	addr1 = pdev->pwr[cluster->id].subsys_tune_ctl_reg;
	bit1 =  1 << pdev->pwr[cluster->id].subsys_tune_ctl_bit;

	if (enable) {
		ret = regmap_update_bits(pdev->topdvfs_map, addr0, bit0, ~bit0);
		if (ret)
			return ret;
		ret = regmap_update_bits(pdev->topdvfs_map, addr1, bit1, ~bit1);
		if (ret)
			return ret;
	} else {
		ret = regmap_update_bits(pdev->topdvfs_map, addr0, bit0, bit0);
		if (ret)
			return ret;
		ret = regmap_update_bits(pdev->topdvfs_map, addr1, bit1, bit1);
			return ret;
	}

	return 0;
}

static
int slave_cluster_auto_tuning_enable(void *clu, bool enable)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 reg, bit;

	reg = cluster->tuning_fun_reg;
	bit = 1 << cluster->tuning_fun_bit;

	if (enable)
		cpu_dvfs_bits_update(pdev, reg, bit, bit);
	else
		cpu_dvfs_bits_update(pdev, reg, bit, ~bit);

	return 0;
}

static int cluster_set_index(void *clu, u32 opp_idx, bool work)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 value = 0;

	if (work) {
		value = opp_idx & cluster->work_index_mask;
		writel(value, pdev->membase + cluster->work_index_reg);
	} else {
		value = opp_idx & cluster->idle_index_mask;
		writel(value, pdev->membase + cluster->idle_index_reg);
	}

	return 0;
}

static int cluster_get_index(void *clu, bool work)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 index = 0;

	if (work)
		index = readl(pdev->membase + cluster->work_index_reg) &
			cluster->work_index_mask;
	else
		index = readl(pdev->membase + cluster->idle_index_reg) &
			cluster->idle_index_mask;

	return index;
}

static int get_device_cgm_sel(void *clu, u32 dev_nr)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 val;

	if (!cluster->subdevs) {
		pr_err("No device found in %s cluster\n",
		       cluster->name);
		return -ENODEV;
	}

	if (dev_nr >= cluster->device_num) {
		pr_err("Invalid device number in %s cluster\n",
		       cluster->name);
		return -EINVAL;
	}

	val =  readl(pdev->membase + cluster->subdevs[dev_nr].sel_reg);

	return (val >> cluster->subdevs[dev_nr].sel_bit) &
				cluster->subdevs[dev_nr].sel_mask;
}

static int get_device_cgm_div(void *clu, u32 dev_nr)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 val;

	if (!cluster->subdevs) {
		pr_err("No device found in %s cluster\n",
		       cluster->name);
		return -ENODEV;
	}

	if (dev_nr >= cluster->device_num) {
		pr_err("Invalid device number in %s cluster\n",
		       cluster->name);
		return -EINVAL;
	}

	val =  readl(pdev->membase + cluster->subdevs[dev_nr].div_reg);

	return (val >> cluster->subdevs[dev_nr].div_bit) &
				cluster->subdevs[dev_nr].div_mask;
}

static int get_device_voted_volt(void *clu, u32 dev_nr)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev = cluster->parent_dev;
	u32 val;

	if (!cluster->subdevs) {
		pr_err("No device found in %s cluster\n",
		       cluster->name);
		return -ENODEV;
	}

	if (dev_nr >= cluster->device_num) {
		pr_err("Invalid device number in %s cluster\n",
		       cluster->name);
		return -EINVAL;
	}

	val =  readl(pdev->membase + cluster->subdevs[dev_nr].vol_reg);

	return (val >> cluster->subdevs[dev_nr].vol_bit) &
				cluster->subdevs[dev_nr].vol_mask;
}

inline unsigned long get_cluster_freq(void *clu, int hw_opp_index)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	int sw_opp_index = hw_opp_index - 1;

	return cluster->freqvolt[sw_opp_index].freq / 1000;
}

static
int get_index_entry_info(void *clu, u32 index, u32 **pdvfs_tbl_entry)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;

	if (index >= cluster->tbl_row_num) {
		pr_err("Invalid map table index\n");
		return -EINVAL;
	}

	*pdvfs_tbl_entry = cluster->opp_map_tbl +
					index * cluster->tbl_column_num;
	return 0;
}

static
int sprd_auto_tuning_enable(void *data, u32 cluster_id, bool enable)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("Incorrect cluster number, cluster_id = %d\n",
		       cluster_id);
		return false;
	}

	cluster = pdev->cluster_array[cluster_id];

	if (!cluster) {
		pr_err("Failed to get point to cluster%d\n",
		       cluster_id);
		return -ENODEV;
	}

	return cluster->auto_tuning_enable(cluster, enable);
}

static int  sprd_set_dvfs_work_index(void *data, u32 cluster_id,
				     u32 opp_idx)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get point to cluster%d\n",
		       cluster_id);
		return -ENODEV;
	}

	if (opp_idx >= cluster->tbl_row_num) {
		pr_err("Invalid dvfs table index for %s cluster\n",
		       cluster->name);
		return -EINVAL;
	}

	return cluster->driver->set_index(cluster, opp_idx, true);
}

static int sprd_set_dvfs_idle_index(void *data, u32 cluster_id,
				    u32 idle_idx)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get point to cluster%d\n",
		       cluster_id);
		return -ENODEV;
	}

	if (idle_idx >= cluster->tbl_row_num) {
		pr_err("Invalid dvfs table index for %s cluster\n",
		       cluster->name);
		return -EINVAL;
	}

	return cluster->driver->set_index(cluster, idle_idx, false);
}

static int sprd_get_dvfs_index(void *data, u32 cluster_id, bool work)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n", cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_index(cluster, work);
}

static int sprd_get_cgm_sel_value(void *data, u32 cluster_id, u32 device_id)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n",
		       cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_cgm_sel(cluster, device_id);
}

static int sprd_get_cgm_div_value(void *data, u32 cluster_id, u32 device_id)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n", cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_cgm_div(cluster, device_id);
}

static int sprd_get_cgm_voted_volt(void *data, u32 cluster_id, u32 device_id)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n", cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_voted_volt(cluster, device_id);
}

static
int sprd_get_index_entry_info(void *data,
			      int index, u32 cluster_id, u32 **pdvfs_tbl_entry)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n", cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_entry_info(cluster,
				index, pdvfs_tbl_entry);
}

static int sprd_get_index_freq(void *data, u32 cluster_id, int index)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *cluster;

	if (cluster_id >= pdev->total_cluster_num) {
		pr_err("The cluster id is overflow.\n");
		return -EINVAL;
	}

	cluster = pdev->cluster_array[cluster_id];
	if (!cluster) {
		pr_err("Failed to get cluster%d device\n",
		       cluster_id);
		return -ENODEV;
	}

	return cluster->driver->get_freq(cluster, index);
}

static int sprd_get_sys_dcdc_dvfs_state(void *data, u32 dcdc_nr)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	u32 addr, bit, mask;

	if (!pdev->pwr) {
		pr_err("No DCDC Power domain found\n");
		return -ENODEV;
	}

	if (dcdc_nr >= pdev->dcdc_num) {
		pr_err("Incorrect dcdc number\n");
		return -ENODEV;
	}

	addr = pdev->pwr[dcdc_nr].subsys_dvfs_state_reg;
	bit = pdev->pwr[dcdc_nr].subsys_dvfs_state_bit;
	mask = pdev->pwr[dcdc_nr].subsys_dvfs_state_mask;

	return (readl(pdev->membase + addr) >> bit) & mask;
}

static int sprd_get_top_dcdc_dvfs_state(void *data, u32 dcdc_nr)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	u32 addr, bit, mask, val;
	int ret;

	if (!pdev->pwr) {
		pr_err("No DCDC Power domain found\n");
		return -ENODEV;
	}

	if (dcdc_nr >= pdev->dcdc_num) {
		pr_err("Incorrect dcdc number\n");
		return -EINVAL;
	}

	addr = pdev->pwr[dcdc_nr].top_dvfs_state_reg;
	bit = pdev->pwr[dcdc_nr].top_dvfs_state_bit;
	mask = pdev->pwr[dcdc_nr].top_dvfs_state_mask;

	ret = regmap_read(pdev->topdvfs_map, addr, &val);
	if (ret) {
		pr_err("Failed to read topdvfs reg[0x%x]\n", addr);
		return ret;
	}

	return (val >> bit) & mask;
}

int sprd_coordinate_dcdc_current_voltage(void *data, u32 dcdc_nr)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	u32 curr_volt;
	u32 addr, bit, mask;
	u32 val;
	int ret;

	if (!pdev->pwr) {
		pr_err("No DCDC Power domain found\n");
		return -ENODEV;
	}

	if (dcdc_nr >= pdev->dcdc_num) {
		pr_err("Incorrect dcdc number\n");
		return -EINVAL;
	}

	/* Tell dvfs module the current voltage for dcdc_nr
	 * before enabling hw dvfs
	 */
	addr = pdev->pwr[dcdc_nr].judge_vol_sw_reg;
	bit = pdev->pwr[dcdc_nr].judge_vol_sw_bit;
	mask = pdev->pwr[dcdc_nr].judge_vol_sw_mask;
	curr_volt =  pdev->pwr[dcdc_nr].judge_vol_val;

	ret = regmap_read(pdev->topdvfs_map, addr, &val);
	if (ret) {
		pr_err("Failed to read topdvfs reg[0x%x]\n", addr);
		return ret;
	}
	val &= ~(mask << bit);
	val |= curr_volt << bit;

	ret = regmap_write(pdev->topdvfs_map, addr, val);
	if (ret) {
		pr_err("Failed to write topdvfs reg[0x%x]\n", addr);
		return ret;
	}

	/* Subsys level dvfs */
	addr = pdev->pwr[dcdc_nr].subsys_dcdc_vol_sw_reg;
	bit = pdev->pwr[dcdc_nr].subsys_dcdc_vol_sw_bit;
	mask = pdev->pwr[dcdc_nr].subsys_dcdc_vol_sw_mask;
	curr_volt  = pdev->pwr[dcdc_nr].subsys_dcdc_vol_sw_vol_val;

	val = readl(pdev->membase + addr) & (~(mask << bit));
	val |= curr_volt << bit;

	writel(val, pdev->membase + addr);

	return 0;
}

int sprd_dcdc_vol_grade_value_setup(void *data, u32 dcdc_nr)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	u32 grade_nr, vol_value, vol_reg, vol_bit, vol_mask;
	u32 i;
	int ret;

	if (!pdev->pwr) {
		pr_err("No DCDC Power domain found\n");
		return -ENODEV;
	}

	if (dcdc_nr >= pdev->dcdc_num) {
		pr_err("Incorrect dcdc number\n");
		return -EINVAL;
	}

	for (i = 0; i < pdev->pwr[dcdc_nr].voltage_grade_num; ++i) {
		grade_nr = pdev->pwr[dcdc_nr].vol_info[i].grade_nr;
		vol_value = pdev->pwr[dcdc_nr].vol_info[i].vol_value;
		vol_reg = pdev->pwr[dcdc_nr].vol_info[i].vol_reg;
		vol_bit = pdev->pwr[dcdc_nr].vol_info[i].vol_bit;
		vol_mask = pdev->pwr[dcdc_nr].vol_info[i].vol_mask;

		ret = regmap_update_bits(pdev->topdvfs_map, vol_reg,
					 vol_mask << vol_bit,
					 vol_value << vol_bit);
		if (ret) {
			pr_err("Error in configuring dcdc grades\n");
			return ret;
		}
	}

	return 0;
}

int sprd_setup_i2c_channel(void *data, u32 dcdc_nr)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;

	if (!pdev->pwr) {
		pr_err("No DCDC Power domain found\n");
		return -ENODEV;
	}

	if (dcdc_nr >= pdev->dcdc_num) {
		pr_err("Incorrect dcdc number\n");
		return -EINVAL;
	}

	if (pdev->pwr[dcdc_nr].i2c_used)
		if (!i2c_add_driver(&cpudvfs_i2c_driver))
			pdev->pwr[dcdc_nr].i2c_client = &pdev->i2c_client;
		else
			pr_err("Failed to add an i2c driver\n");
	else
		pr_info("cluster-%d does not need an i2c channel\n",
			dcdc_nr);

	return 0;
}

static bool  current_clk_volt_check(struct common_clk_volt *real,
				    struct common_clk_volt *expected)
{
	if (real->sel == expected->sel &&
	    real->div == expected->div &&
	    real->voted_volt == expected->voted_volt)
		return true;
	else
		return false;
}

static void dvfs_tuning_fail(struct dvfs_cluster *cluster, u32 dev_num,
			     struct common_clk_volt *real,
			     struct common_clk_volt *work_expected,
			     struct common_clk_volt *idle_expected)
{
	pr_err("The recent dvfs tuning is failing:\n");
	pr_err("%s-device%d:\tsel\tdiv\tvoted_vol\n", cluster->name, dev_num);
	pr_err("\t[real]:\t%d\t%d\t%d\n", real->sel, real->div,
	       real->voted_volt);
	pr_err("\t[work expected]:\t%d\t%d\t%d\n", work_expected->sel,
	       work_expected->div, work_expected->voted_volt);
	if (!idle_expected)
		return;

	pr_err("\t[idle expected]:\t%d\t%d\t%d\n", idle_expected->sel,
	       idle_expected->div, idle_expected->voted_volt);
}

/*
 * Function: The purpose is to determine whether
 * the recent tuning of Hardware-DVFS is successful.
 */
static void dvfs_judge_handler(struct dvfs_cluster *hcluster)
{
	struct host_clk_volt *work_entry, *idle_entry;
	struct cpudvfs_archdata *pdev;
	struct cpudvfs_phy_ops *ops;
	int work_index, idle_index;
	struct common_clk_volt comm_info;
	u32 dev;
       /*
	* Step1:Get the current work index and idle index of
	* a cpu cluster
	* Step2: Determin whether the frequency and voltage of a core
	* which is inside the current cpu cluster match with
	* the hardware map table entry indicated by the work index,
	* if it's failed to match with the work index,
	* then we determin if it's matched with the hardware
	* map table entry indicated by the idle index,
	* if also it's failed to match with idle index,we get the conclusion
	* that the recent tuning of Hardware-DVFS is failing,
	* then give an alarm and return.
	* Step3: If step2 is passed,we perform the step2 for the rest of cores
	* which is inside the current cluster.
	* Step4:If Step3 is passed, return true.
	*/

	pdev = (struct cpudvfs_archdata *)hcluster->parent_dev;

	ops = pdev->phy_ops;

	/*
	 * We can delay here to wait for
	 * finishing hardware dvfs operations
	 */
	work_index = ops->get_dvfs_index(pdev, hcluster->id, true);
	if (work_index < 0)
		return;

	idle_index = ops->get_dvfs_index(pdev, hcluster->id, false);
	if (idle_index < 0)
		return;

	if (ops->get_index_entry_info(pdev, work_index,
				      hcluster->id, (u32 **)&work_entry))
		return;
	if (ops->get_index_entry_info(pdev, idle_index,
				      hcluster->id, (u32 **)&idle_entry))
		return;
	for (dev = 0; dev < hcluster->device_num; ++dev) {
		memset(&comm_info, 0, sizeof(comm_info));
		comm_info.sel = ops->get_cgm_sel_value(pdev,
			hcluster->id, dev);
		comm_info.div = ops->get_cgm_div_value(pdev,
			hcluster->id, dev);
		comm_info.voted_volt = ops->get_cgm_voted_volt(pdev,
			hcluster->id, dev);

		if (current_clk_volt_check(&comm_info,
					   &work_entry->comm_entry)) {
			hcluster->subdevs[dev].curr_index = work_index;
			continue; /*same with the work index*/
		} else if (current_clk_volt_check(&comm_info,
					   &idle_entry->comm_entry)) {
			hcluster->subdevs[dev].curr_index = idle_index;
			continue; /*same with the idle index*/
		} else {
			dvfs_tuning_fail(hcluster, dev, &comm_info,
					 &work_entry->comm_entry,
					 &idle_entry->comm_entry);
			hcluster->subdevs[dev].curr_index = -1;
			return;
		}
	}
}

/*
 * Special hardware dvfs operations in SharkL5 family SOCs
 */

struct cpudvfs_phy_ops sprd_cpudvfs_phy_ops = {
	.dvfs_module_eb = sprd_dvfs_module_eb,
	.mpll_relock_enable = sprd_mpll_relock_enable,
	.mpll_pd_enable = sprd_mpll_pd_enable,
	.auto_tuning_enable = sprd_auto_tuning_enable,
	.hw_dvfs_map_table_init = sprd_hw_dvfs_map_table_init,
	.set_dvfs_work_index = sprd_set_dvfs_work_index,
	.set_dvfs_idle_index = sprd_set_dvfs_idle_index,
	.get_dvfs_index = sprd_get_dvfs_index,
	.get_cgm_sel_value = sprd_get_cgm_sel_value,
	.get_cgm_div_value = sprd_get_cgm_div_value,
	.get_cgm_voted_volt = sprd_get_cgm_voted_volt,
	.get_index_entry_info = sprd_get_index_entry_info,
	.get_index_freq = sprd_get_index_freq,
	.coordinate_dcdc_current_voltage = sprd_coordinate_dcdc_current_voltage,
	.dcdc_vol_grade_value_setup = sprd_dcdc_vol_grade_value_setup,
	.get_sys_dcdc_dvfs_state = sprd_get_sys_dcdc_dvfs_state,
	.get_top_dcdc_dvfs_state = sprd_get_top_dcdc_dvfs_state,
	.setup_i2c_channel = sprd_setup_i2c_channel,
};

static void hardware_dvfs_tuning_result_judge(struct dvfs_cluster *clu)
{
	dvfs_judge_handler(clu);
}

/*
 * sprd_cpufreqhw_table_store - store freq&volt table for cluster
 * @cluster: 0-cluster0, 1-cluster1, 2-scu, 3-periph, 4-gic, 5-atb
 *
 * This is the last known freq, without actually getting it from the driver.
 * Return value will be as same as what is shown in scaling_cur_freq in sysfs.
 */
int sprd_cpudvfs_opp_add(void *data,
			 unsigned int cluster, unsigned long hz_freq,
			 unsigned long u_volt, int opp_idx)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *pcluster;

	if (cluster < 0 || cluster >= pdev->total_cluster_num) {
		pr_err("Cluster number (%d) is overflow\n", cluster);
		return -EINVAL;
	}

	pcluster = pdev->cluster_array[cluster];
	if (!pcluster) {
		pr_err("Cannot find the cluster(%d) device\n", cluster);
		return -ENODEV;
	}

	pcluster->freqvolt[opp_idx].freq = hz_freq;
	pcluster->freqvolt[opp_idx].volt = u_volt;

	if (pcluster->map_idx_max < opp_idx)
		pcluster->map_idx_max = opp_idx;

	return 0;
}

int sprd_cpudvfs_set_target(void *data, u32 cluster, u32 opp_idx)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	struct dvfs_cluster *clu;
	struct i2c_client *client;
	int hw_map_opp_idx = 0, i2c_flag = 0;
	int ret = 0;

	if (cluster < 0 || cluster >= pdev->host_cluster_num) {
		pr_err("The cluster number is overflow");
		return -EINVAL;
	}

	/* Consider the default first entry 'XTL_26M' in hw dvfs table */
	hw_map_opp_idx = opp_idx + 1;

	clu = pdev->cluster_array[cluster];

	if (pdev->pwr[clu->dcdc].i2c_used &&
	    *pdev->pwr[clu->dcdc].i2c_client) {
		client = *pdev->pwr[clu->dcdc].i2c_client;
		i2c_lock_adapter(client->adapter);
		i2c_flag = 1;
	}

	ret = pdev->phy_ops->set_dvfs_work_index(pdev,
				cluster,
				hw_map_opp_idx);
	if (ret) {
		if (i2c_flag)
			i2c_unlock_adapter(client->adapter);
		return ret;
	}

	/* Delay here to wait for finishing dvfs operations by hardware */
	udelay(clu->tuning_latency_us);

	if (clu->needed_judge)
		hardware_dvfs_tuning_result_judge(clu);

	if (i2c_flag)
		i2c_unlock_adapter(client->adapter);

	return ret;
}

bool sprd_cpudvfs_enable(void *data, int cluster, bool enable)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;

	pr_info("Cluster%d's hardware dvfs is %s\n",
		cluster, (enable > 0 ? "enable" : "disable"));

	if (pdev->phy_ops->auto_tuning_enable(pdev, cluster, enable))
		return false;

	return true;
}

unsigned int sprd_cpudvfs_get(void *data, int cluster_id)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;
	int index;

	index = pdev->phy_ops->get_dvfs_index(pdev, cluster_id, true);
	if (index < 0)
		return index;

	pr_info("Get cpu frequency-index%d\n",  index);

	return pdev->phy_ops->get_index_freq(pdev, cluster_id, index);
}

inline bool sprd_cpudvfs_probed(void *data, int cluster)
{
	struct cpudvfs_archdata *pdev = (struct cpudvfs_archdata *)data;

	if (!pdev->probed) {
		pr_err("The cpu dvfs device has not been probed.\n");
		return false;
	}

	if (cluster < 0 || cluster >= pdev->total_cluster_num) {
		pr_err("Cluster%d is overflow!\n",  cluster);
		return false;
	}

	return true;
}

static int dvfs_module_dt_parse(struct cpudvfs_archdata *pdev)
{
	struct property *prop;
	u32 len;

	prop = of_find_property(pdev->of_node, "module-enable-cfg", &len);
	if (!prop) {
		pr_err("No 'module-enable-cfg' property found\n");
		return -ENODEV;
	}

	if (len / sizeof(u32) == 2) {
		of_property_read_u32_index(pdev->of_node, "module-enable-cfg",
					   0, &pdev->module_eb_reg);
		of_property_read_u32_index(pdev->of_node, "module-enable-cfg",
					   1, &pdev->module_eb_bit);
	} else {
		pr_err("Failed to get module enable info\n");
		return -EINVAL;
	}

	return 0;
}

static  int dvfs_mpll_device_dt_parse(struct cpudvfs_archdata *pdev)
{
	u32 idx = 0, mpll_num, size, cfg_num;
	struct property *prop;
	struct device_node *node;
	struct regmap *map;
	int ret = 0;

	prop = of_find_property(pdev->of_node, "mpll-cells", &mpll_num);
	if (!prop) {
		pr_err("No %s node found\n",  "mpll-cells");
		of_node_put(pdev->of_node);
		return -ENODEV;
	}

	mpll_num = mpll_num / sizeof(u32);
	size = mpll_num * sizeof(struct mpll_cfg);
	pdev->mplls = kzalloc(size, GFP_KERNEL);
	if (!pdev->mplls) {
		of_node_put(pdev->of_node);
		return -ENOMEM;
	}

	pdev->mpll_num = mpll_num;

	for (idx = 0; idx < mpll_num; ++idx) {
		node =	of_parse_phandle(pdev->of_node, "mpll-cells", idx);
		if (!node) {
			pr_err("Failed to get mpll node\n");
			ret = -EINVAL;
			goto mpll_free;
		}

		map = syscon_regmap_lookup_by_phandle(node, "sprd,syscon-ang");
		if (!map) {
			pr_err("Cannot get 'sprd,syscon-anag' property\n");
			ret = -EINVAL;
			goto mpll_free;
		}
		pdev->mplls[idx].anag_map = map;

		prop = of_find_property(node, "mpll-rst", &cfg_num);
		if (!prop) {
			pr_err("No %s node found\n",  "mpll-rst");
			ret = -ENODEV;
			goto mpll_free;
		}

		cfg_num = cfg_num / sizeof(u32);
		if (cfg_num == 4) {
			of_property_read_u32_index(node, "mpll-rst", 0,
						   &pdev->mplls[idx].anag_reg);
			of_property_read_u32_index(node, "mpll-rst", 1,
						   &pdev->mplls[idx].POST_DIV);
			of_property_read_u32_index(node, "mpll-rst", 2,
						   &pdev->mplls[idx].ICP);
			of_property_read_u32_index(node, "mpll-rst", 3,
						   &pdev->mplls[idx].N);

		} else {
			pr_err("Failed to get mpll analog register\n");
			ret = -EINVAL;
			goto mpll_free;
		}

		prop = of_find_property(node, "relock-cfg", &cfg_num);
		if (!prop) {
			pr_err("No %s node property\n", "relock-cfg");
			ret = -ENODEV;
			goto mpll_free;
		}

		cfg_num = cfg_num / sizeof(u32);
		if (cfg_num != 2) {
			pr_err("Invalid dts number(%d)\n", cfg_num);
			ret = -ENODEV;
			goto mpll_free;
		}

		of_property_read_u32_index(node, "relock-cfg", 0,
					   &pdev->mplls[idx].relock_reg);
		of_property_read_u32_index(node, "relock-cfg", 1,
					   &pdev->mplls[idx].relock_bit);

		prop = of_find_property(node, "pd-cfg", &cfg_num);
		if (!prop) {
			pr_err("No %s node property\n", "pd-cfg");
			ret = -ENODEV;
			goto mpll_free;
		}

		cfg_num = cfg_num / sizeof(u32);
		if (cfg_num != 2) {
			pr_err("Invalid dts number(%d)\n", cfg_num);
			ret = -ENODEV;
			goto mpll_free;
		}

		of_property_read_u32_index(node, "pd-cfg", 0,
					   &pdev->mplls[idx].pd_reg);
		of_property_read_u32_index(node, "pd-cfg", 1,
					   &pdev->mplls[idx].pd_bit);

		of_node_put(node);
	}

	of_node_put(pdev->of_node);

	return 0;

mpll_free:
	if (node)
		of_node_put(node);
	of_node_put(pdev->of_node);
	kfree(pdev->mplls);
	pdev->mplls = NULL;

	return ret;
}

static int dcdc_voltage_grade_parse(struct device_node *dcdc_node,
				    struct dcdc_pwr *pwr)
{
	struct property *prop;
	const __be32 *list;
	u32 size, count, i;
	int ret = 0;

	prop = of_find_property(dcdc_node, "voltage-grade-num", NULL);
	if (!prop) {
		pr_err("No property 'voltage-grade-num' found\n");
		of_node_put(dcdc_node);
		return -ENODEV;
	}

	of_property_read_u32(dcdc_node, "voltage-grade-num",
			     &pwr->voltage_grade_num);

	size = pwr->voltage_grade_num * sizeof(struct voltage_info);
	pwr->vol_info = kzalloc(size, GFP_KERNEL);
	if (!pwr->vol_info) {
		of_node_put(dcdc_node);
		return -ENOMEM;
	}

	list = of_get_property(dcdc_node, "voltage-grade", &size);
	if (!list || !size) {
		pr_err("No 'voltage-grade' property found\n");
		ret = -ENODEV;
		goto err_mem_free;
	}

	count = size / (sizeof(u32) * 5);
	if (count != pwr->voltage_grade_num) {
		pr_err("The num of voltage grades in is not matched\n");
		ret = -ENODEV;
		goto err_mem_free;
	}

	for (i = 0; i < count; i++) {
		pwr->vol_info[i].grade_nr = be32_to_cpu(*list++);
		pwr->vol_info[i].vol_value = be32_to_cpu(*list++);
		pwr->vol_info[i].vol_reg = be32_to_cpu(*list++);
		pwr->vol_info[i].vol_bit = be32_to_cpu(*list++);
		pwr->vol_info[i].vol_mask = be32_to_cpu(*list++);
	}

	of_node_put(dcdc_node);

	return 0;

err_mem_free:
	of_node_put(dcdc_node);
	kfree(pwr->vol_info);
	pwr->vol_info = NULL;

	return ret;
}

static int dcdc_pwr_dt_parse(struct cpudvfs_archdata *pdev)
{
	struct device_node *node, *dcdc_node;
	struct property *prop;
	u32 nr, ix, num;
	struct regmap *map;
	int ret;
	int i2c_flag;

	node = of_parse_phandle(pdev->of_node, "topdvfs-controller", 0);
	if (!node) {
		pr_err("Failed to find 'topdvfs-controller' node\n");
		ret = -EINVAL;
		goto err_out;
	}

	map = syscon_node_to_regmap(node);
	if (IS_ERR(map)) {
		pr_err("No regmap for syscon topdvfs\n");
		ret = -ENODEV;
		goto err_out;
	}

	pdev->topdvfs_of_node = node;

	pdev->topdvfs_map = map;

	if (!of_find_property(node, "cpu-dcdc-cells", &nr)) {
		pr_err("Failed to find 'cpu-dcdc-cells' property\n");
		ret = -EINVAL;
		goto err_out;
	}

	nr = nr / sizeof(u32);
	pdev->dcdc_num = nr;

	if (!pdev->pwr) {
		pdev->pwr = kzalloc(nr * sizeof(struct dcdc_pwr), GFP_KERNEL);
		if (!pdev->pwr) {
			ret = -ENOMEM;
			goto err_out;
		}
	}

	/* TOP dvfs level - DCDC */
	for (ix = 0; ix < nr; ix++) {
		dcdc_node = of_parse_phandle(node, "cpu-dcdc-cells", ix);
		if (!dcdc_node) {
			pr_err("Failed to find '%s' node-%d\n",
			       "cpu-dcdc-cells", ix);
			ret = -EINVAL;
			goto err_pwr_free;
		}

		sprintf(pdev->pwr[ix].name, "DCDC_CPU%d", ix);

		ret = dcdc_voltage_grade_parse(dcdc_node, &pdev->pwr[ix]);
		if (ret) {
			pr_err("Failed to parse voltage grade info\n");
			goto err_pwr_free;
		}

		of_property_read_u32_index(dcdc_node, "dcdc-dvfs-enable",
					   0, &pdev->pwr[ix].dvfs_ctl_reg);
		of_property_read_u32_index(dcdc_node, "dcdc-dvfs-enable",
					   1, &pdev->pwr[ix].dvfs_ctl_bit);
		of_property_read_u32_index(dcdc_node, "dcdc-dvfs-enable",
					   2, &pdev->pwr[ix].dvfs_eb);

		prop = of_find_property(dcdc_node, "dcdc-judge-vol-sw", &num);
		if (!prop) {
			pr_err("No %s property found\n", "dcdc-judge-vol-sw");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		num = num / sizeof(u32);
		if (num != 4) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		of_property_read_u32_index(dcdc_node, "dcdc-judge-vol-sw", 0,
					   &pdev->pwr[ix].judge_vol_sw_reg);

		of_property_read_u32_index(dcdc_node, "dcdc-judge-vol-sw", 1,
					   &pdev->pwr[ix].judge_vol_sw_bit);

		of_property_read_u32_index(dcdc_node, "dcdc-judge-vol-sw", 2,
					   &pdev->pwr[ix].judge_vol_sw_mask);

		of_property_read_u32_index(dcdc_node, "dcdc-judge-vol-sw", 3,
					   &pdev->pwr[ix].judge_vol_val);

		prop = of_find_property(dcdc_node,
					"dcdc-subsys-tune-enable", &num);
		if (!prop) {
			pr_err("No %s property found\n",
			       "dcdc-subsys-tune-enable");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		num = num / sizeof(u32);
		if (num != 3) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		of_property_read_u32_index(dcdc_node,
					   "dcdc-subsys-tune-enable", 0,
					   &pdev->pwr[ix].subsys_tune_ctl_reg);
		of_property_read_u32_index(dcdc_node,
					   "dcdc-subsys-tune-enable", 1,
					   &pdev->pwr[ix].subsys_tune_ctl_bit);
		of_property_read_u32_index(dcdc_node,
					   "dcdc-subsys-tune-enable", 2,
					   &pdev->pwr[ix].subsys_tune_eb);

		prop = of_find_property(dcdc_node, "chnl-in-i2c", NULL);
		if (!prop) {
			pr_err("No %s property found\n",
			       "chnl-in-i2c");
			ret = -EINVAL;
			goto err_pwr_free;
		}

		of_property_read_u32(dcdc_node, "chnl-in-i2c", &i2c_flag);
		if (i2c_flag == 1) {
			prop = of_find_property(dcdc_node,
						"top-dvfs-i2c-state", &num);
			if (!prop) {
				pr_err("No %s property found\n",
				       "top-dvfs-i2c-state");
				ret = -EINVAL;
				goto err_pwr_free;
			}

			num = num / sizeof(u32);
			if (num != 3) {
				pr_err("Invalid dts configuration\n");
				ret = -EINVAL;
				goto err_pwr_free;
			}

			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-i2c-state",
						   0,
					&pdev->pwr[ix].top_dvfs_state_reg);
			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-i2c-state",
						   1,
					&pdev->pwr[ix].top_dvfs_state_bit);
			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-i2c-state",
						   2,
					&pdev->pwr[ix].top_dvfs_state_mask);

			pdev->pwr[ix].i2c_used = true;

		} else if (i2c_flag == 0) {
			prop = of_find_property(dcdc_node,
						"top-dvfs-adi-state", &num);
			if (!prop) {
				pr_err("No %s property found\n",
				       "top-dvfs-adi-state");
				ret = -EINVAL;
				goto err_pwr_free;
			}

			num = num / sizeof(u32);
			if (num != 3) {
				pr_err("Invalid dts configuration\n");
				ret = -EINVAL;
				goto err_pwr_free;
			}

			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-adi-state",
						   0,
					&pdev->pwr[ix].top_dvfs_state_reg);
			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-adi-state",
						   1,
					&pdev->pwr[ix].top_dvfs_state_bit);
			of_property_read_u32_index(dcdc_node,
						   "top-dvfs-adi-state",
						   2,
					&pdev->pwr[ix].top_dvfs_state_mask);

			pdev->pwr[ix].i2c_used = false;
		}

		of_node_put(dcdc_node);
	}

	/* Subsys dvfs level - DCDC */
	if (!of_find_property(pdev->of_node, "apcpu-dvfs-dcdc-cells", &nr)) {
		pr_err("Failed to find 'apcpu-dvfs-dcdc-cells' node\n");
		ret = -EINVAL;
		goto err_pwr_free;
	}

	nr = nr / sizeof(u32);
	if (nr != pdev->dcdc_num) {
		pr_err("The number of DCDCs is not matched in dts\n");
		ret = -EINVAL;
		goto err_pwr_free;
	}

	for (ix = 0; ix < nr; ix++) {
		dcdc_node = of_parse_phandle(pdev->of_node,
					     "apcpu-dvfs-dcdc-cells", ix);
		if (!dcdc_node) {
			pr_err("Failed to find '%s' node-%d\n",
			       "apcpu-dvfs-dcdc-cells", ix);
			ret = -EINVAL;
			goto err_pwr_free;
		}

		prop = of_find_property(dcdc_node,
					"subsys-dcdc-vol-sw", &num);
		if (!prop) {
			pr_err("No %s property found\n",
			       "subsys-dcdc-vol-sw");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		num = num / sizeof(u32);
		if (num != 4) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		of_property_read_u32_index(dcdc_node,
					   "subsys-dcdc-vol-sw", 0,
				&pdev->pwr[ix].subsys_dcdc_vol_sw_reg);
		of_property_read_u32_index(dcdc_node,
					   "subsys-dcdc-vol-sw", 1,
				&pdev->pwr[ix].subsys_dcdc_vol_sw_bit);
		of_property_read_u32_index(dcdc_node,
					   "subsys-dcdc-vol-sw", 2,
				&pdev->pwr[ix].subsys_dcdc_vol_sw_mask);
		of_property_read_u32_index(dcdc_node,
					   "subsys-dcdc-vol-sw", 3,
				&pdev->pwr[ix].subsys_dcdc_vol_sw_vol_val);

		prop = of_find_property(dcdc_node,
					"subsys-dvfs-state", &num);
		if (!prop) {
			pr_err("No %s property found\n",
			       "subsys-dvfs-state");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		num = num / sizeof(u32);
		if (num != 3) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_pwr_free;
		}

		of_property_read_u32_index(dcdc_node,
					   "subsys-dvfs-state", 0,
				&pdev->pwr[ix].subsys_dvfs_state_reg);
		of_property_read_u32_index(dcdc_node,
					   "subsys-dvfs-state", 1,
				&pdev->pwr[ix].subsys_dvfs_state_bit);
		of_property_read_u32_index(dcdc_node,
					   "subsys-dvfs-state", 2,
				&pdev->pwr[ix].subsys_dvfs_state_mask);

		of_node_put(dcdc_node);
	}

	of_node_put(node);
	of_node_put(pdev->of_node);

	return 0;

err_pwr_free:
	if (dcdc_node)
		of_node_put(dcdc_node);
	kfree(pdev->pwr);
	pdev->pwr = NULL;

err_out:
	if (node)
		of_node_put(node);
	of_node_put(pdev->of_node);

	return ret;
}

static
int dvfs_cluster_info_dt_parse(struct device_node *parent, void *data)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)data;
	struct cpudvfs_archdata *pdev;
	struct device_node *node, *sub_dev_np;
	struct property *prop;
	const char *dcdc_name;
	u32  num, idx, size;
	const char *clu_name;
	u32 dev_nr;
	int ret;

	pdev = (struct cpudvfs_archdata *)cluster->parent_dev;

	if (cluster->id >= pdev->total_cluster_num) {
		cluster->existed = false;
		cluster->of_node = NULL;
		return 0;
	}

	node = of_parse_phandle(parent, "cpudvfs-clusters", cluster->id);

	of_property_read_string(node, "cluster-name", &clu_name);
	if (strcmp(clu_name, cluster->name)) { /* node is not matched */
		cluster->existed = false;
		cluster->of_node = NULL;
		of_node_put(node);
		of_node_put(parent);
		return 0;
	}

	cluster->existed = true;
	cluster->of_node = node;

	prop = of_find_property(node, "cluster-devices", &dev_nr);
	if (!prop) {
		pr_err("No %s node found\n",  "cluster-devices");
		ret = -ENODEV;
		goto err_out;
	}

	dev_nr = dev_nr / sizeof(u32);
	size = dev_nr * sizeof(struct sub_device);
	cluster->subdevs = kzalloc(dev_nr * size, GFP_KERNEL);
	if (!cluster->subdevs) {
		ret = -ENOMEM;
		goto err_out;
	}

	for (idx = 0; idx < dev_nr; ++idx) {
		sub_dev_np = of_parse_phandle(node, "cluster-devices", idx);
		if (!sub_dev_np) {
			pr_err("Failed to get device%d in %s cluster\n",
			       idx, cluster->name);
			ret = -ENODEV;
			goto err_subsys_free;
		}

		cluster->subdevs[idx].of_node = sub_dev_np;
		of_property_read_string_index(sub_dev_np, "device-name",
					      0, &cluster->subdevs[idx].name);
		if (!cluster->subdevs[idx].name) {
			pr_err("No 'device-name' property found\n");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		prop = of_find_property(sub_dev_np, "sel-get", &num);
		if (!prop) {
			pr_err("No %s node found\n",  "sel-get");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		num = num / sizeof(u32);
		if (num != 3) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_subsys_free;
		}
		of_property_read_u32_index(sub_dev_np, "sel-get", 0,
					   &cluster->subdevs[idx].sel_reg);
		of_property_read_u32_index(sub_dev_np, "sel-get", 1,
					   &cluster->subdevs[idx].sel_bit);
		of_property_read_u32_index(sub_dev_np, "sel-get", 2,
					   &cluster->subdevs[idx].sel_mask);

		prop = of_find_property(sub_dev_np, "div-get", &num);
		if (!prop) {
			pr_err("No %s node found\n",  "div-get");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		num = num / sizeof(u32);
		if (num != 3) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		of_property_read_u32_index(sub_dev_np, "div-get", 0,
					   &cluster->subdevs[idx].div_reg);
		of_property_read_u32_index(sub_dev_np, "div-get", 1,
					   &cluster->subdevs[idx].div_bit);
		of_property_read_u32_index(sub_dev_np, "div-get", 2,
					   &cluster->subdevs[idx].div_mask);

		prop = of_find_property(sub_dev_np, "vol-get", &num);
		if (!prop) {
			pr_err("No %s node found\n",  "vol-get");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		num = num / sizeof(u32);
		if (num != 3) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		of_property_read_u32_index(sub_dev_np, "vol-get", 0,
					   &cluster->subdevs[idx].vol_reg);
		of_property_read_u32_index(sub_dev_np, "vol-get", 1,
					   &cluster->subdevs[idx].vol_bit);
		of_property_read_u32_index(sub_dev_np, "vol-get", 2,
					   &cluster->subdevs[idx].vol_mask);

		cluster->subdevs[idx].device_id = dev_nr;
		pdev->total_device_num++;

		of_node_put(sub_dev_np);
	}

	if (of_find_property(node, "tuning-latency-us", NULL))
		of_property_read_u32(node, "tuning-latency-us",
				     &cluster->tuning_latency_us);

	if (of_find_property(node, "tuning-result-judge", NULL))
		of_property_read_u32(node, "tuning-result-judge",
				     &cluster->needed_judge);

	prop = of_find_property(node, "work-index-cfg", &num);
	if (!prop) {
		pr_err("No %s property found\n",
		       "work-index-cfg");
		ret = -ENODEV;
		goto err_subsys_free;
	}

	num = num / sizeof(u32);
	if (num != 2) {
		pr_err("Invalid dts configuration\n");
		ret = -ENODEV;
		goto err_subsys_free;
	}

	of_property_read_u32_index(node, "work-index-cfg", 0,
				   &cluster->work_index_reg);

	of_property_read_u32_index(node, "work-index-cfg", 1,
				   &cluster->work_index_mask);

	prop = of_find_property(node, "idle-index-cfg", &num);
	if (!prop) {
		pr_err("No %s property found\n",
		       "idle-index-cfg");
		ret = -ENODEV;
		goto err_subsys_free;
	}

	num = num / sizeof(u32);
	if (num != 2) {
		pr_err("Invalid dts configuration\n");
		ret = -ENODEV;
		goto err_subsys_free;
	}

	of_property_read_u32_index(node, "idle-index-cfg", 0,
				   &cluster->idle_index_reg);

	of_property_read_u32_index(node, "idle-index-cfg", 1,
				   &cluster->idle_index_mask);

	prop = of_find_property(node, "tuning-func-cfg", &num);
	if (prop) { /* host clusters do not have this property */
		num = num / sizeof(u32);
		if (num != 2) {
			pr_err("Invalid dts configuration\n");
			ret = -ENODEV;
			goto err_subsys_free;
		}

		of_property_read_u32_index(node, "tuning-func-cfg", 0,
					   &cluster->tuning_fun_reg);

		of_property_read_u32_index(node, "tuning-func-cfg", 1,
					   &cluster->tuning_fun_bit);
	}

	of_property_read_string(node, "dcdc-name", &dcdc_name);
	if (!strcmp(dcdc_name, "DCDC_CPU0")) {
		cluster->dcdc = DCDC_CPU0;
	} else if (!strcmp(dcdc_name, "DCDC_CPU1")) {
		cluster->dcdc = DCDC_CPU1;
	} else {
		pr_err("No DCDC name for cluster found\n");
		ret = -EINVAL;
		goto err_subsys_free;
	}

	cluster->device_num = dev_nr;

	if (!of_property_read_u32(node, "row-num", &num)) {
		cluster->tbl_row_num = num;
	} else {
		pr_err("The row mum of the %s cluster map tbl is lost\n",
		       cluster->name);
		ret = -EINVAL;
		goto err_subsys_free;
	}

	cluster->map_tbl_regs = kzalloc(sizeof(u32) * cluster->tbl_row_num,
					GFP_KERNEL);
	if (!cluster->map_tbl_regs) {
		ret = -ENOMEM;
		goto err_subsys_free;
	}

	for (idx = 0; idx < cluster->tbl_row_num; ++idx)
		of_property_read_u32_index(node, "map-tbl-regs",
					   idx, &cluster->map_tbl_regs[idx]);

	if (!of_property_read_u32(node, "column-num", &num)) {
		cluster->tbl_column_num = num;
	} else {
		pr_err("The column mum of %s cluster map tbl is lost\n",
		       cluster->name);
		ret = -EINVAL;
		goto err_map_tbl_regs_free;
	}

	cluster->column_entry_bit =
				kzalloc(sizeof(u32) * cluster->tbl_column_num,
					GFP_KERNEL);

	if (!cluster->column_entry_bit) {
		ret = -ENOMEM;
		goto err_map_tbl_regs_free;
	}

	cluster->column_entry_mask =
				kzalloc(sizeof(u32) * cluster->tbl_column_num,
					GFP_KERNEL);

	if (!cluster->column_entry_mask) {
		ret = -ENOMEM;
		goto err_column_entry_bit_free;
	}

	for (idx = 0; idx < cluster->tbl_column_num; ++idx) {
		of_property_read_u32_index(node, "column-entry-start-bit",
					   idx,
					   &cluster->column_entry_bit[idx]);
		of_property_read_u32_index(node, "column-entry-mask",
					   idx,
					   &cluster->column_entry_mask[idx]);
	}

	of_node_put(node);
	of_node_put(parent);

	return cluster->tbl_row_num;

err_column_entry_bit_free:
	kfree(cluster->column_entry_bit);
	cluster->column_entry_bit = NULL;
err_map_tbl_regs_free:
	kfree(cluster->map_tbl_regs);
	cluster->map_tbl_regs = NULL;
err_subsys_free:
	if (sub_dev_np)
		of_node_put(sub_dev_np);
	kfree(cluster->subdevs);
	cluster->subdevs = NULL;
err_out:
	of_node_put(node);
	of_node_put(parent);

	return ret;
}

static int cpudvfs_cluster_dt_parse(void *clu)
{
	struct dvfs_cluster *cluster = (struct dvfs_cluster *)clu;
	struct cpudvfs_archdata *pdev;
	u32 num = 0;

	pdev = (struct cpudvfs_archdata *)cluster->parent_dev;

	num = dvfs_cluster_info_dt_parse(pdev->of_node, cluster);

	if (!num && !cluster->of_node)
		return 0;/* no current cluster */

	if (num < 0 || (!num && cluster->of_node))
		return -EINVAL;

	return num;
}

static int dvfs_device_dt_parse(struct cpudvfs_archdata *pdev)
{
	struct dvfs_cluster *cluster, *temp_cluster;
	int entry_num, idx, jx, size, id = 0;
	struct property *prop;
	u32 num;
	int ret = 0;
	bool is_host;

	if (!pdev->phost_cluster || !pdev->pslave_cluster) {
		pr_err("No cluster sets found.\n");
		return -EINVAL;
	}

	ret = dvfs_module_dt_parse(pdev);
	if (ret)
		return ret;

	prop = of_find_property(pdev->of_node, "cpudvfs-clusters", &num);
	if (!prop) {
		pr_err("No %s node found\n",  "cpudvfs-clusters");
		of_node_put(pdev->of_node);
		return -ENODEV;
	}

	/* Real number of total cluster */
	pdev->total_cluster_num = num / sizeof(u32);

	pr_info("total_cluster_num = %d\n", pdev->total_cluster_num);

	temp_cluster = pdev->phost_cluster;
	size = pdev->host_cluster_num;
	is_host = true;
	/* Just 2 kinds of cluster: host & slave */
	for (idx = 0; idx < 2; ++idx) {
		cluster = temp_cluster;
		for (jx = 0; jx < size; jx++) {
			cluster[jx].parent_dev = pdev;
			cluster[jx].id = id;
			entry_num = cluster[jx].driver->parse(&cluster[jx]);
			if (entry_num > 0) {
				cluster[jx].freqvolt =
				kzalloc(entry_num * sizeof(struct plat_opp),
					GFP_KERNEL);
				if (!cluster[jx].freqvolt) {
					ret = -ENOMEM;
					goto err_freqvolt_free;
				}
				pdev->cluster_array[id] = &cluster[jx];
				cluster[jx].is_host_cluster = is_host;
				id++;
			} else if (entry_num == 0) { /* cluster does not exist*/
				continue;
			} else {
				return entry_num;
			}
		}

		temp_cluster = pdev->pslave_cluster;
		size = pdev->slave_cluster_num;
		is_host = false;
	}

	/* Parse mpll */
	ret = dvfs_mpll_device_dt_parse(pdev);
	if (ret)
		goto err_freqvolt_free;

	/* Parse dcdc pwr */
	ret = dcdc_pwr_dt_parse(pdev);
	if (ret)
		goto err_freqvolt_free;

	pdev->parse_done = true;

	of_node_put(pdev->of_node);

	pr_info("Finish to parse cpu dvfs device\n");

	return 0;

err_freqvolt_free:
	of_node_put(pdev->of_node);
	id = pdev->total_cluster_num - 1;
	while (id >= 0) {
		kfree(pdev->cluster_array[id]->freqvolt);
		pdev->cluster_array[id]->freqvolt = NULL;
		id--;
	}
	return ret;
}

/*
 * sprd_cpufreqhw_common_init - configure hardware dvfs,
 * not including enabling hardware dvfs function
 */

static int sprd_cpudvfs_common_init(struct cpudvfs_archdata *pdev)
{
	int ret;
	u32 ix, addr, bit;

	ret = pdev->phy_ops->dvfs_module_eb(pdev);
	if (ret) {
		pr_err("DVFS module has not been enabled\n");
		return ret;
	}

	for (ix = 0; ix < pdev->mpll_num; ++ix) {
		ret = pdev->phy_ops->mpll_relock_enable(pdev, ix, true);
		if (ret)
			return ret;
		ret = pdev->phy_ops->mpll_pd_enable(pdev, ix, true);
		if (ret)
			return ret;
		addr = pdev->mplls[ix].anag_reg;
		bit = (1 << pdev->mplls[ix].POST_DIV) |
		      (1 << pdev->mplls[ix].ICP) |
		      (1 << pdev->mplls[ix].N);
		ret = regmap_update_bits(pdev->mplls[ix].anag_map,
					 addr, bit, ~bit);
		if (ret) {
			pr_err("Error in configuring MPLL\n");
			return ret;
		}
	}

	for (ix = 0; ix < pdev->dcdc_num; ++ix) {
		ret = pdev->phy_ops->dcdc_vol_grade_value_setup(pdev, ix);
		if (ret)
			return ret;
		ret = pdev->phy_ops->coordinate_dcdc_current_voltage(pdev, ix);
		if (ret)
			return ret;
		ret = pdev->phy_ops->setup_i2c_channel(pdev, ix);
		if (ret)
			return ret;
	}

	ret = pdev->phy_ops->hw_dvfs_map_table_init(pdev);
	if (ret) {
		pr_err("Error in initializing dvfs map tbls\n");
		return ret;
	}

	return 0;
}

static struct dvfs_cluster_driver default_cluster_ops = {
	.parse = cpudvfs_cluster_dt_parse,
	.map_tbl_init = dvfs_map_tbl_init,
	.set_index = cluster_set_index,
	.get_index = cluster_get_index,
	.get_cgm_sel = get_device_cgm_sel,
	.get_cgm_div = get_device_cgm_div,
	.get_voted_volt = get_device_voted_volt,
	.get_entry_info = get_index_entry_info,
	.get_freq =  get_cluster_freq,
};

struct dvfs_cluster global_host_cluster[] = {
	{
		.name = "lit-core-cluster",
		.enum_name = DVFS_CLUSTER_LIT_CORE,
		.dts_tbl_name = "lit-core-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = host_cluster_auto_tuning_enable,
	},
	{
		.name = "big-core-cluster",
		.enum_name = DVFS_CLUSTER_BIG_CORE,
		.dts_tbl_name = "big-core-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = host_cluster_auto_tuning_enable,
	},
};

struct dvfs_cluster global_slave_cluster[] = {
	{
		.name = "scu-cluster",
		.enum_name = DVFS_CLUSTER_SCU,
		.dts_tbl_name = "scu-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = slave_cluster_auto_tuning_enable,
	},
	{
		.name = "periph-cluster",
		.enum_name = DVFS_CLUSTER_PERIPH,
		.dts_tbl_name = "periph-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = slave_cluster_auto_tuning_enable,
	},
	{
		.name = "gic-cluster",
		.enum_name = DVFS_CLUSTER_GIC,
		.dts_tbl_name = "gic-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = slave_cluster_auto_tuning_enable,
	},
	{
		.name = "atb-cluster",
		.enum_name = DVFS_CLUSTER_ATB,
		.dts_tbl_name = "atb-dvfs-tbl",
		.driver = &default_cluster_ops,
		.auto_tuning_enable = slave_cluster_auto_tuning_enable,
	},
};

/*
 * Hardware DVFS common operations in different plats, such as
 * sharkl3 and sharkl5 family SOCs
 */
static struct sprd_cpudvfs_device cpudvfs_plat_dev = {
	.name = "sprd-cpudvfs-plat",
	.ops = {
		.probed = sprd_cpudvfs_probed,
		.enable = sprd_cpudvfs_enable,
		.opp_add = sprd_cpudvfs_opp_add,
		.set = sprd_cpudvfs_set_target,
		.get = sprd_cpudvfs_get,
	},
};

static int sprd_cpudvfs_probe(struct platform_device *pdev)
{
	struct cpudvfs_archdata *parchdev;
	struct regmap *aon_reg;
	struct device_node *np;
	struct resource	*res;
	void __iomem *base;
	int ret;

	parchdev = devm_kzalloc(&pdev->dev,
				sizeof(struct cpudvfs_archdata), GFP_KERNEL);
	if (!parchdev)
		return -ENOMEM;

	np = pdev->dev.of_node;
	if (!np) {
		dev_err(&pdev->dev, "Have not found device node!\n");
		ret = -ENODEV;
		goto err_out;
	}

	parchdev->of_node = np;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (!base) {
		dev_err(&pdev->dev, "Failed to remap the top dvfs register\n");
		ret = -ENOMEM;
		goto err_out;
	}
	parchdev->membase = base;

	aon_reg = syscon_regmap_lookup_by_phandle(np, "sprd,syscon-enable");
	if (!aon_reg) {
		dev_err(&pdev->dev, "Failed to get aon apb register map\n");
		ret = -ENODEV;
		goto err_mem_unmap;
	}
	parchdev->aon_apb_reg_base = aon_reg;

	parchdev->phy_ops = &sprd_cpudvfs_phy_ops;

	parchdev->phost_cluster = global_host_cluster;

	parchdev->pslave_cluster = global_slave_cluster;

	parchdev->host_cluster_num = ARRAY_SIZE(global_host_cluster);

	parchdev->slave_cluster_num = ARRAY_SIZE(global_slave_cluster);

	ret = dvfs_device_dt_parse(parchdev);
	if (ret)
		goto err_mem_unmap;

	ret = sprd_cpudvfs_common_init(parchdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize hw dvfs device\n");
		goto err_mem_unmap;
	}

	cpudvfs_sysfs_create(parchdev);

	parchdev->probed = true;

	cpudvfs_plat_dev.archdata = parchdev;

	platform_set_drvdata(pdev, &cpudvfs_plat_dev);

	ret = sprd_hardware_dvfs_device_register(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register a hardware cpufreq!\n");
		goto err_mem_unmap;
	}

	pr_info("Finish to probe the sprd hardware dvfs device.\n");

	return 0;

err_mem_unmap:
	devm_iounmap(&pdev->dev, parchdev->membase);
	parchdev->membase = NULL;

err_out:
	return ret;
}

static int sprd_cpudvfs_remove(struct platform_device *pdev)
{
	i2c_del_driver(&cpudvfs_i2c_driver);
	return 0;
}

static struct platform_driver sprd_cpudvfs_driver = {
	.probe = sprd_cpudvfs_probe,
	.remove = sprd_cpudvfs_remove,
	.driver = {
		.name = "sprd_cpudvfs",
		.of_match_table = sprd_cpudvfs_of_match,
	},
};

static int __init sprd_cpudvfs_init(void)
{
	return platform_driver_register(&sprd_cpudvfs_driver);
}

static void __exit sprd_cpudvfs_exit(void)
{
	platform_driver_unregister(&sprd_cpudvfs_driver);
}

subsys_initcall(sprd_cpudvfs_init);
module_exit(sprd_cpudvfs_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jack Liu<Jack.Liu@unisoc.com>");
MODULE_DESCRIPTION("sprd hardware dvfs driver");
