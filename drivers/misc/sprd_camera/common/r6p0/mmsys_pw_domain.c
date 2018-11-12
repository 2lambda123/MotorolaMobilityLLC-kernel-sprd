/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
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
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <video/sprd_mmsys_pw_domain.h>


/* Macro Definitions */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "MM_PW: %d %d %s: " \
	fmt, current->pid, __LINE__, __func__


static const char * const tb_name[] = {
	"chip_id0", "chip_id1",
	"force_shutdown", "shutdown_en", /* clear */
	"power_state", /* on: 0; off:7 */
	"ckg_eb",
	"cphy_ckg_eb",
	"qos_ar", "qos_aw",

	/* whole register,ahb_eb,rst,ckg_eb */
	"ahb_rst",
	"ahb_ckg_eb",

};
enum  {
	_e_chip_id0 = 0,
	_e_chip_id1,
	_e_force_shutdown,
	_e_shutdown_en,
	_e_power_state,
	_e_ckg_eb,
	_e_cphy_ckg_eb,
	_e_qos_ar,
	_e_qos_aw,

	/* whole register,ahb_eb,rst,ckg_eb */
	_e_ahb_rst,
	_e_ahb_ckg_eb,
};

struct register_gpr {
	struct regmap *gpr;
	uint32_t reg;
	uint32_t mask;
};

struct mmsys_power_info {
	struct mutex mlock;
	atomic_t users_pw;
	atomic_t users_clk;
	atomic_t inited;
	uint32_t mm_qos_ar, mm_qos_aw;
	struct register_gpr regs[sizeof(tb_name) / sizeof(void *)];

	struct clk *mm_eb;
	struct clk *ahb_clk;
	struct clk *ahb_clk_parent;
	struct clk *ahb_clk_default;

	struct clk *mtx_clk;
	struct clk *mtx_clk_parent;
	struct clk *mtx_clk_default;
};

/* L5 */
#define PD_MM_DOWN_FLAG			(0x7 << 16)

static struct mmsys_power_info *pw_info;
#ifndef TEST_ON_HAPS
#define TEST_ON_HAPS /* on haps, will remove this after bringup */
#endif
/* test on haps */
#if    (defined(TEST_ON_HAPS))
static unsigned int read_hwaddress(unsigned int addr)
{
	void __iomem *io_tmp = NULL;
	unsigned int val;

	io_tmp = ioremap_nocache(addr, 0x4);
	val = __raw_readl(io_tmp);
	iounmap(io_tmp);

	return val;
}

static void write_hwaddress_mask(unsigned int addr, unsigned int mask,
				unsigned int val)
{
	void __iomem *io_tmp = NULL;
	unsigned int tmp;

	io_tmp = ioremap_nocache(addr, 0x4);
	tmp = __raw_readl(io_tmp);
	val = (val & mask) | (tmp & (~mask));
	__raw_writel(val, io_tmp);
	val = __raw_readl(io_tmp);
	iounmap(io_tmp);
}
#else

/* The position first bit is 1 from low */
static int lsb_bit1(uint32_t tmp)
{
	int num = 0;

	if (!(tmp & 0xFFFF)) {
		num += 16;
		tmp >>= 16;
	}
	if (!(tmp & 0xFF)) {
		num += 8;
		tmp >>= 8;
	}
	if (!(tmp & 0xF)) {
		num += 4;
		tmp >>= 4;
	}
	if (!(tmp & 0x3)) {
		num += 2;
		tmp >>= 2;
	}
	if (!(tmp & 0x1))
		num += 1;

	return num;
}
#endif


static int check_drv_init(void)
{
	int ret = 0;

	if (!pw_info)
		ret = -1;
	if (atomic_read(&pw_info->inited) == 0)
		ret = -2;
	return ret;
}

