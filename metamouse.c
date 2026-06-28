// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * metamouse - remap a mouse button to a keyboard keycode via Linux kprobes
 *
 * Copyright (C) 2026 Vojtěch Sedláček <ska_coral6n@icloud.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/ptrace.h>
#include <linux/bitops.h>
#include <linux/string.h>

/* The in-flight code rewrite reads/writes pt_regs by the x86-64 SysV calling
 * convention (di/si/dx). On other architectures pt_regs has no such members,
 * so fail at compile time with a clear message rather than a cryptic one. */
#ifndef CONFIG_X86_64
#error "metamouse uses the x86-64 SysV register layout; port pt_regs access for other arches"
#endif

/* "" => match any device that declares src_code (fine for a single mouse).
 * Set to the exact dev->name from the observe pass for precise targeting
 * AND to enable the registration-time capability stamp below. */
static char *target_name = "";
module_param(target_name, charp, 0644);
MODULE_PARM_DESC(target_name, "exact input_dev name ('' = any device declaring src_code)");

static unsigned int src_code = BTN_EXTRA;    /* forward side button, 0x114 */
module_param(src_code, uint, 0644);
MODULE_PARM_DESC(src_code, "source key/button code to remap from");

static unsigned int dst_code = KEY_LEFTMETA; /* Mod4 / Super, 0x7d */
module_param(dst_code, uint, 0644);
MODULE_PARM_DESC(dst_code, "key code to remap to");

static bool dev_matches(struct input_dev *dev)
{
	if (!dev || !dev->name)
		return false;
	if (target_name[0])
		return strcmp(dev->name, target_name) == 0;
	/* match-any mode: any device that can emit the source button */
	return test_bit(src_code, dev->keybit);
}

/* Per-device and idempotent: NOT a global flag. A udev re-add hands us a
 * fresh input_dev with a clean keybit, so the cap must be re-checked on
 * that struct — a global bool would skip it and you'd loop forever
 * "fixing" it. */
static void ensure_cap(struct input_dev *dev)
{
	if (!test_bit(dst_code, dev->keybit)) {
		set_bit(dst_code, dev->keybit);
		pr_info("metamouse: set_bit(%u) on '%s'\n", dst_code, dev->name);
	}
}

/* Probe 1: stamp the capability at registration, before libinput opens the
 * device and caches its caps. Covers any matching device that appears after
 * the module is loaded (incl. the new struct produced by a udev re-add). */
static int reg_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct input_dev *dev = (struct input_dev *)regs->di;
	if (dev_matches(dev))
		ensure_cap(dev);
	return 0;
}

/* Probe 2: rewrite the button code in-flight. Also stamps the cap as a
 * fallback for a device that was already registered before load. */
static int ev_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct input_dev *dev = (struct input_dev *)regs->di;
	unsigned int type     = (unsigned int)regs->si;
	unsigned int code     = (unsigned int)regs->dx;

	if (type != EV_KEY || code != src_code)
		return 0;
	if (!dev_matches(dev))
		return 0;

	ensure_cap(dev);
	regs->dx = dst_code;   /* rewrite arg3 (code) */
	return 0;
}

static struct kprobe kp_reg = {
	.symbol_name = "input_register_device",
	.pre_handler = reg_pre,
};
static struct kprobe kp_ev = {
	.symbol_name = "input_event",
	.pre_handler = ev_pre,
};

static int __init m_init(void)
{
	int ret = register_kprobe(&kp_reg);
	if (ret < 0) {
		pr_err("metamouse: kprobe input_register_device failed: %d\n", ret);
		return ret;
	}
	ret = register_kprobe(&kp_ev);
	if (ret < 0) {
		pr_err("metamouse: kprobe input_event failed: %d\n", ret);
		unregister_kprobe(&kp_reg);
		return ret;
	}
	pr_info("metamouse: armed; src=%u dst=%u target='%s'\n",
		src_code, dst_code, target_name[0] ? target_name : "(any w/ src_code)");
	return 0;
}
static void __exit m_exit(void)
{
	unregister_kprobe(&kp_ev);
	unregister_kprobe(&kp_reg);
	pr_info("metamouse: removed\n");
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vojtěch Sedláček <ska_coral6n@icloud.com>");
MODULE_DESCRIPTION("Remap a mouse button to a modifier via kprobe on input_event");
MODULE_VERSION("1.0.0");
