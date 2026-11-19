/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/linux/timerfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_TIMERFD_H
#define _LINUX_TIMERFD_H

#include <linux/types.h>
#include <uapi/linux/timerfd.h>

#define TFD_SHARED_FCNTL_FLAGS (TFD_CLOEXEC | TFD_NONBLOCK)
/* Flags for timerfd_create.  */
#define TFD_CREATE_FLAGS TFD_SHARED_FCNTL_FLAGS
/* Flags for timerfd_settime.  */
#define TFD_SETTIME_FLAGS (TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET)

#ifdef CONFIG_TIMERFD
struct file;
struct file_operations;
struct poll_table_struct;

extern const struct file_operations timerfd_fops;
__poll_t timerfd_poll(struct file *file, struct poll_table_struct *wait);
#endif

#endif /* _LINUX_TIMERFD_H */
