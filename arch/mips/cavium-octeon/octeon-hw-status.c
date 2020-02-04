/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Cavium, Inc.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/slab.h>

#include <asm/octeon/octeon-hw-status.h>
#include <asm/octeon/octeon.h>

static RAW_NOTIFIER_HEAD(octeon_hw_status_notifiers);

int octeon_hw_status_notifier_register(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&octeon_hw_status_notifiers, nb);
}
EXPORT_SYMBOL(octeon_hw_status_notifier_register);

int octeon_hw_status_notifier_unregister(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&octeon_hw_status_notifiers, nb);
}
EXPORT_SYMBOL(octeon_hw_status_notifier_unregister);

struct octeon_hw_status_node {
	struct octeon_hw_status_node *next; /* Child list */
	struct octeon_hw_status_node *child;
	struct octeon_hw_status_node *parent;
	union {
		struct {
			u64 reg;
			u64 mask_reg;
		};
		struct {
			u32 hwint;
			int irq;
		};
	};
	u8 bit;
	u8 users;  /* Reference count. */
	u8 is_hwint:1;
	u8 ack_w1c:1;
};

/* Protects octeon_hw_status_roots */
static DEFINE_RWLOCK(octeon_hw_status_lock);
static struct octeon_hw_status_node *octeon_hw_status_roots;

/* call cb on each node.  stop if cb returns true. */
static int visit_leaves(struct octeon_hw_status_node *s, bool visit_sibs,
			  int (*cb)(struct octeon_hw_status_node *, void *),
			  void *arg)
{
	int depth = 0;
	struct octeon_hw_status_node *w = s;
	int r;

	while (w && depth >= 0) {
		if (w->child) {
			/* Go out to the leaves */
			w = w->child;
			depth++;
			continue;
		}

		/* At a leaf, make the callback. */
		r = cb(w, arg);
		if (r)
			return r;

		if (w->next) {
			/* Go to next leaf */
			w = w->next;
			continue;
		}
		/* back toward the root*/
		for (;;) {
			depth--;
			if (!w->parent)
				return 0;
			if (!visit_sibs && w->parent == s)
				return 0;
			if (w->parent->next) {
				w = w->parent->next;
				break;
			}
			w = w->parent;
		}
	}
	return 0;
}

struct find_node_cb_data {
	struct octeon_hw_status_node *r;
	struct octeon_hw_status_reg *sr;
};

static int find_node_cb(struct octeon_hw_status_node *n, void *arg)
{
	struct find_node_cb_data *d = arg;
	struct octeon_hw_status_reg *sr = d->sr;

	if (n->is_hwint == sr->reg_is_hwint) {
		if (n->is_hwint) {
			if (n->hwint == sr->reg)
				goto found;
		} else {
			if (n->reg == sr->reg && n->bit == sr->bit)
				goto found;
		}
	}
	return 0;
found:
	WARN((n->child != NULL) != sr->has_child ||
	     n->ack_w1c != sr->ack_w1c, "Mismatched properties %p, %d, %d, %d",n->child, sr->has_child, n->ack_w1c, sr->ack_w1c );
	d->r = n;
	return 1;
}

static struct octeon_hw_status_node *find_node(struct octeon_hw_status_node *r,
					       struct octeon_hw_status_reg *sr)
{
	struct find_node_cb_data d;
	d.r = NULL;
	d.sr = sr;

	visit_leaves(r, true, find_node_cb, &d);

	return d.r;
}

struct irq_cb_data {
	bool handled_one;
};

