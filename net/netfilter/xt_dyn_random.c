/*
 * Copyright (c) 2013, Ubiquiti Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_dyn_random.h>

MODULE_LICENSE("GPL");

struct dyn_rand {
	struct list_head 	list;
	struct proc_dir_entry 	*proc;
	unsigned int 		refcnt;
	uint32_t 		prob;
};

static struct proc_dir_entry *proc_root;
static LIST_HEAD(proc_list);
static DEFINE_MUTEX(proc_lock);

static int read_proc(char *page, char **start, off_t off, int count,
		     int *eof, void *data)
{
	const struct dyn_rand *e = data;
	int len = sprintf(page, "%u\n", e->prob);

	if (len <= (off + count))
		*eof = 1;

	if (off >= len)
		return 0;

	if (count > (len - off))
		return (len - off);

	return count;
}

static int write_proc(struct file *file, const char __user *buf,
		      unsigned long count, void *data)
{
	struct dyn_rand *e = data;
	char tmp[16];
	uint32_t p;

	if (count >= 16)
		return -EACCES;

	memset(tmp, 0, count + 1);
	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	p = simple_strtoul(tmp, NULL, 10);
	if (p >= 0x100)
		return -EINVAL;

	e->prob = p;
	return count;
}

static bool dyn_rand_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_dyn_rand_info *info = par->matchinfo;
	const struct dyn_rand *e = info->dyn_rand;

	return ((e->prob > 0 && (net_random() & 0xff) <= e->prob)
		^ info->invert);
}

static int dyn_rand_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_dyn_rand_info *info = par->matchinfo;
	struct dyn_rand *e;

	if (*(info->name) == 0 || *(info->name) == '.'
		|| memchr(info->name, 0, sizeof(info->name)) == NULL
		|| memchr(info->name, '/', sizeof(info->name)) != NULL)
		return -EINVAL;

	mutex_lock(&proc_lock);
	list_for_each_entry(e, &proc_list, list) {
		if (strcmp(info->name, e->proc->name) == 0) {
			e->refcnt++;
			mutex_unlock(&proc_lock);
			info->dyn_rand = e;
			return 0;
		}
	}

	if ((e = kzalloc(sizeof(struct dyn_rand), GFP_KERNEL)) == NULL) {
		mutex_unlock(&proc_lock);
		return -EINVAL;
	}

	e->proc = create_proc_entry(info->name, S_IRUSR | S_IWUSR, proc_root);
	if (e->proc == NULL) {
		kfree(e);
		mutex_unlock(&proc_lock);
		return -EINVAL;
	}

	e->refcnt = 1;
	e->prob = 0;
	e->proc->data = e;
	e->proc->read_proc = read_proc;
	e->proc->write_proc = write_proc;
	e->proc->uid = S_IRUSR | S_IWUSR;
	e->proc->gid = S_IRUSR | S_IWUSR;
	list_add(&(e->list), &proc_list);
	mutex_unlock(&proc_lock);
	info->dyn_rand = e;
	return 0;
}

static void dyn_rand_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_dyn_rand_info *info = par->matchinfo;
	struct dyn_rand *e = info->dyn_rand;

	mutex_lock(&proc_lock);
	if (--e->refcnt == 0) {
		list_del(&(e->list));
		remove_proc_entry(e->proc->name, proc_root);
		mutex_unlock(&proc_lock);
		kfree(e);
		return;
	}
	mutex_unlock(&proc_lock);
}

static struct xt_match xt_dyn_rand_reg __read_mostly = {
	.name       = "dyn_random",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = dyn_rand_mt,
	.checkentry = dyn_rand_mt_check,
	.destroy    = dyn_rand_mt_destroy,
	.matchsize  = sizeof(struct xt_dyn_rand_info),
	.me         = THIS_MODULE,
};

static const char * const proc_root_name = "xt_dyn_random";

static int __init dyn_rand_init(void)
{
	int ret;

	mutex_init(&proc_lock);
	proc_root = proc_mkdir(proc_root_name, init_net.proc_net);
	if (proc_root == NULL)
		return -EACCES;

	if ((ret = xt_register_match(&xt_dyn_rand_reg)) < 0) {
		remove_proc_entry(proc_root_name, init_net.proc_net);
		return ret;
	}

	return 0;
}

static void __exit dyn_rand_exit(void)
{
	xt_unregister_match(&xt_dyn_rand_reg);
}

module_init(dyn_rand_init);
module_exit(dyn_rand_exit);
