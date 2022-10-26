/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for DAMON Sysfs Interface
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

#define __ATTR_RW_MODE(_name, _mode) {					\
	.attr	= { .name = __stringify(_name),				\
		    .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _name##_show,						\
	.store	= _name##_store,					\
}

extern struct mutex damon_sysfs_lock;
