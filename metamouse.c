// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * metamouse - remap a mouse button to a keyboard keycode via a HID driver
 *
 * Binds the target mouse (matched by USB VID/PID) and rewrites one HID usage
 * to a key at input-mapping time. hid_map_usage() records the new type/code
 * and hid-input sets the matching capability bit during input registration,
 * so the key is a declared capability before libinput ever opens the device.
 *
 * Claiming the device needs no help: a matching driver beats hid-generic on
 * plug-in, and when this module loads while hid-generic already holds the
 * mouse, hid-core reprobes it over to us. If the module is missing, the mouse
 * simply stays on hid-generic and works unremapped.
 *
 * Copyright (C) 2026 Vojtěch Sedláček <ska_coral6n@icloud.com>
 */

#include <linux/module.h>
#include <linux/hid.h>

/* HID usage to remap. Button page | N = mouse button N; 5 is the forward
 * thumb button (BTN_EXTRA by default), 4 is back. */
static unsigned int src_usage = HID_UP_BUTTON | 5;
module_param(src_usage, uint, 0644);
MODULE_PARM_DESC(src_usage, "HID usage to remap from (default 0x00090005 = forward button)");

static unsigned int dst_keycode = KEY_LEFTMETA;
module_param(dst_keycode, uint, 0644);
MODULE_PARM_DESC(dst_keycode, "input keycode to remap to (default 125 = KEY_LEFTMETA)");

/* Return 1 (mapped by us) for the source usage, 0 (default map) otherwise. */
static int metamouse_input_mapping(struct hid_device *hdev, struct hid_input *hi,
				   struct hid_field *field, struct hid_usage *usage,
				   unsigned long **bit, int *max)
{
	if (usage->hid != src_usage)
		return 0;

	hid_map_usage(hi, usage, bit, max, EV_KEY, dst_keycode);
	hid_info(hdev, "remap usage 0x%08x -> EV_KEY %u\n", src_usage, dst_keycode);
	return 1;
}

/* Same as hid-generic, so every other control behaves exactly as before. */
static int metamouse_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	hdev->quirks |= HID_QUIRK_INPUT_PER_APP;
	return hid_parse(hdev) ?: hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

/* Mice to bind (all their interfaces; ones without src_usage map unchanged). */
static const struct hid_device_id metamouse_devices[] = {
	{ HID_USB_DEVICE(0x1af3, 0x0001) },	/* ZOWIE Gaming mouse */
	{ HID_USB_DEVICE(0x30fa, 0x1440) },	/* INSTANT USB GAMING MOUSE */
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
MODULE_VERSION("2.1.0");
