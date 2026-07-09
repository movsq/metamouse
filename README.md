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

## Requirements

- A kernel built with `CONFIG_HID=y`/`m` (universal) and headers for the running
  kernel (`linux-headers`, `kernel-devel`, …).
- `make` and a C compiler.
- A libinput-based session (Wayland or X) for the remapped key to reach apps.
- `systemd` and `udev` for the auto-rebind rule (otherwise bind the driver
  manually — see below).

## Build

    make

This produces `metamouse.ko`. Run `make help` to list every target.

## Configuration

The **target device** is matched by USB VID/PID in the driver's `id_table`
(`metamouse.c`); change it there for a different mouse. The **remap** is tuned by
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
    sudo dkms install metamouse/2.0.0

If you had previously run `make install` (Manual, below), DKMS will refuse to
overwrite the module already sitting in `updates/` — re-run the install with
`--force` to let DKMS take ownership of it:

    sudo dkms install metamouse/2.0.0 --force

Then install the parameter, autoload, and udev configs (edit them for your
device first):

    sudo cp etc/modprobe.d/metamouse.conf     /etc/modprobe.d/
    sudo cp etc/modules-load.d/metamouse.conf /etc/modules-load.d/
    sudo cp etc/udev/rules.d/99-metamouse.rules /etc/udev/rules.d/
    sudo install -Dm755 usr/lib/metamouse/rebind /usr/lib/metamouse/rebind
    sudo udevadm control --reload

### Manual

    sudo make install
    sudo udevadm control --reload

`make install` builds the module, installs it for the running kernel, runs
`depmod`, and copies the `etc/` templates plus the udev helper into place.
Re-run it after a kernel upgrade (DKMS avoids this).

### Quick test without installing

    make
    sudo insmod metamouse.ko src_usage=0x00090005 dst_keycode=125
    # then bind the device (see below)
    sudo rmmod metamouse

## Claiming the device from hid-generic

A mouse is normally held by `hid-generic`. metamouse only takes effect once it
binds the device. The installed udev rule does this automatically on boot and on
every replug: when the matching mouse appears it runs `usr/lib/metamouse/rebind`,
which `modprobe`s metamouse and moves the device off `hid-generic`.

The helper is fail-safe: it only unbinds the mouse from its current driver once
metamouse is actually loaded, and if the handoff fails it rebinds the device to
the driver that held it before. So a missing or stale module — e.g. one not yet
rebuilt after a kernel upgrade — leaves the mouse on `hid-generic` (remap
inactive but the mouse fully working) rather than stranding it with no driver.

To do it by hand for an already-connected mouse (find the hid id under
`ls /sys/bus/hid/drivers/hid-generic/`):

    id=0003:1AF3:0001.0006
    echo -n "$id" | sudo tee /sys/bus/hid/drivers/hid-generic/unbind
    echo -n "$id" | sudo tee /sys/bus/hid/drivers/metamouse/bind

## Verification

Check the kernel log:

    sudo dmesg | grep metamouse
    # metamouse 0003:1AF3:0001.000X: bound <name> (1af3:0001)
    # metamouse 0003:1AF3:0001.000X: remap usage 0x00090005 -> EV_KEY 125

Confirm the driver actually holds the device:

    cat /sys/bus/hid/devices/0003:1AF3:0001.*/uevent | grep DRIVER
    # DRIVER=metamouse

The device should advertise the keyboard capability with **no** unbind/rebind
dance — on Sway:

    swaymsg -t get_inputs    # the mouse appears as both pointer and keyboard

Finally, hold the remapped button and trigger any binding that uses the target
modifier (e.g. a `$mod+Return` shortcut); it should fire.

## Uninstall

    sudo make uninstall                       # manual install
    sudo dkms remove metamouse/2.0.0 --all    # DKMS install
    sudo udevadm control --reload

## License

GPL-2.0-or-later — see [COPYING](COPYING). The HID core exports GPL-only
symbols, so the module must be GPL.
