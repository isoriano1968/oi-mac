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

# applegmux: the gmux of the dual-GPU Apple machines (ACPI APP000B): the
# panel backlight of the MacBook Pro 15" 2017 (MacBookPro14,3) and the other
# dual-GPU MacBook Pros, and the mux state, read only.  Our own driver
# (CDDL); the register map and protocols are those Linux's apple-gmux
# documents.  See notes/macbookpro-2017-drivers.md.
#
# Source: src/ (driver, ioctl header), files/ (gmuxctl, devfsadm link
# module, the protocol unit test).
#
# illumos does not install acpica.h or the ACPICA headers, so they are
# taken from the pristine illumos-gate snapshot of the kernel the module is
# built for: c893fd03ad = osnet 23344 = OpenIndiana 2026.10 (as
# ports/atopcase).
#
# Run on OpenIndiana with GNU make:
#   gmake            extract headers, build ./build/applegmux and gmuxctl
#   gmake check      unit test; every undefined symbol must be in the kernel
#   gmake publish    package driver/graphics/applegmux into $(REPO)
#   gmake clean      remove ./build

COMPONENT_NAME=		applegmux
GATE_COMMIT=		c893fd03ad8b8be59f62b9f3a1573835ff5b1b67
GATE_SHORT=		c893fd03ad
OSNET_VERSION=		0.5.11-2026.0.0.23344
COMPONENT_SRC=		illumos-gate-$(GATE_COMMIT)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
	sha256:50ba97fa9529388bdafc9a1409835c5ba01b0b8002e42db6f05346adaa5e49f6
COMPONENT_ARCHIVE_URL=	\
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz
COMPONENT_LICENSE=	CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

ARCHIVE_DIR?=		$(HOME)/src/oi-userland/archives
BUILD_DIR=		$(CURDIR)/build
HDR_DIR=		$(BUILD_DIR)/$(COMPONENT_SRC)/usr/src/uts
SRC=			$(CURDIR)/src

PKG_VERSION=		0.5.11-2026.0.0.1
REPO?=			$(HOME)/oi-repo
PROTO=			$(BUILD_DIR)/proto

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar

LINKMOD=	applegmux_link.so

# As ports/atopcase: the flags cw hands gcc for an amd64 kernel module.
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

all: $(BUILD_DIR)/applegmux $(BUILD_DIR)/$(LINKMOD) $(BUILD_DIR)/gmuxctl

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
	    $(COMPONENT_SRC)/usr/src/uts/intel/sys/acpica.h \
	    $(COMPONENT_SRC)/usr/src/uts/intel/sys/acpi
	touch $@

$(BUILD_DIR)/applegmux.o: $(SRC)/applegmux.c $(SRC)/applegmux.h \
    $(BUILD_DIR)/.headers
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $<

$(BUILD_DIR)/applegmux: $(BUILD_DIR)/applegmux.o
	$(LD) -r -dy -N misc/acpica -o $@.tmp $(BUILD_DIR)/applegmux.o
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# devfsadm link module for /dev/applegmux: 32-bit, as devfsadm.
$(BUILD_DIR)/$(LINKMOD): files/applegmux_link.c
	mkdir -p $(BUILD_DIR)
	$(CC) -m32 -O2 -std=gnu17 -Wall -Wextra -Werror -fPIC -shared \
	    -Wl,-h$(LINKMOD) -o $@ $< -ldevinfo -lc

$(BUILD_DIR)/gmuxctl: files/gmuxctl.c $(SRC)/applegmux.h
	mkdir -p $(BUILD_DIR)
	$(CC) -m64 -O2 -std=gnu99 -Wall -Wextra -Werror -I$(SRC) -o $@ \
	    files/gmuxctl.c

# The protocol code, cut out of src/applegmux.c, and its userland test
# against simulated gmuxes (files/applegmux_test.c).
$(BUILD_DIR)/applegmux_proto.h: $(SRC)/applegmux.c
	mkdir -p $(BUILD_DIR)
	/usr/bin/sed -n '/applegmux protocol: begin/,/applegmux protocol: end/p' \
	    $(SRC)/applegmux.c > $@
	test -s $@

$(BUILD_DIR)/applegmux_test: files/applegmux_test.c \
    $(BUILD_DIR)/applegmux_proto.h $(SRC)/applegmux.h
	$(CC) -m64 -O2 -std=gnu99 -Wall -Wextra -Werror -Wno-sign-compare \
	    -I$(SRC) -I$(BUILD_DIR) -o $@ files/applegmux_test.c

# Every symbol the module leaves undefined must be defined by the running
# kernel (unix, genunix) or by misc/acpica.
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/acpica

check: $(BUILD_DIR)/applegmux $(BUILD_DIR)/applegmux_test
	$(BUILD_DIR)/applegmux_test
	/usr/bin/nm -u -h $(BUILD_DIR)/applegmux | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of applegmux are in unix, genunix or misc/acpica"
	/usr/bin/elfdump -d $(BUILD_DIR)/applegmux | grep NEEDED

publish: all check
	rm -rf $(PROTO)
	mkdir -p $(PROTO)
	cp $(BUILD_DIR)/applegmux $(BUILD_DIR)/$(LINKMOD) $(BUILD_DIR)/gmuxctl \
	    $(COMPONENT_NAME).conf $(COMPONENT_LICENSE_FILE) \
	    $(SRC)/applegmux.h $(PROTO)/
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
