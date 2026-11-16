#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2026 isoriano1968
#

# applesmc: driver for the Apple SMC of Intel Macs (temperatures as
# ksensors, every key through /dev/applesmc; the only writes are the fan
# minimums), smcstat(8), and the fan presets: smcfan(8), the service
# svc:/system/applesmc-fan and the rights profile "Apple SMC Fan Control".
#
# Pristine source: illumos-gate 586678ac1b (the Mac Pro's OS build).  The
# driver is new code: patches/0001 is the illumos-gate commit that adds it
# (branch applesmc of the VM's ~/src/illumos-usbroot; upstream as illumos
# issue 18700); patches/0002 adds the fan minimum writes, smcfan(8), its SMF
# service and rights profile (cmd/smcfan); patches/0003 lets the driver and
# smcfan handle fans whose keys are "flt " instead of fpe2 (newer SMCs, the
# MacBook Pro 2016-2017).  All apply to the whole tree.
# Only the directories the patches touch and the kernel headers the driver
# includes that system/header does not ship (sys/sensors.h, sys/acpica.h,
# sys/acpi/) are extracted.  Built out of tree as ports/audiohd-apple: the flags of
# uts/Makefile.uts + uts/intel/Makefile.intel, linked as the patch's
# uts/i86pc/applesmc/Makefile (ld -r -dy -N misc/acpica); smcstat with the
# flags of cmd/Makefile.cmd.64, and so is smcfan.  files/applesmc_fan_test.c
# is a userland unit test of the driver's fan write policy, built from the
# patched applesmc.c (gmake check runs it).
#
# The package is new (not part of OpenIndiana): driver/sensor/applesmc, the
# same contents as the patch's usr/src/pkg/manifests/driver-sensor-applesmc.p5m,
# plus files/applesmc_link.c: the patch's devfsadm rule for /dev/applesmc
# as a link module of its own (pkg cannot deliver into /dev).
#
#   gmake / gmake check / gmake publish / gmake clean   (GNU make, on OI)

COMPONENT_NAME=		applesmc
GATE_COMMIT=		586678ac1bcbd77644a6ec4dac7f79d676cfb4ec
COMPONENT_SRC=		illumos-gate-$(GATE_COMMIT)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
	sha256:ad278ac9dcb58e92e0a441a5a302c25692ca4c1ee435d7464b79858c04e40acf
COMPONENT_ARCHIVE_URL=	\
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz
COMPONENT_LICENSE=	CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

ARCHIVE_DIR?=		$(HOME)/src/oi-userland/archives
BUILD_DIR=		$(CURDIR)/build
SRC_DIR=		$(BUILD_DIR)/$(COMPONENT_SRC)
UTS=			$(SRC_DIR)/usr/src/uts
MODULE=			applesmc
LINKMOD=		applesmc_link.so

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar
GPATCH=			/usr/bin/gpatch

# What the patch changes, and the headers the driver needs.
EXTRACT=	usr/src/cmd/Makefile usr/src/cmd/devfsadm/i386 \
		usr/src/man/man4d/Makefile usr/src/man/man8/Makefile \
		usr/src/pkg/manifests/system-header.p5m \
		usr/src/uts/common/sys/Makefile usr/src/uts/common/sys/sensors.h \
		usr/src/uts/i86pc/Makefile.files usr/src/uts/i86pc/Makefile.i86pc \
		usr/src/uts/intel/sys/acpica.h usr/src/uts/intel/sys/acpi

