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

#define pr_fmt(fmt) "unisoc_binder: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <trace/hooks/binder.h>
#include <uapi/linux/sched/types.h>
#include <../drivers/android/binder_internal.h>

#include "unibinder_netlink.h"
#include "unibinder.h"

/**
 * This list is used to hold the unisoc feature affected
 * alived task info, its item is @unibinder_thread
 */
static HLIST_HEAD(unibinder_threads);
static DEFINE_SPINLOCK(unibinder_threads_lock);
/**
 * The count of unibinder_thread that enabled the
 * skip priority restore feature
 */
static int skip_restore_count;
/**
 * The count of unibinder_thread that enabled the inhert rt
 * schedule policy and priority from caller thread feature
 */
static int inherit_rt_count;

enum {
	UNIBINDER_DEBUG_THREADS		= 1U << 0,
	UNIBINDER_DEBUG_PRIORITY	= 1U << 1,
};
static uint32_t unibinder_debug_mask = UNIBINDER_DEBUG_THREADS;
module_param_named(debug_mask, unibinder_debug_mask, uint, 0644);

#define unibinder_debug(mask, x...) \
	do { \
		if (unibinder_debug_mask & mask) \
			pr_info_ratelimited(x); \
	} while (0)

static bool is_rt_policy(int policy)
{
	return policy == SCHED_FIFO || policy == SCHED_RR;
}

/* Check if the feature flags is valid */
static bool is_flags_valid(int flags)
{
	return (flags & UNIBINDER_SCHED_FLAG);
}

/**
 * Get the unibinder_thread record of the input pid,
 * Please do the pid validation before call this function.
 */
static struct unibinder_thread *get_unibinder_thread(int pid)
{
	struct unibinder_thread *itr;
	struct unibinder_thread *found_thread = NULL;

	spin_lock(&unibinder_threads_lock);
	hlist_for_each_entry(itr, &unibinder_threads, thread_node) {
		if (itr->pid == pid) {
			found_thread = itr;
			break;
		}
	}
	spin_unlock(&unibinder_threads_lock);

	return found_thread;
}

/**
 * Check if the unibinder_thread enabled the feature that
 * the feature_flag refer to.
 * @pid:            the pid of unibinder_thread
 * @feature_flag:   the feature flags, can refer to the is_flags_valid
 *                  function checked flags definition.
 */
static bool is_enabled_feature(int pid, int feature_flag)
{
	struct unibinder_thread *thread;

	if (pid <= 0)
		return false;

	thread = get_unibinder_thread(pid);
	if (!thread)
		return false;

	spin_lock(&unibinder_threads_lock);
	if ((thread->pid == pid) && (thread->sched_flags & feature_flag)) {
		spin_unlock(&unibinder_threads_lock);
		return true;
	}
	spin_unlock(&unibinder_threads_lock);

	return false;
}

/**
 * Update the unibinder_thread enabled feature flags
 * @thread:  the unibinder_thread record which feature flags will be updated
 * @flags:   the feature flags which will be updated to the unibinder_thread
 * @set:     whether set the feature flags, true for enable the feature for
 *           the thread, false for disabel the feature for the thread.
 */
static void update_thread_flags_locked(struct unibinder_thread *thread, int flags, bool set)
{
	if (!thread)
		return;

	if (flags & SCHED_FLAG_SKIP_RESTORE) {
		if (set) {
			thread->sched_flags |= SCHED_FLAG_SKIP_RESTORE;
			skip_restore_count++;
		} else if (thread->sched_flags & SCHED_FLAG_SKIP_RESTORE) {
			thread->sched_flags &= ~SCHED_FLAG_SKIP_RESTORE;
			skip_restore_count--;
		}
	}

	if (flags & SCHED_FLAG_INHERIT_RT) {
		if (set) {
			thread->sched_flags |= SCHED_FLAG_INHERIT_RT;
			inherit_rt_count++;
		} else if (thread->sched_flags & SCHED_FLAG_INHERIT_RT) {
			thread->sched_flags &= ~SCHED_FLAG_INHERIT_RT;
			inherit_rt_count--;
		}
	}
}

/**
 * Add unibinder_thread record with the initial feature flags
 * @pid:    the unibinder_thread related task pid
 * @flags:  the feature flags attached to the unibinder_thread
 */
