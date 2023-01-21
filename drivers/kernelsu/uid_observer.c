#include "linux/kprobes.h"
#include <linux/list.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "uid_observer.h"
#include "allowlist.h"
#include "arch.h"
#include "klog.h"
#include "ksu.h"

#define SYSTEM_PACKAGES_LIST_PATH "/data/system/packages.list"
static struct work_struct ksu_update_uid_work;

struct uid_data {
	struct list_head list;
	u32 uid;
};

static bool is_uid_exist(uid_t uid, void *data)
{
	struct list_head *list = (struct list_head *)data;
	struct uid_data *np;

	bool exist = false;
	list_for_each_entry (np, list, list) {
		if (np->uid == uid) {
			exist = true;
			break;
		}
	}
	return exist;
}

static void do_update_uid(struct work_struct *work)
{
	struct file *fp = filp_open(SYSTEM_PACKAGES_LIST_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("do_update_uid, open " SYSTEM_PACKAGES_LIST_PATH
		       " failed: %d\n",
		       ERR_PTR(fp));
		return;
	}

	struct list_head uid_list;
	INIT_LIST_HEAD(&uid_list);

	char chr = 0;
	loff_t pos = 0;
	loff_t line_start = 0;
	char buf[128];
	for (;;) {
		ssize_t count = kernel_read(fp, &chr, sizeof(chr), &pos);
		if (count != sizeof(chr))
			break;
		if (chr != '\n')
			continue;

		count = kernel_read(fp, buf, sizeof(buf), &line_start);

		struct uid_data *data =
			kmalloc(sizeof(struct uid_data), GFP_ATOMIC);
		if (!data) {
			goto out;
		}

		char *tmp = buf;
		const char *delim = " ";
		strsep(&tmp, delim); // skip package
		char *uid = strsep(&tmp, delim);
		if (!uid) {
			pr_err("update_uid: uid is NULL!\n");
			continue;
		}

		u32 res;
		if (kstrtou32(uid, 10, &res)) {
			pr_err("update_uid: uid parse err\n");
			continue;
		}
		data->uid = res;
		list_add_tail(&data->list, &uid_list);
		// reset line start
		line_start = pos;
	}

	// now update uid list
	struct uid_data *np;
	struct uid_data *n;

	// first, check if manager_uid exist!
	bool manager_exist = false;
	list_for_each_entry (np, &uid_list, list) {
		if (np->uid == ksu_get_manager_uid()) {
			manager_exist = true;
			break;
		}
	}

	if (!manager_exist && ksu_is_manager_uid_valid()) {
		pr_info("manager is uninstalled, invalidate it!\n");
		ksu_invalidate_manager_uid();
	}

	// then prune the allowlist
	ksu_prune_allowlist(is_uid_exist, &uid_list);
out:
	// free uid_list
	list_for_each_entry_safe (np, n, &uid_list, list) {
		list_del(&np->list);
		kfree(np);
	}
	filp_close(fp, 0);
}

static void update_uid()
{
	ksu_queue_work(&ksu_update_uid_work);
}

int ksu_handle_rename(struct dentry *old_dentry, struct dentry *new_dentry)
{
	if (!current->mm) {
		// skip kernel threads
		return 0;
	}

	if (current_uid().val != 1000) {
		// skip non system uid
		return 0;
	}

	if (!old_dentry || !new_dentry) {
		return 0;
	}

	// /data/system/packages.list.tmp -> /data/system/packages.list
	if (strcmp(new_dentry->d_iname, "packages.list")) {
		return 0;
	}

	char path[128];
	char *buf = dentry_path_raw(new_dentry, path, sizeof(path));
	if (IS_ERR(buf)) {
		pr_err("dentry_path_raw failed.\n");
		return 0;
	}

	if (strcmp(buf, "/system/packages.list")) {
		return 0;
	}
	pr_info("renameat: %s -> %s\n, new path: %s", old_dentry->d_iname,
		new_dentry->d_iname, buf);

	update_uid();

	return 0;
}

int ksu_uid_observer_init()
{
	INIT_WORK(&ksu_update_uid_work, do_update_uid);
	return 0;
}

int ksu_uid_observer_exit()
{
	return 0;
}