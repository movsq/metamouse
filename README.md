# metamouse

A Linux kernel module that remaps a mouse button to a keyboard keycode using
kprobes — no kernel-tree patch, no userspace daemon, no `uinput` device. The
original use case is turning a mouse thumb button into **Super/Mod4**
(`KEY_LEFTMETA`) so it can act as a window-manager modifier, but any
`src_code` → `dst_code` mapping works.

> **Architecture:** x86-64 only. The in-flight rewrite reads function arguments
> from `pt_regs` using the SysV AMD64 calling convention; other architectures
> would need that register access ported.

## How it works

A button press normally travels `input_event()` → `input_handle_event()` →
evdev → libinput → compositor. Two things stop a pointer device from simply
emitting a key, and metamouse clears both with two kprobes:

- **`input_event`** — the pre-handler rewrites the event `code` argument in
  flight (`src_code` → `dst_code`) by patching the saved register that holds
  argument 3. The event leaves the kernel already looking like the target key.
- **`input_register_device`** — `input_handle_event()` silently drops any code
  that is not set in the device's `keybit` capability bitmap, and libinput
  caches a device's capabilities the moment it *opens* the device. So the module
  stamps `set_bit(dst_code, dev->keybit)` at registration time, before libinput
  ever opens the device, making the new key a declared capability.

The capability stamp is per-device and idempotent: every freshly registered
`input_dev` is re-checked, so a hot-replug — which hands the kernel a brand-new
struct with a clean bitmap — is handled correctly. Both handlers run in atomic
context and touch nothing that can sleep.

## Requirements

- An **x86-64** Linux system (see the note above).
- A kernel built with `CONFIG_KPROBES=y` (the default on virtually all distros).
- Kernel headers for the running kernel (`linux-headers`, `kernel-devel`, …).
- `make` and a C compiler.
- A libinput-based session (Wayland or X) for the remapped key to reach apps.

## Build

    make

This produces `metamouse.ko`. Run `make help` to list every target.

## Configuration

The module takes three parameters:

| Parameter     | Default                | Meaning                                                         |
|---------------|------------------------|-----------------------------------------------------------------|
| `src_code`    | `276` (`BTN_EXTRA`)    | Input event code to intercept and replace.                      |
| `dst_code`    | `125` (`KEY_LEFTMETA`) | Event code to emit instead.                                     |
| `target_name` | `""` (any device)      | Exact `input_dev` name to act on; empty matches any device that emits `src_code`. |

Finding the values for **your** device:

- **`src_code`** — run `sudo evtest`, select the mouse, press the button you
  want to remap, and read the `code NNN` it prints.
- **`target_name`** — the `Name=` line in `cat /proc/bus/input/devices` (or the
  name `evtest` shows). Set it to pin the remap to one device; recommended when
  more than one device can emit `src_code`.
- **`dst_code`** — any keycode from `<linux/input-event-codes.h>` (e.g. `125`
  for the left Super/Meta key).

## Install

### DKMS (recommended — rebuilds automatically across kernel upgrades)

    sudo dkms add .
    sudo dkms install metamouse/1.0.0

Then install the parameter and boot-autoload configs (edit them for your device
first):

    sudo cp etc/modprobe.d/metamouse.conf     /etc/modprobe.d/
    sudo cp etc/modules-load.d/metamouse.conf /etc/modules-load.d/

### Manual

    sudo make install

`make install` builds the module, installs it for the running kernel, runs
`depmod`, and copies the same `etc/` templates into `/etc`. Re-run it after a
kernel upgrade (DKMS avoids this).

### Quick test without installing

    make
    sudo insmod metamouse.ko src_code=276 dst_code=125 target_name="Your Device Name"
    # ...
    sudo rmmod metamouse

## Persistence and boot-load

Loading metamouse **before** the session starts is the cleanest setup: the
capability is stamped at the device's first registration, before libinput opens
it, so no further action is needed. That is what the two `/etc` files do —
`modules-load.d` loads the module at boot and `modprobe.d` supplies its
parameters.

## Applying it to an already-running device

If you load the module while the device is already registered, libinput has
already cached the old capabilities. Force the input device to be recreated so
the registration probe fires on a fresh struct. For a USB mouse, unbind and
rebind its usbhid interface (a physical unplug/replug does the same):

    # Find the interface name under: ls /sys/bus/usb/drivers/usbhid/
    iface=7-1.2:1.0
    echo -n "$iface" | sudo tee /sys/bus/usb/drivers/usbhid/unbind
    sleep 1
    echo -n "$iface" | sudo tee /sys/bus/usb/drivers/usbhid/bind

`udevadm trigger --action=change` does **not** work here — it only emits a
uevent and never re-registers the input device, so the probe never fires.

## Verification

Check the kernel log:

    sudo dmesg | grep metamouse
    # metamouse: armed; src=276 dst=125 target='...'
    # metamouse: set_bit(125) on '<device>'

The device should now advertise the keyboard capability — on Sway:

    swaymsg -t get_inputs    # the mouse appears as both pointer and keyboard

Finally, hold the remapped button and trigger any binding that uses the target
modifier (e.g. a `$mod+Return` shortcut); it should fire.

## Uninstall

    sudo make uninstall                       # manual install
    sudo dkms remove metamouse/1.0.0 --all    # DKMS install

## License

GPL-2.0-or-later — see [COPYING](COPYING). kprobes exports GPL-only symbols, so
the module must be GPL.