static int add_unibinder_thread(int pid, int flags)
{
	struct unibinder_thread *thread;

	thread = kzalloc(sizeof(*thread), GFP_KERNEL);
	if (!thread)
		return -ENOMEM;

	thread->pid = pid;

	spin_lock(&unibinder_threads_lock);
	update_thread_flags_locked(thread, flags, true);
	hlist_add_head(&thread->thread_node, &unibinder_threads);
	spin_unlock(&unibinder_threads_lock);

	unibinder_debug(UNIBINDER_DEBUG_THREADS,
			"%s pid %d added flags %d", __func__, pid, flags);
	return 0;
}

static void remove_unibinder_thread(int pid)
{
	struct unibinder_thread *found_thread;

	if (pid <= 0)
		return;

	found_thread = get_unibinder_thread(pid);
	if (found_thread) {
		spin_lock(&unibinder_threads_lock);
		update_thread_flags_locked(found_thread,
			SCHED_FLAG_SKIP_RESTORE | SCHED_FLAG_INHERIT_RT, false);
		hlist_del(&found_thread->thread_node);
		kfree(found_thread);
		spin_unlock(&unibinder_threads_lock);

		unibinder_debug(UNIBINDER_DEBUG_THREADS,
				"%s pid %d removed", __func__, pid);
	}
}

void set_thread_flags(int pid, int flags)
{
	struct unibinder_thread *thread;

	unibinder_debug(UNIBINDER_DEBUG_THREADS,
			"%s pid %d set flags %d", __func__, pid, flags);

	if (pid <= 0)
		return;

	if (!is_flags_valid(flags))
		return;

	thread = get_unibinder_thread(pid);

	if (!thread) {
		add_unibinder_thread(pid, flags);
	} else {
		spin_lock(&unibinder_threads_lock);
		update_thread_flags_locked(thread, flags, true);
		spin_unlock(&unibinder_threads_lock);

		unibinder_debug(UNIBINDER_DEBUG_THREADS,
				"%s pid %d flags has been updated to %d",
				__func__, pid, thread->sched_flags);
	}
}

void remove_thread_flags(int pid, int flags)
{
	struct unibinder_thread *thread;

	unibinder_debug(UNIBINDER_DEBUG_THREADS,
			"%s pid %d remove flags %d", __func__, pid, flags);

	if (pid <= 0)
		return;

	if (!is_flags_valid(flags))
		return;

	thread = get_unibinder_thread(pid);

	if (thread) {
		spin_lock(&unibinder_threads_lock);
		update_thread_flags_locked(thread, flags, false);
		spin_unlock(&unibinder_threads_lock);
		unibinder_debug(UNIBINDER_DEBUG_THREADS,
				"%s pid %d flags has been updated to %d",
				__func__, pid, thread->sched_flags);
	}
}

#ifdef CONFIG_UNISOC_BINDER_SCHED
/* Check whether the binder_thread has some pending binder work */
static bool unibinder_has_work_ilocked(struct binder_thread *thread,
					bool do_proc_work)
{
	bool has_work;

	spin_lock(&thread->proc->inner_lock);
	has_work = thread->process_todo ||
		thread->looper_need_return ||
		(do_proc_work && !list_empty(&thread->proc->todo));
	spin_unlock(&thread->proc->inner_lock);
	return has_work;
}

/**
 * Hook the trace_android_vh_binder_restore_priority for skip restore feature
 * If the binder thread enabled the skip restore feature, we will
 * skip the priority restore flow for it
 */
static void unibinder_restore_priority(void *data,
				struct binder_transaction *in_reply_to,
				struct task_struct *task)
{
	struct binder_thread *to_thread;

	if ((skip_restore_count <= 0) || !in_reply_to)
		return;

	to_thread = in_reply_to->to_thread;

	if (!to_thread || !to_thread->task)
		return;

	if (!is_enabled_feature(to_thread->task->pid, SCHED_FLAG_SKIP_RESTORE))
		return;

	if (unibinder_has_work_ilocked(to_thread, true)) {
		spin_lock(&to_thread->prio_lock);
		to_thread->prio_state = BINDER_PRIO_ABORT;
		spin_unlock(&to_thread->prio_lock);
		unibinder_debug(UNIBINDER_DEBUG_PRIORITY,
				"%s to_thread: %d Abort binder thread prio restore",
				__func__, to_thread->task->pid);
	}
}

/**
 * Hook the trace_android_vh_binder_wait_for_work for skip resore feature
 * When the binder thread is idle for waiting more work to do, we
 * will restore its priority to its saved priority.
 */
static void unibinder_wait_for_work(void *data, bool do_proc_work,
				struct binder_thread *thread, struct binder_proc *proc)
{
	bool restore_normal = false;

	if ((skip_restore_count <= 0) || !thread || !thread->task)
		return;

