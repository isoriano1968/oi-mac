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

# audiohd-apple: illumos' audiohd driver with the speaker/headphone amplifier
# GPIOs of Apple's Cirrus CS4206 turned on (patches/), for the iMac13,2.
# See notes/imac2019-network.md ("Sound").
#
# Pristine source: the illumos-gate snapshot the iMac's kernel was built
# from (uname -v: illumos-08e59b676b).  Only the audiohd directory is
# extracted.  Built out of tree against the installed system headers, with
# the flags of usr/src/uts/Makefile.uts + uts/intel/Makefile.intel, linked
# as uts/intel/audiohd/Makefile (ld -r -dy -N drv/audio).  Same method as
# ports/bge-57766; same reason for having no IPS package (the file belongs
# to driver/audio/audiohd).  Tested in its own boot environment.
#
#   gmake / gmake check / gmake clean     (GNU make, on OpenIndiana)

COMPONENT_NAME=		audiohd-apple
GATE_COMMIT=		08e59b676b426617f8720d1f2fa14d9e2db2afa2
COMPONENT_SRC=		illumos-gate-$(GATE_COMMIT)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
	sha256:499966dc96a22b075874c6c6e33efcf9f7598fec22dffa1cfc3e4e5b1fbab516
COMPONENT_ARCHIVE_URL=	\
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz
COMPONENT_LICENSE=	CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

ARCHIVE_DIR?=		$(HOME)/src/oi-userland/archives
BUILD_DIR=		$(CURDIR)/build
SRC_DIR=		$(BUILD_DIR)/$(COMPONENT_SRC)
DRV_DIR=		$(SRC_DIR)/usr/src/uts/common/io/audio/drv/audiohd
MODULE=			audiohd

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar
GPATCH=			/usr/bin/gpatch

OBJS=		audiohd.o

# As ports/bge-57766 (Makefile.master + uts/Makefile.uts + Makefile.intel).
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
		-D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -I$(DRV_DIR)
# <sys/beep.h> is a kernel-private header that system/header does not ship:
# taken from the same source tree, searched after /usr/include.
CPPFLAGS+=	-idirafter $(SRC_DIR)/usr/src/uts/common
# CERRWARN of uts/Makefile.uts (release) and uts/intel/audiohd/Makefile
CWARN=		-Wall -Werror -Winline -Wno-missing-braces -Wno-sign-compare \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-unused -Wno-empty-body -Wno-maybe-uninitialized \
		-Wno-switch -Wno-parentheses

all: $(BUILD_DIR)/$(MODULE)

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
	$(GTAR) -xzf $< -C $(BUILD_DIR) \
	    $(COMPONENT_SRC)/usr/src/uts/common/io/audio/drv/audiohd \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/beep.h
	touch $@

$(BUILD_DIR)/.patched: $(BUILD_DIR)/.unpacked $(wildcard patches/*.patch)
	for p in patches/*.patch; do \
		echo "== $$p"; \
		$(GPATCH) -d $(SRC_DIR) -p1 -N --no-backup-if-mismatch < $$p || exit 1; \
	done
	touch $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/.patched
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $(DRV_DIR)/$*.c

$(BUILD_DIR)/$(MODULE): $(OBJS:%=$(BUILD_DIR)/%)
	$(LD) -r -dy -N drv/audio -o $@.tmp $(OBJS:%=$(BUILD_DIR)/%)
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# Every symbol the module leaves undefined must be defined by the running
# kernel (unix, genunix) or by drv/audio (its -N dependency).
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/drv/amd64/audio
check: $(BUILD_DIR)/$(MODULE)
	/usr/bin/nm -u -h $(BUILD_DIR)/$(MODULE) | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of $(MODULE) are in unix, genunix or drv/audio"
	/usr/bin/elfdump -d $(BUILD_DIR)/$(MODULE) | grep NEEDED

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean
