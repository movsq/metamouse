# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convenience targets for the out-of-tree metamouse module. The module build
# itself is described in Kbuild; this file is only the user-facing front end.

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

MODULE  := metamouse
CONFDIR := $(DESTDIR)/etc
UDEVDIR := $(DESTDIR)/etc/udev/rules.d
LIBDIR  := $(DESTDIR)/usr/lib/metamouse

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

# Load/unload the freshly built module for a quick test (no install).
load: all
	insmod $(MODULE).ko

unload:
	rmmod $(MODULE)

# Install for the running kernel plus the /etc parameter and autoload configs.
install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install
	depmod -a $(KVER)
	install -Dm644 etc/modprobe.d/$(MODULE).conf       $(CONFDIR)/modprobe.d/$(MODULE).conf
	install -Dm644 etc/modules-load.d/$(MODULE).conf   $(CONFDIR)/modules-load.d/$(MODULE).conf
	install -Dm644 etc/udev/rules.d/99-$(MODULE).rules $(UDEVDIR)/99-$(MODULE).rules
	install -Dm755 usr/lib/metamouse/rebind            $(LIBDIR)/rebind

uninstall:
	rm -f /lib/modules/$(KVER)/extra/$(MODULE).ko
	rm -f $(CONFDIR)/modprobe.d/$(MODULE).conf
	rm -f $(CONFDIR)/modules-load.d/$(MODULE).conf
	rm -f $(UDEVDIR)/99-$(MODULE).rules
	rm -f $(LIBDIR)/rebind
	depmod -a $(KVER)

help:
	@echo 'metamouse - out-of-tree kernel module'
	@echo
	@echo 'Targets:'
	@echo '  make            build the module (metamouse.ko)'
	@echo '  make clean      remove build artifacts'
	@echo '  make load       insmod the built module (root)'
	@echo '  make unload     rmmod the module (root)'
	@echo '  make install    install module + /etc configs (root)'
	@echo '  make uninstall  remove installed module + configs (root)'
	@echo
	@echo 'Overridable variables: KVER (=$(KVER)), KDIR, DESTDIR.'

.PHONY: all clean load unload install uninstall help
