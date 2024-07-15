/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage.
 */

#ifndef _VDSP_DRIVER_DVFS_PHY_H_
#define _VDSP_DRIVER_DVFS_PHY_H_

#define MM_SYS_EN		(0x0)

/*MM_SYS_EN		(0x0)*/
#define DVFS_EN		BIT(3) //GENMASK(23, 16)

struct dvfs_phy_ops {
	void (*enable)(void *hw_arg);
	void (*disable)(void *hw_arg);
	uint32_t (*level_to_freq)(uint32_t level);
	void (*setdvfs)(uint32_t level);
	void (*strategy)(uint32_t *level, uint32_t max_level,
		uint32_t percent, uint32_t last_percent);
};

struct dvfs_phy_desc {
	struct dvfs_phy_ops *ops;
};

struct dvfs_phy_desc *get_dvfs_phy_desc(void);



#endif