# As ports/audiohd-apple (Makefile.master + uts/Makefile.uts + Makefile.intel).
CFLAGS=		-m64 -Ui386 -U__i386 -O2 -std=gnu99
CFLAGS+=	-mcmodel=kernel -mno-red-zone -ffreestanding -mno-mmx -mno-sse
CFLAGS+=	-msave-args -fno-shrink-wrap
CFLAGS+=	-mindirect-branch=thunk-extern -mindirect-branch-register
CFLAGS+=	-fno-asynchronous-unwind-tables -fstack-protector-strong
CFLAGS+=	-fno-inline-small-functions -fno-inline-functions-called-once
CFLAGS+=	-fno-ipa-cp -fno-ipa-icf -fno-clone-functions
CFLAGS+=	-fno-reorder-functions -fno-reorder-blocks-and-partition
CFLAGS+=	-fno-aggressive-loop-optimizations
CFLAGS+=	--param=max-inline-insns-single=450
CFLAGS+=	-fno-builtin -fident -gdwarf-2
CPPFLAGS=	-D_ASM_INLINES -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		-D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4
# Kernel headers that system/header does not ship, from the same tree,
# searched after /usr/include.
CPPFLAGS+=	-idirafter $(UTS)/common -idirafter $(UTS)/intel
CWARN=		-Wall -Werror -Winline -Wno-missing-braces -Wno-sign-compare \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-unused -Wno-empty-body -Wno-maybe-uninitialized \
		-Wno-switch -Wno-parentheses

# smcstat: cmd/Makefile.cmd.64, CSTD_GNU17, CCVERBOSE.
UCFLAGS=	-m64 -O2 -std=gnu17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fstack-protector-strong -D_REENTRANT
UCPPFLAGS=	-idirafter $(UTS)/common

SMCFAN_DIR=	$(SRC_DIR)/usr/src/cmd/smcfan

all: $(BUILD_DIR)/$(MODULE) $(BUILD_DIR)/smcstat $(BUILD_DIR)/smcfan \
    $(BUILD_DIR)/$(LINKMOD)

$(ARCHIVE_DIR)/$(COMPONENT_ARCHIVE):
	mkdir -p $(ARCHIVE_DIR)
	/usr/bin/curl -fL -o $@.part $(COMPONENT_ARCHIVE_URL)
	mv $@.part $@

$(BUILD_DIR)/.unpacked: $(ARCHIVE_DIR)/$(COMPONENT_ARCHIVE)
	test "sha256:`/usr/bin/digest -a sha256 $<`" = \
	    "$(COMPONENT_ARCHIVE_HASH)" || \
	    { echo "$<: wrong sha256" >&2; exit 1; }
	rm -rf $(SRC_DIR)
	mkdir -p $(BUILD_DIR)
	$(GTAR) -xzf $< -C $(BUILD_DIR) $(EXTRACT:%=$(COMPONENT_SRC)/%)
	touch $@

