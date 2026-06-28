// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * metamouse - remap a mouse button to a keyboard keycode via a HID driver
 *
 * Binds the target mouse (matched by USB VID/PID) and rewrites one HID usage
 * to a key at input-mapping time. hid_map_usage() records the new type/code
 * and hid-input then sets the matching capability bit during input
 * registration, so the remapped key is a declared capability the instant the
 * input device appears -- before libinput opens it. No kprobe, no pt_regs, no
 * register patching, no capability-stamp race.
 *
 * Copyright (C) 2026 Vojtěch Sedláček <ska_coral6n@icloud.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>

#define USB_VENDOR_ID_ZOWIE	0x1af3
#define USB_DEVICE_ID_ZOWIE	0x0001

/* HID usage of the source control. Default 0x00090005 = Button page
 * (HID_UP_BUTTON, 0x00090000) | button 5 = the forward thumb button, which
 * hid-input would otherwise map to BTN_EXTRA (0x114 / 276). For button N use
 * 0x00090000 | N (e.g. back button 4 = 0x00090004). */
static unsigned int src_usage = HID_UP_BUTTON | 0x05;
module_param(src_usage, uint, 0644);
MODULE_PARM_DESC(src_usage, "HID usage to remap from (default 0x00090005 = forward button)");

/* Input keycode to emit instead. Default 125 = KEY_LEFTMETA (Super/Mod4). */
static unsigned int dst_keycode = KEY_LEFTMETA;
module_param(dst_keycode, uint, 0644);
MODULE_PARM_DESC(dst_keycode, "input keycode to remap to (default 125 = KEY_LEFTMETA)");

/*
 * Called once per usage while the input device is built, before the default
 * HID->input mapping. For the source usage we install our own mapping
 * (EV_KEY -> dst_keycode) and return 1 so hid-input skips its default; every
 * other usage (movement, wheel, other buttons) returns 0 and maps normally.
 */
static int metamouse_input_mapping(struct hid_device *hdev,
				   struct hid_input *hi,
				   struct hid_field *field,
				   struct hid_usage *usage,
				   unsigned long **bit, int *max)
{
	if (usage->hid != src_usage)
		return 0;

	hid_map_usage(hi, usage, bit, max, EV_KEY, dst_keycode);
	hid_info(hdev, "remap usage 0x%08x -> EV_KEY %u\n", src_usage, dst_keycode);
	return 1;
}

static int metamouse_probe(struct hid_device *hdev,
			   const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid_parse failed: %d\n", ret);
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hid_hw_start failed: %d\n", ret);
		return ret;
	}

	hid_info(hdev, "bound %s (%04x:%04x)\n",
		 hdev->name, id->vendor, id->product);
	return 0;
}

static const struct hid_device_id metamouse_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ZOWIE, USB_DEVICE_ID_ZOWIE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, metamouse_devices);

static struct hid_driver metamouse_driver = {
	.name		= "metamouse",
	.id_table	= metamouse_devices,
	.probe		= metamouse_probe,
	.input_mapping	= metamouse_input_mapping,
};
module_hid_driver(metamouse_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vojtěch Sedláček <ska_coral6n@icloud.com>");
MODULE_DESCRIPTION("Remap a mouse button to a modifier via a HID input_mapping driver");
MODULE_VERSION("2.0.0");
