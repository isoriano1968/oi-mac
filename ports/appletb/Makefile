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


# driver/input/appletb: the Touch Bar of the MacBook Pro 2016-2017 (Apple T1
# "iBridge", USB 05ac:8600).  It binds to the iBridge's light-sensor HID
# interface, finds the Touch Bar interface by its report descriptor, and
# turns the bar on in F-key mode (two control requests); hid(4D) and
# usbkbm(4M) keep the Touch Bar's keys.  See src/appletb.c and
# notes/macbookpro-2017-drivers.md 5f.  New code (CDDL); the Linux
# apple-ib-tb driver was a reference for the protocol only.
#
# Run on OpenIndiana with GNU make:
#   gmake            build ./build/appletb
#   gmake check      unit test of the descriptor lookup; every undefined
#                    symbol must be in the kernel or misc/usba
#   gmake publish    package driver/input/appletb into $(REPO)
#   gmake clean      remove ./build

COMPONENT_NAME=		appletb
COMPONENT_LICENSE=	CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license
# Built against the OS's own headers (sys/usb/usba.h is in system/header).
OSNET_VERSION=		0.5.11-2026.0.0.23344
PKG_VERSION=		0.5.11-2026.0.0.1
REPO?=			$(HOME)/oi-repo

# The iBridge's light-sensor interface in configuration 1.  Assumed 3
# (camera 0 and 1, Touch Bar 2); confirm with prtconf -v on the machine
# before publishing.
ALS_IF?=		3

BUILD_DIR=		$(CURDIR)/build
PROTO=			$(BUILD_DIR)/proto
SRC=			$(CURDIR)/src

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert

# As ports/atopcase: uts/Makefile.uts + uts/intel/Makefile.intel flags.
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
CWARN=		-Wall -Werror -Wno-missing-braces -Wno-sign-compare \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-empty-body -Wno-parentheses

all: $(BUILD_DIR)/appletb

$(BUILD_DIR)/appletb.o: $(SRC)/appletb.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $<

$(BUILD_DIR)/appletb: $(BUILD_DIR)/appletb.o
	$(LD) -r -dy -N misc/usba -o $@.tmp $<
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# The descriptor lookup, cut out of src/appletb.c, and its unit test.
$(BUILD_DIR)/appletb_hid_lookup.h: $(SRC)/appletb.c
	mkdir -p $(BUILD_DIR)
	/usr/bin/sed -n '/appletb hid lookup: begin/,/appletb hid lookup: end/p' \
	    $(SRC)/appletb.c > $@
	test -s $@

$(BUILD_DIR)/appletb_hid_test: files/appletb_hid_test.c \
    $(BUILD_DIR)/appletb_hid_lookup.h
	$(CC) -m64 -O2 -std=gnu99 -Wall -Wextra -Werror -Wno-sign-compare \
	    -I$(BUILD_DIR) -o $@ files/appletb_hid_test.c

KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/usba
check: $(BUILD_DIR)/appletb $(BUILD_DIR)/appletb_hid_test
	$(BUILD_DIR)/appletb_hid_test
	/usr/bin/nm -u -h $(BUILD_DIR)/appletb | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of appletb are in unix, genunix or misc/usba"
	/usr/bin/elfdump -d $(BUILD_DIR)/appletb | grep NEEDED

publish: check
	rm -rf $(PROTO)
	mkdir -p $(PROTO)
	cp $(BUILD_DIR)/appletb $(COMPONENT_LICENSE_FILE) $(PROTO)/
	/usr/bin/pkgmogrify -D PKG_VERSION=$(PKG_VERSION) \
	    -D OSNET_VERSION=$(OSNET_VERSION) -D ALS_IF=$(ALS_IF) \
	    -D COMPONENT_LICENSE="$(COMPONENT_LICENSE)" \
	    $(COMPONENT_NAME).p5m > $(BUILD_DIR)/manifest
	/usr/bin/pkglint $(BUILD_DIR)/manifest
	/usr/bin/pkgsend -s $(REPO) publish -d $(PROTO) \
	    --fmri-in-manifest $(BUILD_DIR)/manifest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check publish clean