$(BUILD_DIR)/.patched: $(BUILD_DIR)/.unpacked $(wildcard patches/*.patch)
	for p in patches/*.patch; do \
		echo "== $$p"; \
		$(GPATCH) -d $(SRC_DIR) -p1 -N --no-backup-if-mismatch < $$p || exit 1; \
	done
	touch $@

$(BUILD_DIR)/applesmc.o: $(BUILD_DIR)/.patched
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $(UTS)/i86pc/io/applesmc.c

$(BUILD_DIR)/$(MODULE): $(BUILD_DIR)/applesmc.o
	$(LD) -r -dy -N misc/acpica -o $@.tmp $<
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

$(BUILD_DIR)/smcstat: $(BUILD_DIR)/.patched
	$(CC) $(UCFLAGS) $(UCPPFLAGS) -o $@ $(SRC_DIR)/usr/src/cmd/smcstat/smcstat.c
	/usr/bin/digest -a sha256 $@

$(BUILD_DIR)/smcfan: $(BUILD_DIR)/.patched
	$(CC) $(UCFLAGS) $(UCPPFLAGS) -o $@ $(SMCFAN_DIR)/smcfan.c
	/usr/bin/digest -a sha256 $@

# The fan write policy of the driver, cut out of the patched applesmc.c,
# and its unit test.
$(BUILD_DIR)/applesmc_fan_policy.h: $(BUILD_DIR)/.patched
	/usr/bin/sed -n '/applesmc fan policy: begin/,/applesmc fan policy: end/p' \
	    $(UTS)/i86pc/io/applesmc.c > $@
	test -s $@

$(BUILD_DIR)/applesmc_fan_test: files/applesmc_fan_test.c \
    $(BUILD_DIR)/applesmc_fan_policy.h
	$(CC) $(UCFLAGS) -I$(BUILD_DIR) -o $@ files/applesmc_fan_test.c

# devfsadm link module for /dev/applesmc (files/applesmc_link.c; the same
# rule as the patch's misc_link_i386.c).  32-bit, as devfsadm and its
# modules (cmd/devfsadm/Makefile.com: -G, -h, -ldevinfo -lc).
$(BUILD_DIR)/$(LINKMOD): files/applesmc_link.c
	mkdir -p $(BUILD_DIR)
	$(CC) -m32 -O2 -std=gnu17 -Wall -Wextra -Werror -fPIC -shared \
	    -Wl,-h$(LINKMOD) -o $@ $< -ldevinfo -lc

# Every symbol the module leaves undefined must be defined by the running
# kernel (unix, genunix) or by misc/acpica (its -N dependency).
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/acpica
# smcfan must fail cleanly (exit 3) where there is no /dev/applesmc.
check: all $(BUILD_DIR)/applesmc_fan_test
	$(BUILD_DIR)/applesmc_fan_test
	/usr/sbin/svccfg validate $(SMCFAN_DIR)/applesmc-fan.xml
	/usr/bin/sh -n $(SMCFAN_DIR)/svc-applesmc-fan
	test -c /dev/applesmc || { $(BUILD_DIR)/smcfan; test $$? -eq 3; }
	/usr/bin/nm -u -h $(BUILD_DIR)/$(MODULE) | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of $(MODULE) are in unix, genunix or misc/acpica"
	/usr/bin/elfdump -d $(BUILD_DIR)/$(MODULE) | grep NEEDED
	/usr/bin/mandoc -Tlint -Wwarning $(SRC_DIR)/usr/src/man/man4d/applesmc.4d \
	    $(SRC_DIR)/usr/src/man/man8/smcstat.8 \
	    $(SRC_DIR)/usr/src/man/man8/smcfan.8

# The package version: 0.5.11 as the osnet drivers, branch part = the
# year, the OS build the module was built and tested against (the VM's),
# and our revision.  Kernel interfaces used: DDI, ksensor(9E), acpica.
PKG_VERSION=	0.5.11-2026.0.0.4
MIN_OSNET=	0.5.11-2026.0.0.23344
GATE_SHORT=	$(shell echo $(GATE_COMMIT) | cut -c1-10)
REPO?=		$(HOME)/oi-repo
PROTO=		$(BUILD_DIR)/proto

publish: check
	rm -rf $(PROTO)
	mkdir -p $(PROTO)
	cp $(BUILD_DIR)/$(MODULE) $(BUILD_DIR)/smcstat $(BUILD_DIR)/smcfan \
	    $(BUILD_DIR)/$(LINKMOD) $(PROTO)/
	cp $(SMCFAN_DIR)/applesmc-fan.xml $(SMCFAN_DIR)/svc-applesmc-fan \
	    $(SMCFAN_DIR)/exec_attr $(SMCFAN_DIR)/prof_attr $(PROTO)/
	cp $(UTS)/i86pc/io/applesmc.conf $(PROTO)/
	cp $(SRC_DIR)/usr/src/man/man4d/applesmc.4d $(PROTO)/
	cp $(SRC_DIR)/usr/src/man/man8/smcstat.8 $(PROTO)/
	cp $(SRC_DIR)/usr/src/man/man8/smcfan.8 $(PROTO)/
	cp $(COMPONENT_LICENSE_FILE) $(PROTO)/
	/usr/bin/pkgmogrify -D PKG_VERSION=$(PKG_VERSION) \
	    -D MIN_OSNET=$(MIN_OSNET) -D GATE_COMMIT=$(GATE_COMMIT) \
	    -D GATE_SHORT=$(GATE_SHORT) $(COMPONENT_NAME).p5m > $(BUILD_DIR)/manifest
	/usr/bin/pkglint $(BUILD_DIR)/manifest
	/usr/bin/pkgsend -s $(REPO) publish -d $(PROTO) \
	    --fmri-in-manifest $(BUILD_DIR)/manifest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean publish