	if (is_enabled_feature(thread->task->pid, SCHED_FLAG_SKIP_RESTORE))
		return;

	// prio state restore been aborted, reset the thread
	// prio to normal
	spin_lock(&thread->prio_lock);
	if (thread->prio_state == BINDER_PRIO_ABORT) {
		struct sched_param params;
		int policy = thread->prio_next.sched_policy;

		params.sched_priority = thread->prio_next.prio;
		sched_setscheduler_nocheck(thread->task, policy | SCHED_RESET_ON_FORK,
						&params);
		thread->prio_state = BINDER_PRIO_SET;
		restore_normal = true;
	}
	spin_unlock(&thread->prio_lock);
	if (restore_normal)
		unibinder_debug(UNIBINDER_DEBUG_PRIORITY,
				"%s pid:%d priority restore to normal",
				__func__, thread->task->pid);
}

/**
 * Hook trace_android_vh_binder_set_priority for inherit rt feature
 * If the caller thread schedule policy is rt, and it enabled the inherit rt
 * feature, then we will enabled inherit rt for the binder thread that will
 * handle the binder work that from the caller thread.
 */
static void unibinder_set_priority(void *data, struct binder_transaction *t,
				struct task_struct *task)
{
	struct sched_param params;
	int policy;

	if (inherit_rt_count <= 0)
		return;

	if (t && t->from && task && t->from->task) {
		bool oneway = !!(t->flags & TF_ONE_WAY);

		if (oneway)
			return;

		if (!is_enabled_feature(t->from->task->pid, SCHED_FLAG_INHERIT_RT))
			return;

		spin_lock(&t->from->prio_lock);
		if (is_rt_policy(t->from->task->policy)) {
			policy = t->from->task->policy;
			params.sched_priority = t->from->task->normal_prio;
		} else  {
			spin_unlock(&t->from->prio_lock);
			return;
		}

		sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK,
						&params);
		spin_unlock(&t->from->prio_lock);
		unibinder_debug(UNIBINDER_DEBUG_PRIORITY,
				"%s set pid %d policy %d prio %d", __func__,
				task->pid, policy, params.sched_priority);
	}
}

/**
 * Hook the trace_android_vh_binder_thread_release for unisoc binder features
 * When the unbinder_thread related binder_thread is released, we will release the
 * unibinder_thread record too.
 */
static void unibinder_thread_release(void *data, struct binder_proc *proc,
				struct binder_thread *thread)
{
	int pid;

	if ((skip_restore_count <= 0) && (inherit_rt_count <= 0))
		return;

	if (thread) {
		pid = thread->pid;
		if (pid > 0)
			remove_unibinder_thread(pid);
	}
}
#endif // CONFIG_UNISOC_BINDER_SCHED

static void free_all_unibinder_threads(void)
{
	struct hlist_node *tmp;
	struct unibinder_thread *itr;

	spin_lock(&unibinder_threads_lock);
	hlist_for_each_entry_safe(itr, tmp, &unibinder_threads, thread_node) {
		hlist_del(&itr->thread_node);
		kfree(itr);
	}
	skip_restore_count = 0;
	inherit_rt_count = 0;
	spin_unlock(&unibinder_threads_lock);
}

static int __init unibinder_init(void)
{
#ifdef CONFIG_UNISOC_BINDER_SCHED
	register_trace_android_vh_binder_restore_priority(unibinder_restore_priority, NULL);
	register_trace_android_vh_binder_wait_for_work(unibinder_wait_for_work, NULL);
	register_trace_android_vh_binder_set_priority(unibinder_set_priority, NULL);
	register_trace_android_vh_binder_thread_release(unibinder_thread_release, NULL);
#endif

	binder_netlink_init();

	pr_info("unisoc binder module init");
	return 0;
}

static void __exit unibinder_exit(void)
{
#ifdef CONFIG_UNISOC_BINDER_SCHED
	unregister_trace_android_vh_binder_restore_priority(unibinder_restore_priority, NULL);
	unregister_trace_android_vh_binder_wait_for_work(unibinder_wait_for_work, NULL);
	unregister_trace_android_vh_binder_set_priority(unibinder_set_priority, NULL);
	unregister_trace_android_vh_binder_thread_release(unibinder_thread_release, NULL);
#endif

	binder_netlink_exit();
	free_all_unibinder_threads();

	pr_info("unisoc binder module exit");
}

module_init(unibinder_init);
module_exit(unibinder_exit);

MODULE_AUTHOR("Xiaomei Li <xiaomei.li@unisoc.com>");
MODULE_LICENSE("GPL v2");