static int mmsys_power_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *tregmap;
	uint32_t syscon_args[2];
	const char *pname;
	int i = 0;
	int ret = 0;

	pw_info = devm_kzalloc(&pdev->dev, sizeof(*pw_info), GFP_KERNEL);
	if (!pw_info)
		return -ENOMEM;

	pr_info("E\n");
	mutex_init(&pw_info->mlock);
	/* read global register */
	for (i = 0; i < ARRAY_SIZE(tb_name); i++) {
		pname = tb_name[i];
		tregmap =  syscon_regmap_lookup_by_name(np, pname);
		if (IS_ERR(tregmap)) {
			pr_err("Read DTS %s regmap fail\n", pname);
			return -ENODEV;
		}
		ret = syscon_get_args_by_name(np, pname, 2, syscon_args);
		if (ret != 2) {
			pr_err("Read DTS %s args fail, ret = %d\n", pname, ret);
			return -ENODEV;
		}
		ret = 0;
		pw_info->regs[i].gpr = tregmap;
		pw_info->regs[i].reg = syscon_args[0];
		pw_info->regs[i].mask = syscon_args[1];
		pr_debug("DTS[%s]%p, 0x%x, 0x%x\n", pname,
			pw_info->regs[i].gpr, pw_info->regs[i].reg,
			pw_info->regs[i].mask);
	}
	/* read qos ar aw */
	ret = of_property_read_u32_index(np, "mm_qos", 0, &pw_info->mm_qos_ar);
	if (ret) {
		/* set ar default */
		pw_info->mm_qos_ar = 0xD;
		pr_info("read qos ar fail, default %d\n", pw_info->mm_qos_ar);
	}
	ret = of_property_read_u32_index(np, "mm_qos", 1, &pw_info->mm_qos_aw);
	if (ret) {
		/* set aw default */
		pw_info->mm_qos_aw = 0xD;
		pr_info("read qos aw fail, default %d\n", pw_info->mm_qos_aw);
	}

	/* read mm ahb clk */
	pw_info->mm_eb = devm_clk_get(&pdev->dev, "mm_eb");
	if (IS_ERR(pw_info->mm_eb)) {
		ret = PTR_ERR(pw_info->mm_eb);
		pr_err("Get mm_eb clk fail, ret %d\n", ret);
		return ret;
	}
	pw_info->ahb_clk = devm_clk_get(&pdev->dev, "mm_ahb_eb");
	if (IS_ERR(pw_info->ahb_clk)) {
		ret = PTR_ERR(pw_info->ahb_clk);
		pr_err("Get mm_ahb_eb clk fail, ret %d\n", ret);
		return ret;
	}
	pw_info->ahb_clk_parent = devm_clk_get(&pdev->dev, "clk_mm_ahb_parent");
	if (IS_ERR(pw_info->ahb_clk_parent)) {
		ret = PTR_ERR(pw_info->ahb_clk_parent);
		pr_err("Get mm_ahb_eb clk fail, ret %d\n", ret);
		return ret;
	}
	pw_info->ahb_clk_default = pw_info->ahb_clk_parent;
	/* read mm mtx clk */
	pw_info->mtx_clk = devm_clk_get(&pdev->dev, "mm_mtx_eb");
	if (IS_ERR(pw_info->mtx_clk)) {
		ret = PTR_ERR(pw_info->mtx_clk);
		pr_err("Get mm_mtx_eb clk fail, ret %d\n", ret);
		return -ENODEV;
	}
	pw_info->mtx_clk_parent = devm_clk_get(&pdev->dev, "clk_mm_mtx_parent");
	if (IS_ERR(pw_info->mtx_clk_parent)) {
		ret = PTR_ERR(pw_info->mtx_clk_parent);
		pr_err("Get mm_mtx_eb clk fail, ret %d\n", ret);
		return -ENODEV;
	}
	pw_info->mtx_clk_default = pw_info->mtx_clk_parent;

	atomic_set(&pw_info->inited, 1);
	pr_info("Read DTS OK\n");

	return ret;
}


static int mmsys_power_deinit(struct platform_device *pdev)
{
	pr_debug("Exit\n");
	/* kfree(pw_info); */
	devm_kfree(&pdev->dev, pw_info);

	return 0;
}

