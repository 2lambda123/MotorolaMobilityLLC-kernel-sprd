/**
 * SPDX-FileContributor: vdsp dvfs function, dynamically adjust frequency voltage.
 */

#ifndef _VDSP_ADPT_H_
#define _VDSP_ADPT_H_

struct dvfs_adpt_ops {
	void (*enable_adpt)(void *hw_arg);
	void (*disable_adpt)(void *hw_arg);
	uint32_t (*level_to_freq_adpt)(uint32_t level);
	void (*setdvfs_adpt)(uint32_t level);
	void (*strategy_adpt)(uint32_t *level, uint32_t max_level, uint32_t percent, uint32_t last_percent);
};

struct dvfs_adpt_ops *get_dvfs_adpt_ops(void);

#endif

