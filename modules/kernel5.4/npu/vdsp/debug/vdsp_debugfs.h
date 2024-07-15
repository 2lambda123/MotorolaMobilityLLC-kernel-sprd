/**
 * SPDX-FileContributor: vdsp driver debug function.
 */

#ifndef __VDSP_DEBUGFS_H__
#define __VDSP_DEBUGFS_H__

int vdsp_debugfs_init(void);
void vdsp_debugfs_exit(void);
unsigned int vdsp_debugfs_log_mode(void);
unsigned int vdsp_debugfs_log_level(void);
unsigned int vdsp_debugfs_dvfs_level(void);
unsigned int vdsp_debugfs_trace_mem(void);

#endif

