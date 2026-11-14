# SPDX-License-Identifier: GPL-2.0-or-later
#
# Convenience targets for the out-of-tree metamouse module. The module build
# itself is described in Kbuild; this file is only the user-facing front end.

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
CONF := $(DESTDIR)/etc/modprobe.d/metamouse.conf

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

# Load/unload the freshly built module for a quick test (no install).
load: all
	insmod metamouse.ko

unload:
	rmmod metamouse

# Install for the running kernel (into updates/) plus the parameter config.
install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install
	depmod -a $(KVER)
	install -Dm644 etc/modprobe.d/metamouse.conf $(CONF)

uninstall:
	rm -f /lib/modules/$(KVER)/updates/metamouse.ko* $(CONF)
	depmod -a $(KVER)

help:
	@echo 'metamouse - out-of-tree kernel module'
	@echo
	@echo 'Targets:'
	@echo '  make            build the module (metamouse.ko)'
	@echo '  make clean      remove build artifacts'
	@echo '  make load       insmod the built module (root)'
	@echo '  make unload     rmmod the module (root)'
	@echo '  make install    install module + modprobe config (root)'
	@echo '  make uninstall  remove installed module + config (root)'
	@echo
	@echo 'Overridable variables: KVER (=$(KVER)), KDIR, DESTDIR.'

.PHONY: all clean load unload install uninstall help