static int irq_cb(struct octeon_hw_status_node *n, void *arg)
{
	u64 csr, en, bit_mask;
	struct octeon_hw_status_data ohsd;
	struct irq_cb_data *d = arg;

	memset(&ohsd, 0, sizeof(ohsd));

	if (n->is_hwint) {
		ohsd.reg = n->hwint;
		ohsd.reg_is_hwint = 1;
	} else {
		bit_mask = 1ull << n->bit;
		csr = cvmx_read_csr(n->reg);
		if (!(csr & bit_mask))
			return 0;

		if (n->mask_reg) {
			en = cvmx_read_csr(n->mask_reg);
			if (!(en & bit_mask))
				return 0;
		}
		/* Found an enabled source. */
		if (n->ack_w1c)
			cvmx_write_csr(n->reg, bit_mask);

		ohsd.reg = n->reg;
		ohsd.bit = n->bit;
	}
	raw_notifier_call_chain(&octeon_hw_status_notifiers,
				OCTEON_HW_STATUS_SOURCE_ASSERTED, &ohsd);
	d->handled_one = true;
	return 0;
}

static irqreturn_t octeon_hw_status_irq(int irq, void *dev)
{
	struct irq_cb_data d;
	struct octeon_hw_status_node *root = dev;

	d.handled_one = false;
	read_lock(&octeon_hw_status_lock);
	visit_leaves(root, false, irq_cb, &d);
	read_unlock(&octeon_hw_status_lock);
	return d.handled_one ? IRQ_HANDLED : IRQ_NONE;
}

static struct octeon_hw_status_node *find_child(struct octeon_hw_status_node *r,
					       struct octeon_hw_status_reg *sr)
{
	struct octeon_hw_status_node *cw = r->child;
	struct find_node_cb_data cbd;
	cbd.r = NULL;
	cbd.sr = sr;
	while(cw) {
		if (find_node_cb(cw, &cbd))
			break;
		cw = cw->next;
	}
	return cw;
}

int octeon_hw_status_add_source(struct octeon_hw_status_reg *chain)
{
	struct octeon_hw_status_data ohsd;
	struct octeon_hw_status_node *root;
	struct octeon_hw_status_node *n;
	struct octeon_hw_status_node *w;
	bool root_created = false;
	int rv = 0;

	if (!chain->reg_is_hwint)
		return -EINVAL;

	write_lock(&octeon_hw_status_lock);
	/* Find the root */
	root = octeon_hw_status_roots;
	while (root && root->hwint != chain->reg)
		root = root->next;

	if (!root) {
		root = kzalloc(sizeof(struct octeon_hw_status_node),
			       GFP_KERNEL);
		if (!root) {
			rv = -ENOMEM;
			goto out;
		}
		root->hwint = chain->reg;
		root->is_hwint = 1;
		root->ack_w1c = chain->ack_w1c;
		root->next = octeon_hw_status_roots;
		octeon_hw_status_roots = root;
		root_created = true;
	}
	w = root;
	while (chain->has_child) {
		chain++;
		n = find_child(w, chain);
		if (!n) {
			n = kzalloc(sizeof(struct octeon_hw_status_node),
				    GFP_KERNEL);
			if (!n) {
				rv = -ENOMEM;
				goto out;
			}
			n->is_hwint = chain->reg_is_hwint;
			n->ack_w1c = chain->ack_w1c;
			if (n->is_hwint) {
				n->hwint = chain->reg;
			} else {
				n->reg = chain->reg;
				n->mask_reg = chain->mask_reg;
				n->bit = chain->bit;
			}
			n->next = w->child;
			w->child = n;
			n->parent = w;
		}
		w = n;
	}
	w->users++;
	WARN(w->users == 0, "Reference count overflowed!");

	write_unlock(&octeon_hw_status_lock);

	if (root_created) {
		/* register an interrupt handler */
		root->irq = irq_create_mapping(NULL, root->hwint);
		if (!root->irq)
			return -ENXIO;

		rv = request_threaded_irq(root->irq, NULL, octeon_hw_status_irq,
					  IRQF_ONESHOT, "octeon-hw-status", root);
		WARN(rv, "request_threaded_irq failed: %d", rv);
	}

	ohsd.reg = w->reg;
	ohsd.bit = w->bit;
	raw_notifier_call_chain(&octeon_hw_status_notifiers,
				OCTEON_HW_STATUS_SOURCE_ADDED, &ohsd);
	return 0;
out:
	write_unlock(&octeon_hw_status_lock);
	return rv;
}
EXPORT_SYMBOL(octeon_hw_status_add_source);

