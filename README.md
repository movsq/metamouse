# metamouse

A small Linux **HID driver** that remaps a mouse button to a keyboard keycode —
no kernel-tree patch, no userspace daemon, no `uinput`. The original use case is
turning a mouse thumb button into **Super/Mod4** (`KEY_LEFTMETA`) so it can act
as a window-manager modifier, but any HID-usage → keycode mapping works.

## How it works

The driver binds the target mouse by its USB vendor/product id and implements
the HID `input_mapping` callback. While the kernel builds the device's input
node, metamouse intercepts one HID usage (by default the forward thumb button,
Button-page usage `0x00090005`) and maps it to `EV_KEY` / `dst_keycode` with
`hid_map_usage()`. hid-input then sets the capability bit for that key as part
of registration.

That single hook clears both obstacles that previously needed workarounds:

- **Kernel capability gate** — `input_handle_event()` drops any code not set in
  the device's `keybit`. Because the mapping is installed at registration time,
  the bit is set for us; the key is a real capability from the start.
- **libinput cap caching** — libinput records a device's capabilities when it
  *opens* the device. Since the capability already exists at registration, it is
  present before libinput ever opens the device — no stamping race, no replug
  dance.

Unlike a kprobe-based approach, this touches no saved registers and is not tied
to any CPU architecture.

Claiming the mouse needs no udev rule or rebind script either: a driver whose
`id_table` matches wins over `hid-generic` when the mouse is plugged in, the
module autoloads from its HID modalias, and if it loads while `hid-generic`
already holds the mouse, the HID core reprobes the device over to metamouse. If
the module is missing (e.g. not rebuilt for a new kernel) the mouse just stays on
`hid-generic` and keeps working, unremapped.

## Requirements

- A kernel built with `CONFIG_HID=y`/`m` (universal) and headers for the running
  kernel (`linux-headers`, `kernel-devel`, …).
- `make` and a C compiler.
- A libinput-based session (Wayland or X) for the remapped key to reach apps.

## Build

    make

This produces `metamouse.ko`. Run `make help` to list every target.

## Configuration

The **target devices** are matched by USB VID/PID in the driver's `id_table`
(`metamouse.c`); add a `HID_USB_DEVICE(vid, pid)` line there for your mouse. The **remap** is tuned by
two module parameters:

| Parameter     | Default              | Meaning                                                              |
|---------------|----------------------|----------------------------------------------------------------------|
| `src_usage`   | `0x00090005`         | HID usage to remap from. `0x00090000` is the Button page; `\| N` selects button N (button 5 = the forward thumb button). |
| `dst_keycode` | `125` (`KEY_LEFTMETA`) | Input keycode to emit instead.                                     |

Finding the values for **your** device:

- **VID/PID** — `lsusb` (e.g. `ID 1af3:0001`), or
  `cat /sys/bus/usb/devices/*/idVendor` / `idProduct`.
- **`src_usage`** — the HID usage of the control. For a standard mouse button N
  it is `0x00090000 | N`. To inspect the report descriptor:
  `cat /sys/bus/hid/devices/<id>/report_descriptor | hidrd-convert` (or read it
  with `usbhid-dump`).
- **`dst_keycode`** — any keycode from `<linux/input-event-codes.h>` (e.g. `125`
  for the left Super/Meta key).

## Install

### DKMS (recommended — rebuilds automatically across kernel upgrades)

    sudo dkms add .
    sudo dkms install metamouse/2.1.0
    sudo cp etc/modprobe.d/metamouse.conf /etc/modprobe.d/

If you had previously run `make install` (Manual, below), DKMS will refuse to
overwrite the module already sitting in `updates/` — re-run the install with
`--force` to let DKMS take ownership of it.

Upgrading from 2.0.x: if your initramfs generator copies `/etc/modules-load.d`
into the image (dracut does), the old module and its autoload entry stay in the
initramfs and load at boot ahead of the new one. Regenerate the initramfs once
after upgrading (`dracut --regenerate-all --force`, `mkinitcpio -P`, or your
distro's equivalent).

### Manual

    sudo make install

`make install` builds the module, installs it for the running kernel, runs
`depmod`, and copies the modprobe config into place.
Re-run it after a kernel upgrade (DKMS avoids this).

### Quick test without installing

    make
    sudo insmod metamouse.ko src_usage=0x00090005 dst_keycode=125
    sudo rmmod metamouse

## Rebinding by hand

The HID core moves a device from `hid-generic` to a newly loaded driver only
once per plug-in, and unloading metamouse leaves the mouse without a driver. So
after an `rmmod`/`insmod` cycle while testing, either replug the mouse or move it
over by hand (find the id under `ls /sys/bus/hid/devices/`):

    id=0003:1AF3:0001.0006
    echo -n "$id" | sudo tee /sys/bus/hid/drivers/hid-generic/unbind  # if bound
    echo -n "$id" | sudo tee /sys/bus/hid/drivers/metamouse/bind

## Verification

Check the kernel log:

    sudo dmesg | grep metamouse
    # metamouse 0003:1AF3:0001.000X: remap usage 0x00090005 -> EV_KEY 125

Confirm the driver actually holds the device:

    cat /sys/bus/hid/devices/0003:1AF3:0001.*/uevent | grep DRIVER
    # DRIVER=metamouse

The device should advertise the keyboard capability — on Sway:

    swaymsg -t get_inputs    # the mouse appears as both pointer and keyboard

Finally, hold the remapped button and trigger any binding that uses the target
modifier (e.g. a `$mod+Return` shortcut); it should fire.

## Uninstall

    sudo make uninstall                       # manual install
    sudo dkms remove metamouse/2.1.0 --all    # DKMS install

## License

GPL-2.0-or-later — see [COPYING](COPYING). The HID core exports GPL-only
symbols, so the module must be GPL.