int sprd_cam_pw_on(void)
{
#if	defined(TEST_ON_HAPS)
	mutex_lock(&pw_info->mlock);
	if (atomic_inc_return(&pw_info->users_pw) == 1) {
		/* pmu */
		write_hwaddress_mask(0x327E0024, BIT(24) | BIT(25), 0);
		usleep_range(500, 1000);
	}
	mutex_unlock(&pw_info->mlock);

	return 0;
#else /* #elif */
	int ret = 0;
	unsigned int power_state1;
	unsigned int power_state2;
	unsigned int power_state3;
	unsigned int read_count = 0;
	unsigned int val = 0;

	pr_debug("E\n");
	if (check_drv_init())
		return -ENODEV;

	mutex_lock(&pw_info->mlock);

	if (atomic_inc_return(&pw_info->users_pw) == 1) {
		/* clear force shutdown */
		regmap_update_bits(pw_info->regs[_e_force_shutdown].gpr,
			pw_info->regs[_e_force_shutdown].reg,
			pw_info->regs[_e_force_shutdown].mask,
			0);
		/* power on */
		regmap_update_bits(pw_info->regs[_e_shutdown_en].gpr,
			pw_info->regs[_e_shutdown_en].reg,
			pw_info->regs[_e_shutdown_en].mask,
			0);

		do {
			cpu_relax();
			usleep_range(300, 350);
			read_count++;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_on;
			power_state1 = val & pw_info->regs[_e_power_state].mask;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_on;
			power_state2 = val & pw_info->regs[_e_power_state].mask;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_on;
			power_state3 = val & pw_info->regs[_e_power_state].mask;

		} while ((power_state1 && read_count < 10) ||
				(power_state1 != power_state2) ||
				(power_state2 != power_state3));

		if (power_state1) {
			pr_err("cam domain pw on failed 0x%x\n", power_state1);
			ret = -1;
			goto err_pw_on;
		}
	}
	mutex_unlock(&pw_info->mlock);
	/* if count == 0, other using */
	pr_info("Done, uses: %d, read count %d, cb: %p\n",
		atomic_read(&pw_info->users_pw), read_count,
		__builtin_return_address(0));

	return 0;

err_pw_on:
	atomic_dec_return(&pw_info->users_pw);
	mutex_unlock(&pw_info->mlock);
	pr_info("cam domain, failed to power on, ret = %d\n", ret);

	return ret;

#endif
}
EXPORT_SYMBOL(sprd_cam_pw_on);

int sprd_cam_pw_off(void)
{
#if	defined(TEST_ON_HAPS)
	return 0;
#else /* #elif */
	int ret = 0;
	unsigned int power_state1;
	unsigned int power_state2;
	unsigned int power_state3;
	unsigned int read_count = 0;
	unsigned int val = 0;

	pr_debug("E\n");
	if (check_drv_init())
		return -ENODEV;

	mutex_lock(&pw_info->mlock);
	if (atomic_dec_return(&pw_info->users_pw) == 0) {
		/* set 1 to shutdown */
		regmap_update_bits(pw_info->regs[_e_shutdown_en].gpr,
			pw_info->regs[_e_shutdown_en].reg,
			pw_info->regs[_e_shutdown_en].mask,
			~((uint32_t)0));
		/* force shutdown */
		regmap_update_bits(pw_info->regs[_e_force_shutdown].gpr,
			pw_info->regs[_e_force_shutdown].reg,
			pw_info->regs[_e_force_shutdown].mask,
			~((uint32_t)0));

		do {
			cpu_relax();
			usleep_range(300, 350);
			read_count++;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_off;
			power_state1 = val & pw_info->regs[_e_power_state].mask;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_off;
			power_state2 = val & pw_info->regs[_e_power_state].mask;

			ret = regmap_read(pw_info->regs[_e_power_state].gpr,
					pw_info->regs[_e_power_state].reg,
					&val);
			if (ret)
				goto err_pw_off;
			power_state3 = val & pw_info->regs[_e_power_state].mask;

		} while (((power_state1 != PD_MM_DOWN_FLAG) &&
			(read_count < 10)) ||
				(power_state1 != power_state2) ||
				(power_state2 != power_state3));

		if (power_state1 != PD_MM_DOWN_FLAG) {
			pr_err("power off failed 0x%x\n", power_state1);
			ret = -1;
			goto err_pw_off;
		}
	}
	mutex_unlock(&pw_info->mlock);
	/* if count == 0, other using */
	pr_info("Done, uses: %d, read count %d, cb: %p\n",
		atomic_read(&pw_info->users_pw), read_count,
		__builtin_return_address(0));

	return 0;

err_pw_off:
	mutex_unlock(&pw_info->mlock);
	pr_err("power off failed, ret: %d, count: %d!\n", ret, read_count);

	return ret;

#endif
}
EXPORT_SYMBOL(sprd_cam_pw_off);