/* Return true if we unlocked the lock because we did free_irq. */
static bool dispose_of_node(struct octeon_hw_status_node *n)
{
	bool rv = false;
	struct octeon_hw_status_node *parent;
	struct octeon_hw_status_node **pw;

	for (;;) {
		parent = n->parent;
		if (parent)
			pw = &n->parent->child;
		else
			pw = &octeon_hw_status_roots;

		while (*pw) {
			if (*pw == n) {
				if (!n->is_hwint && n->mask_reg) {
					/* Disable the source if we are removing it. */
					u64 mask = 1ull << n->bit;
					u64 csr = cvmx_read_csr(n->mask_reg);
					csr &= ~mask;
					cvmx_write_csr(n->mask_reg, csr);
				}

				*pw = n->next;
				if (n->is_hwint) {
					rv = true;
					write_unlock(&octeon_hw_status_lock);
					free_irq(n->irq, n);
				}
				kfree(n);
				if (rv)
					return rv;
				break;
			} else {
				pw = &(*pw)->next;
			}
		}
		/* Stop at the root or if there are more children. */
		if (!parent || parent->child)
			break;
		n = parent;
	}
	return rv;
}

int octeon_hw_status_remove_source(struct octeon_hw_status_reg *leaf)
{
	int rv = 0;
	bool already_unlocked = false;
	struct octeon_hw_status_node *n;

	write_lock(&octeon_hw_status_lock);

	n = find_node(octeon_hw_status_roots, leaf);
	if (!n) {
		rv = -ENODEV;
		goto out;
	}

	n->users--;
	if (n->users == 0) {
		already_unlocked = dispose_of_node(n);
	}
out:
	if (!already_unlocked)
		write_unlock(&octeon_hw_status_lock);
	return rv;
}
EXPORT_SYMBOL(octeon_hw_status_remove_source);


struct enable_cb_data {
	u64 reg;
	u64 requested_mask;
	u64 mask_reg;
	u64 valid_mask;
};

static int enable_cb(struct octeon_hw_status_node *n, void *arg)
{
	struct enable_cb_data *d = arg;

	if (n->reg == d->reg && (d->requested_mask & (1ul << n->bit))) {
		d->valid_mask |= (1ul << n->bit);
		WARN(d->mask_reg && d->mask_reg != n->mask_reg,
		     "mask reg mismatch %llu", n->reg);
		d->mask_reg = n->mask_reg;
	}
	return 0;
}

int octeon_hw_status_enable(u64 reg, u64 bit_mask)
{
	struct enable_cb_data cbd;

	memset(&cbd, 0, sizeof(cbd));
	cbd.reg = reg;
	cbd.requested_mask = bit_mask;

	read_lock(&octeon_hw_status_lock);

	visit_leaves(octeon_hw_status_roots, true, enable_cb, &cbd);

	if (cbd.valid_mask && cbd.mask_reg) {
		u64 csr = cvmx_read_csr(cbd.mask_reg);
		csr |= cbd.valid_mask;
		cvmx_write_csr(cbd.mask_reg, csr);
	}

	read_unlock(&octeon_hw_status_lock);
	return 0;
}
EXPORT_SYMBOL(octeon_hw_status_enable);

int octeon_hw_status_disable(u64 reg, u64 bit_mask)
{
	struct enable_cb_data cbd;

	memset(&cbd, 0, sizeof(cbd));
	cbd.reg = reg;
	cbd.requested_mask = bit_mask;

	read_lock(&octeon_hw_status_lock);

	visit_leaves(octeon_hw_status_roots, true, enable_cb, &cbd);

	if (cbd.valid_mask && cbd.mask_reg) {
		u64 csr = cvmx_read_csr(cbd.mask_reg);
		csr &= ~cbd.valid_mask;
		cvmx_write_csr(cbd.mask_reg, csr);
	}

	read_unlock(&octeon_hw_status_lock);
	return 0;
}
EXPORT_SYMBOL(octeon_hw_status_disable);
