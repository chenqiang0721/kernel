/*
 * Module for modifying the secmark field of the skb, for use by
 * security subsystems.
 *
 * Based on the nfmark match by:
 * (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * (C) 2006,2008 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/selinux.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_SECMARK.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@redhat.com>");
MODULE_DESCRIPTION("Xtables: packet security mark modification");
MODULE_ALIAS("ipt_SECMARK");
MODULE_ALIAS("ip6t_SECMARK");

#define PFX "SECMARK: "

static u8 mode;

static unsigned int
secmark_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	u32 secmark = 0;
	const struct xt_secmark_target_info *info = par->targinfo;

	BUG_ON(info->mode != mode);

	switch (mode) {
	case SECMARK_MODE_SEL:
		secmark = info->u.sel.selsid;
		break;

	default:
		BUG();
	}

	skb->secmark = secmark;
	return XT_CONTINUE;
}

static bool checkentry_selinux(struct xt_secmark_target_info *info)
{
	int err;
	struct xt_secmark_target_selinux_info *sel = &info->u.sel;

	sel->selctx[SECMARK_SELCTX_MAX - 1] = '\0';

	err = selinux_string_to_sid(sel->selctx, &sel->selsid);
	if (err) {
		if (err == -EINVAL)
			printk(KERN_INFO PFX "invalid SELinux context \'%s\'\n",
			       sel->selctx);
		return false;
	}

	if (!sel->selsid) {
		printk(KERN_INFO PFX "unable to map SELinux context \'%s\'\n",
		       sel->selctx);
		return false;
	}

	err = selinux_secmark_relabel_packet_permission(sel->selsid);
	if (err) {
		printk(KERN_INFO PFX "unable to obtain relabeling permission\n");
		return false;
	}

	selinux_secmark_refcount_inc();
	return true;
}

static bool secmark_tg_check(const struct xt_tgchk_param *par)
{
	struct xt_secmark_target_info *info = par->targinfo;

	if (strcmp(par->table, "mangle") != 0 &&
	    strcmp(par->table, "security") != 0) {
		printk(KERN_INFO PFX "target only valid in the \'mangle\' "
		       "or \'security\' tables, not \'%s\'.\n", par->table);
		return false;
	}

	if (mode && mode != info->mode) {
		printk(KERN_INFO PFX "mode already set to %hu cannot mix with "
		       "rules for mode %hu\n", mode, info->mode);
		return false;
	}

	switch (info->mode) {
	case SECMARK_MODE_SEL:
		if (!checkentry_selinux(info))
			return false;
		break;

	default:
		printk(KERN_INFO PFX "invalid mode: %hu\n", info->mode);
		return false;
	}

	if (!mode)
		mode = info->mode;
	return true;
}

static void secmark_tg_destroy(const struct xt_tgdtor_param *par)
{
	switch (mode) {
	case SECMARK_MODE_SEL:
		selinux_secmark_refcount_dec();
	}
}

static struct xt_target secmark_tg_reg __read_mostly = {
	.name       = "SECMARK",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = secmark_tg_check,
	.destroy    = secmark_tg_destroy,
	.target     = secmark_tg,
	.targetsize = sizeof(struct xt_secmark_target_info),
	.me         = THIS_MODULE,
};

static int __init secmark_tg_init(void)
{
	return xt_register_target(&secmark_tg_reg);
}

static void __exit secmark_tg_exit(void)
{
	xt_unregister_target(&secmark_tg_reg);
}

module_init(secmark_tg_init);
module_exit(secmark_tg_exit);