int sprd_cam_domain_eb(void)
{
#if	defined(TEST_ON_HAPS)
	/* aon */
	write_hwaddress_mask(0x327d0000, BIT(9), BIT(9));

	write_hwaddress_mask(0x62200000, 0x3FC, 0x3FC); /* D2:D9 */
	write_hwaddress_mask(0x62200008, 0xF8, 0xF8); /* D3:D7 */

	return 0;
#else /* #elif */
	uint32_t tmp = 0;
	int ret = 0;

	if (check_drv_init())
		return -ENODEV;

	pr_debug("clk users count:%d, cb: %p\n",
		atomic_read(&pw_info->users_clk),
		__builtin_return_address(0));

	mutex_lock(&pw_info->mlock);

	if (atomic_inc_return(&pw_info->users_clk) == 1) {
		/* mm bus enable */
		clk_prepare_enable(pw_info->mm_eb);
		/* ahb clk */
		clk_set_parent(pw_info->ahb_clk, pw_info->ahb_clk_parent);
		clk_prepare_enable(pw_info->ahb_clk);
		/* mm mtx clk */
		clk_set_parent(pw_info->mtx_clk, pw_info->mtx_clk_parent);
		clk_prepare_enable(pw_info->mtx_clk);

		/* cam CKG enable, L5:0x62200000.b7
		 * as before, maybe should be removed
		 */
		regmap_update_bits(pw_info->regs[_e_ckg_eb].gpr,
			pw_info->regs[_e_ckg_eb].reg,
			pw_info->regs[_e_ckg_eb].mask, ~0);
		/* cphy ckg enable, L5:0x62200008.b8 */
		regmap_update_bits(pw_info->regs[_e_cphy_ckg_eb].gpr,
			pw_info->regs[_e_cphy_ckg_eb].reg,
			pw_info->regs[_e_cphy_ckg_eb].mask, ~0);

		/* Qos ar */
		tmp = pw_info->mm_qos_ar;
		regmap_update_bits(pw_info->regs[_e_qos_ar].gpr,
			pw_info->regs[_e_qos_ar].reg,
			pw_info->regs[_e_qos_ar].mask,
			tmp << lsb_bit1(pw_info->regs[_e_qos_ar].mask));
		/* Qos aw */
		tmp = pw_info->mm_qos_aw;
		regmap_update_bits(pw_info->regs[_e_qos_aw].gpr,
			pw_info->regs[_e_qos_aw].reg,
			pw_info->regs[_e_qos_aw].mask,
			tmp << lsb_bit1(pw_info->regs[_e_qos_aw].mask));
	}
	mutex_unlock(&pw_info->mlock);

	return 0;
#endif
}
EXPORT_SYMBOL(sprd_cam_domain_eb);

int sprd_cam_domain_disable(void)
{
#if	defined(TEST_ON_HAPS)
	uint32_t t = 0;

	t = read_hwaddress(0x62200000);
	pr_debug("mm ahb [0x62200000] = 0x%x\n", t);

	return 0;
#else /* #elif */
	if (check_drv_init())
		return -ENODEV;

	pr_info("clk users count: %d, cb: %p\n",
		atomic_read(&pw_info->users_clk),
		__builtin_return_address(0));

	mutex_lock(&pw_info->mlock);

	if (atomic_dec_return(&pw_info->users_clk) == 0) {
		/* cam CKG disable */
		regmap_update_bits(pw_info->regs[_e_ckg_eb].gpr,
			pw_info->regs[_e_ckg_eb].reg,
			pw_info->regs[_e_ckg_eb].mask, 0);
		/* cphy ckg disable */
		regmap_update_bits(pw_info->regs[_e_cphy_ckg_eb].gpr,
			pw_info->regs[_e_cphy_ckg_eb].reg,
			pw_info->regs[_e_cphy_ckg_eb].mask, 0);

		/* no need update qos */

		/* ahb clk */
		clk_set_parent(pw_info->ahb_clk, pw_info->ahb_clk_default);
		clk_disable_unprepare(pw_info->ahb_clk);
		/* mm mtx clk */
		clk_set_parent(pw_info->mtx_clk, pw_info->mtx_clk_default);
		clk_disable_unprepare(pw_info->mtx_clk);
		/* mm disable */
		clk_disable_unprepare(pw_info->mm_eb);
	}
	mutex_unlock(&pw_info->mlock);

	return 0;
#endif
}
EXPORT_SYMBOL(sprd_cam_domain_disable);

uint32_t sprd_chip_id0(void)
{
	uint32_t tmp = 0;
	int ret = 0;

	if (check_drv_init())
		return tmp;
	mutex_lock(&pw_info->mlock);
	ret = regmap_read(pw_info->regs[_e_chip_id0].gpr,
		pw_info->regs[_e_chip_id0].reg, &tmp);
	if (ret) {
		pr_err("read id0 fail\n");
		tmp = 0;
	}
	tmp &= pw_info->regs[_e_chip_id0].mask;
	mutex_unlock(&pw_info->mlock);

	return tmp;
}
EXPORT_SYMBOL(sprd_chip_id0);

uint32_t sprd_chip_id1(void)
{
	uint32_t tmp = 0;
	int ret = 0;

	if (check_drv_init())
		return tmp;
	mutex_lock(&pw_info->mlock);
	ret = regmap_read(pw_info->regs[_e_chip_id1].gpr,
		pw_info->regs[_e_chip_id1].reg, &tmp);
	if (ret) {
		pr_err("read id0 fail\n");
		tmp = 0;
	}
	tmp &= pw_info->regs[_e_chip_id1].mask;
	mutex_unlock(&pw_info->mlock);

	return tmp;
}
EXPORT_SYMBOL(sprd_chip_id1);

static int mmpw_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("E\n");
	/* read dts */
	ret = mmsys_power_init(pdev);
	if (ret) {
		pr_err("power init fail\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, pw_info);

	pr_info(",OK\n");

	return ret;
}

static int mmpw_remove(struct platform_device *pdev)
{

	pr_debug("E\n");

	mmsys_power_deinit(pdev);

	return 0;
}

static const struct of_device_id mmpw_match_table[] = {
	{.compatible = "sprd,mm-domain", },
	{},
};

static struct platform_driver mmpw_driver = {
	.probe = mmpw_probe,
	.remove = mmpw_remove,
	.driver = {
			.name = "mmsys-power",
			.of_match_table = of_match_ptr(mmpw_match_table),
	},
};

/* module_platform_driver(mmpw_driver); */
static int __init mmpw_init(void)
{
	int ret;

	ret = platform_driver_register(&mmpw_driver);

	return ret;
}

static void __exit mmpw_exit(void)
{
	platform_driver_unregister(&mmpw_driver);
}

subsys_initcall(mmpw_init)
module_exit(mmpw_exit)


MODULE_DESCRIPTION("MMsys Power Driver");
MODULE_AUTHOR("Multimedia_Camera@unisoc.com");
MODULE_LICENSE("GPL");

