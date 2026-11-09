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

# bge-57766: illumos' bge driver with three BCM57766 changes (patches/),
# for the wired Ethernet of the iMac 27" 2019 (iMac19,1).  See
# notes/imac2019-network.md.
#
# Pristine source: the illumos-gate snapshot the OpenIndiana VM's kernel
# was built from (uname -v: illumos-c893fd03ad); bge is identical at
# illumos-gate master (b1c424a261, 2026-11-08).  Only the bge directory
# is extracted.  The module is built out of tree against the installed
# system headers (system/header), with the flags of usr/src/uts/Makefile.uts
# and uts/intel/Makefile.intel (cw's -xmodel=kernel, -Wu,-save_args, ...),
# and linked like uts/intel/bge/Makefile (ld -r -dy -N misc/mac).
#
# No IPS package: the module is /kernel/drv/amd64/bge, which
# driver/network/bge delivers already, and IPS refuses two packages with
# one file.  The patches are meant for illumos-gate (upstream, then OI's
# driver/network/bge); until then the module is tested in a new boot
# environment (see the note, "Hardware test plan").
#
# Run on OpenIndiana with GNU make:
#   gmake            download, verify, extract, patch, build ./build/bge
#   gmake check      every undefined symbol of bge must be in the kernel
#   gmake clean      remove ./build (the archive stays in ARCHIVE_DIR)

COMPONENT_NAME=		bge-57766
GATE_COMMIT=		c893fd03ad8b8be59f62b9f3a1573835ff5b1b67
COMPONENT_SRC=		illumos-gate-$(GATE_COMMIT)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
	sha256:50ba97fa9529388bdafc9a1409835c5ba01b0b8002e42db6f05346adaa5e49f6
COMPONENT_ARCHIVE_URL=	\
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz
COMPONENT_LICENSE=	CDDL-1.0 AND BSD-4-Clause
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

ARCHIVE_DIR?=		$(HOME)/src/oi-userland/archives
BUILD_DIR=		$(CURDIR)/build
SRC_DIR=		$(BUILD_DIR)/$(COMPONENT_SRC)
BGE_DIR=		$(SRC_DIR)/usr/src/uts/common/io/bge

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar
GPATCH=			/usr/bin/gpatch

# usr/src/uts/common/Makefile.files: BGE_OBJS
BGE_OBJS=	bge_main2.o bge_chip2.o bge_kstats.o bge_log.o bge_ndd.o \
		bge_atomic.o bge_mii.o bge_send.o bge_recv2.o bge_mii_5906.o

# Makefile.master + uts/Makefile.uts + uts/intel/Makefile.intel, as cw
# hands them to gcc for an amd64 kernel module.
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
		-D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -I$(BGE_DIR)
# CERRWARN of uts/Makefile.uts (release build: -Wno-unused, -Wno-empty-body)
# and uts/intel/bge/Makefile
CWARN=		-Wall -Werror -Winline -Wno-missing-braces -Wno-sign-compare \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-unused -Wno-empty-body -Wno-maybe-uninitialized \
		-Wno-switch -Wno-parentheses

all: $(BUILD_DIR)/bge

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
	$(GTAR) -xzf $< -C $(BUILD_DIR) $(COMPONENT_SRC)/usr/src/uts/common/io/bge
	touch $@

$(BUILD_DIR)/.patched: $(BUILD_DIR)/.unpacked $(wildcard patches/*.patch)
	for p in patches/*.patch; do \
		echo "== $$p"; \
		$(GPATCH) -d $(SRC_DIR) -p1 -N --no-backup-if-mismatch < $$p || exit 1; \
	done
	touch $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/.patched
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CWARN) -c -o $@ $(BGE_DIR)/$*.c

$(BUILD_DIR)/bge: $(BGE_OBJS:%=$(BUILD_DIR)/%)
	$(LD) -r -dy -N misc/mac -o $@.tmp $(BGE_OBJS:%=$(BUILD_DIR)/%)
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# Every symbol bge leaves undefined must be defined by the running kernel
# (unix, genunix) or by misc/mac (its -N dependency).
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/mac
check: $(BUILD_DIR)/bge
	/usr/bin/nm -u -h $(BUILD_DIR)/bge | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of bge are in unix, genunix or misc/mac"
	/usr/bin/elfdump -d $(BUILD_DIR)/bge | grep NEEDED

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean
