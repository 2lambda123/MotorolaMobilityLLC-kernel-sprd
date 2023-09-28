/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2023 Unisoc(Shanghai) Technologies Co.Ltd
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program;
 * if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef _UNIBINDER_H
#define _UNIBINDER_H

#define pr_fmt(fmt) "unisoc_binder: " fmt

#include <linux/list.h>

/**
 * Set the thread flags, the flags indicate that the thread will enable some features.
 * @pid:    the pid of the thread which will be attach the feature flags
 * @flags:   use this param to set the feature flags, the flags please
 *          refer to @thread_sched_flags enum.
 */
void set_thread_flags(int pid, int flags);

/**
 * remove the feature flags for thread, remove flags will disable some features for it.
 * @pid:    the pid of the thread which will remove the feature flags
 * @flags:   use this param to set the feature flags, the flags please
 *          refer to @thread_sched_flags enum.
 */
void remove_thread_flags(int pid, int flags);

/**
 * The thread record can be binder thread or any other alived task.
 * This record will be added when the binder feature flag be attached to it, will be removed
 * when the alived task died.
 * @pid:        alived task pid
 * @shed_flags: schedule policy feature flags, refer to @thread_sched_flags enum.
 *
 * TODO: make the related task attach to unibinder_thread record
 */
struct unibinder_thread {
	int pid;
	int sched_flags;
	struct hlist_node thread_node;
};

/**
 * This is the sched policy feature flags, it be used to adjust the
 * schedule policy and priority for binder thread.
 *
 * @SCHED_FLAG_SKIP_RESTORE:    set this flag will skip priority restore flow for binder thread.
 *                              this flag is for binder thread.
 * @SCHED_FLAG_INHERIT_RT:      set this flag will enable the inherit rt for binder thread.
 *                              this flag is for the caller thread, but affect the binder thread
 *                              which will do the binder work for the caller thread.
 */
enum thread_sched_flags {
	SCHED_FLAG_NONE         = 0x01,
	SCHED_FLAG_SKIP_RESTORE = 0x04,
	SCHED_FLAG_INHERIT_RT   = 0x08,

	__SCHED_FLAG__MAX,
};
#define SCHED_FLAG__MAX (__SCHED_FLAG__MAX - 1)

#define UNIBINDER_SCHED_FLAG (SCHED_FLAG_NONE | SCHED_FLAG_SKIP_RESTORE | SCHED_FLAG_INHERIT_RT)

#endif/* _UNIBINDER_H */
