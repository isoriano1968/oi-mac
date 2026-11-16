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

# atopcase: driver for the internal keyboard of the 2015-2017 MacBooks and
# MacBook Pros (Apple "topcase" on the Intel LPSS SPI controller, ACPI
# APP000D), first for the MacBook Pro 15" 2017 (MacBookPro14,3).  See
# notes/macbookpro-2017-drivers.md.
#
# Source: src/ (our port of FreeBSD's atopcase(4) and intelspi(4), BSD-2,
# plus a kbtrans keyboard stream modelled on illumos kb8042/usbkbm, CDDL).
# The FreeBSD originals it follows: sys/dev/atopcase/{atopcase.c,
# atopcase_acpi.c,atopcase_reg.h} and sys/dev/intel/{spi.c,lpss.h}
# (freebsd-src main, 2026-11).
#
# illumos does not install kbtrans.h, pci_cap.h, acpica.h or the ACPICA
# headers, so they are taken from the pristine illumos-gate snapshot of
# the kernel the module is built for: c893fd03ad = osnet 23344 = OpenIndiana 2026.10, the
# kernel of the text-install medium (notes/oi-apple-installer-eval.md 1.5)
# and of the OI VM.
#
# Run on OpenIndiana with GNU make:
#   gmake            extract headers, build ./build/atopcase
#   gmake check      every undefined symbol must be in the kernel
#   gmake publish    package driver/input/atopcase into $(REPO)
#   gmake clean      remove ./build

COMPONENT_NAME=		atopcase
GATE_COMMIT=		c893fd03ad8b8be59f62b9f3a1573835ff5b1b67
GATE_SHORT=		c893fd03ad
OSNET_VERSION=		0.5.11-2026.0.0.23344
COMPONENT_SRC=		illumos-gate-$(GATE_COMMIT)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
	sha256:50ba97fa9529388bdafc9a1409835c5ba01b0b8002e42db6f05346adaa5e49f6
COMPONENT_ARCHIVE_URL=	\
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz
COMPONENT_LICENSE=	BSD-2-Clause AND CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

ARCHIVE_DIR?=		$(HOME)/src/oi-userland/archives
BUILD_DIR=		$(CURDIR)/build
HDR_DIR=		$(BUILD_DIR)/$(COMPONENT_SRC)/usr/src/uts
SRC=			$(CURDIR)/src

# As ports/applesmc: 0.5.11 like the osnet drivers, branch part = year,
# our revision; built against (and incorporated with) osnet 23344.
PKG_VERSION=		0.5.11-2026.0.0.2
REPO?=			$(HOME)/oi-repo
PROTO=			$(BUILD_DIR)/proto

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar

OBJS=		atopcase.o atopcase_spi.o atopcase_proto.o atopcase_kbd.o \
		atopcase_tpad.o
LINKMOD=	atopcase_link.so

# Makefile.master + uts/Makefile.uts + uts/intel/Makefile.intel, as cw
# hands them to gcc for an amd64 kernel module (as in ports/bge-57766).
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
		-D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 \
		-I$(SRC) -I$(HDR_DIR)/intel -I$(HDR_DIR)/common
CWARN=		-Wall -Werror -Wno-missing-braces -Wno-sign-compare \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-empty-body -Wno-parentheses

all: $(BUILD_DIR)/atopcase $(BUILD_DIR)/$(LINKMOD)

$(ARCHIVE_DIR)/$(COMPONENT_ARCHIVE):
	mkdir -p $(ARCHIVE_DIR)
	/usr/bin/curl -fL -o $@.part $(COMPONENT_ARCHIVE_URL)
	mv $@.part $@

# Only the headers the system does not install
$(BUILD_DIR)/.headers: $(ARCHIVE_DIR)/$(COMPONENT_ARCHIVE)
	test "sha256:`/usr/bin/digest -a sha256 $<`" = \
	    "$(COMPONENT_ARCHIVE_HASH)" || \
	    { echo "$<: wrong sha256" >&2; exit 1; }
	mkdir -p $(BUILD_DIR)
	$(GTAR) -xzf $< -C $(BUILD_DIR) \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/kbtrans.h \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/pci_cap.h \
	    $(COMPONENT_SRC)/usr/src/uts/intel/sys/acpica.h \
	    $(COMPONENT_SRC)/usr/src/uts/intel/sys/acpi
	touch $@

$(BUILD_DIR)/%.o: $(SRC)/%.c $(SRC)/atopcase.h $(BUILD_DIR)/.headers
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $<

$(BUILD_DIR)/atopcase: $(OBJS:%=$(BUILD_DIR)/%)
	$(LD) -r -dy -N misc/kbtrans -N misc/acpica -o $@.tmp \
	    $(OBJS:%=$(BUILD_DIR)/%)
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# devfsadm link module for /dev/atopcase/{keyboard,mouse}: 32-bit, as
# devfsadm and its modules (as ports/applesmc).
$(BUILD_DIR)/$(LINKMOD): files/atopcase_link.c
	mkdir -p $(BUILD_DIR)
	$(CC) -m32 -O2 -std=gnu17 -Wall -Wextra -Werror -fPIC -shared \
	    -Wl,-h$(LINKMOD) -o $@ $< -ldevinfo -lc

# Every symbol the module leaves undefined must be defined by the running
# kernel (unix, genunix) or by its -N dependencies.
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/kbtrans /kernel/misc/amd64/acpica
# The trackpad HID parser, cut out of src/atopcase_tpad.c, and its userland
# unit test (files/atopcase_tpad_test.c).
$(BUILD_DIR)/atopcase_tpad_parser.h: $(SRC)/atopcase_tpad.c
	mkdir -p $(BUILD_DIR)
	/usr/bin/sed -n '/atopcase tpad parser: begin/,/atopcase tpad parser: end/p' \
	    $(SRC)/atopcase_tpad.c > $@
	test -s $@

$(BUILD_DIR)/atopcase_tpad_test: files/atopcase_tpad_test.c \
    $(BUILD_DIR)/atopcase_tpad_parser.h
	$(CC) -m64 -O2 -std=gnu99 -Wall -Wextra -Werror -Wno-sign-compare \
	    -I$(BUILD_DIR) -o $@ files/atopcase_tpad_test.c

check: $(BUILD_DIR)/atopcase $(BUILD_DIR)/atopcase_tpad_test
	$(BUILD_DIR)/atopcase_tpad_test
	/usr/bin/nm -u -h $(BUILD_DIR)/atopcase | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of atopcase are in unix, genunix, misc/kbtrans or misc/acpica"
	/usr/bin/elfdump -d $(BUILD_DIR)/atopcase | grep NEEDED

# The package: module, driver.conf, driver action with the PCI aliases.
publish: check
	rm -rf $(PROTO)
	mkdir -p $(PROTO)
	cp $(BUILD_DIR)/atopcase $(BUILD_DIR)/$(LINKMOD) \
	    $(COMPONENT_NAME).conf $(COMPONENT_LICENSE_FILE) \
	    files/60-atopcase-trackpad.conf $(PROTO)/
	/usr/bin/pkgmogrify -D PKG_VERSION=$(PKG_VERSION) \
	    -D OSNET_VERSION=$(OSNET_VERSION) -D GATE_COMMIT=$(GATE_COMMIT) \
	    -D GATE_SHORT=$(GATE_SHORT) \
	    -D COMPONENT_LICENSE="$(COMPONENT_LICENSE)" \
	    $(COMPONENT_NAME).p5m > $(BUILD_DIR)/manifest
	/usr/bin/pkglint $(BUILD_DIR)/manifest
	/usr/bin/pkgsend -s $(REPO) publish -d $(PROTO) \
	    --fmri-in-manifest $(BUILD_DIR)/manifest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check publish clean
