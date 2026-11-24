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

# usbvc-v4l2: illumos' usbvc(4D) USB webcam driver with what V4L2
# applications such as Chromium (WebRTC) and GStreamer need
# (notes/usbvc-v4l2-webcam.md; illumos-gate branch usbvc-v4l2):
#
#   0001 VIDIOC_ENUM_FRAMESIZES/ENUM_FRAMEINTERVALS, poll(2), UVC frame
#        based formats, device_caps; sys/videodev2.h with Linux' structs
#   0002 sys/videodev2.h: v4l2_ext_control(s) as in Linux
#   0003 isoc requests of at most 64k (xhci: one DMA cookie; 720p failed),
#        buffer sequence numbers, timestamps, V4L2_BUF_FLAG_ERROR
#   0004 struct v4l2_buffer in the Linux layout; the old illumos layout's
#        ioctls are still accepted
#   0005 struct v4l2_input in the Linux layout (GStreamer looped over
#        VIDIOC_ENUMINPUT), no empty frames (read(2) returned 0), the UVC
#        header error bit
#   0006 VIDIOC_TRY_FMT (GStreamer's caps negotiation), an empty end of
#        frame no longer costs the next frame
#   0007 VIDIOC_CROPCAP (GStreamer read its errno after ENUM_FMT), the
#        format committed at every stream start (read(2) got no data)
#
# Pristine source: the illumos-gate snapshot of the OS build
# (pkg contents -m driver/graphics/usbvc: illumos-gate.info.git-rev).  Only
# the usbvc sources, its headers, sys/videodev2.h, usbai_private.h and the
# man page are extracted.  Built out of tree with the flags of usr/src/uts/Makefile.uts +
# uts/intel/Makefile.intel, linked as uts/intel/usbvc/Makefile
# (ld -r -dy -N misc/usba), against the patched extracted headers first and
# the installed system headers for the rest.  Same method as
# ports/audiohd-apple.
#
# The package carries the module and the man page, not sys/videodev2.h:
# that file belongs to system/header, and IPS does not install two packages
# that deliver one path with different contents.  The driver accepts both
# struct v4l2_buffer layouts, so programs built against the installed
# header keep working; GStreamer and Chromium use their own copies of the
# Linux header.  The new header goes into system/header with
# ports/system-header-int8's method when it is needed on a system.
#
#   gmake / gmake check / gmake publish / gmake clean  (GNU make, on OI)

COMPONENT_NAME=		usbvc-v4l2
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
UTS_DIR=		$(SRC_DIR)/usr/src/uts/common
DRV_DIR=		$(UTS_DIR)/io/usb/clients/video/usbvc
MODULE=			usbvc

CC=			/usr/gcc/10/bin/gcc
LD=			/usr/bin/ld
CTFCONVERT=		/opt/onbld/bin/i386/ctfconvert
GTAR=			/usr/bin/gtar
GPATCH=			/usr/bin/gpatch

OBJS=		usbvc.o usbvc_v4l2.o

# As ports/audiohd-apple (Makefile.master + uts/Makefile.uts + Makefile.intel).
CFLAGS=		-m64 -Ui386 -U__i386 -O2 -std=gnu99
# no build host paths in the objects (__FILE__, debug info): the port
# maps to /usr/src/ports/usbvc-v4l2, the rest of $(HOME) to /build
CFLAGS+=	-ffile-prefix-map=$(HOME)=/build
CFLAGS+=	-ffile-prefix-map=$(CURDIR)=/usr/src/ports/usbvc-v4l2
CFLAGS+=	-Wa,--debug-prefix-map,$(HOME)=/build
CFLAGS+=	-Wa,--debug-prefix-map,$(CURDIR)=/usr/src/ports/usbvc-v4l2
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
# The extracted tree holds only the patched sys/videodev2.h, the usbvc
# headers and the kernel-private <sys/usb/usba/usbai_private.h>, which
# system/header does not ship: they come first, the rest from /usr/include.
CPPFLAGS=	-D_ASM_INLINES -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		-D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -I$(UTS_DIR)
# CERRWARN of uts/Makefile.uts (release)
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
	    $(COMPONENT_SRC)/usr/src/uts/common/io/usb/clients/video/usbvc \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/usb/clients/video/usbvc \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/videodev2.h \
	    $(COMPONENT_SRC)/usr/src/uts/common/sys/usb/usba/usbai_private.h \
	    $(COMPONENT_SRC)/usr/src/man/man4d/usbvc.4d
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
	# relative -o: ld -r names its output in a FILE symbol (no build path)
	cd $(@D) && $(LD) -r -dy -N misc/usba -o $(@F).tmp $(OBJS:%=$(BUILD_DIR)/%)
	$(CTFCONVERT) -l $(COMPONENT_NAME) -o $@.tmp $@.tmp
	mv $@.tmp $@
	/usr/bin/digest -a sha256 $@

# Every symbol the module leaves undefined must be defined by the running
# kernel (unix, genunix) or by misc/usba (its -N dependency).
KERNEL_OBJS=	/platform/i86pc/kernel/amd64/unix /kernel/amd64/genunix \
		/kernel/misc/amd64/usba
check: $(BUILD_DIR)/$(MODULE)
	/usr/bin/nm -u -h $(BUILD_DIR)/$(MODULE) | awk '{print $$NF}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/undef.txt
	for k in $(KERNEL_OBJS); do /usr/bin/nm -h -g $$k; done | \
	    awk -F'|' '$$7 !~ /UNDEF/ {gsub(/ /,"",$$8); print $$8}' | \
	    LC_ALL=C sort -u > $(BUILD_DIR)/kdefs.txt
	@missing=`LC_ALL=C comm -23 $(BUILD_DIR)/undef.txt $(BUILD_DIR)/kdefs.txt`; \
	if [ -n "$$missing" ]; then echo "undefined: $$missing"; exit 1; fi; \
	echo "check: all `wc -l < $(BUILD_DIR)/undef.txt` undefined symbols \
	    of $(MODULE) are in unix, genunix or misc/usba"
	/usr/bin/elfdump -d $(BUILD_DIR)/$(MODULE) | grep NEEDED

# The IPS package: our driver/graphics/usbvc, same name, files and driver
# action as OpenIndiana's, at $(OSNET_VERSION).1 in our publisher
# (usbvc-v4l2.p5m).  OSNET_VERSION is the installed OS build, whose
# illumos-gate commit must be GATE_COMMIT.  For a new OS build: set both,
# the archive hash, gmake clean publish.
OSNET_VERSION=	0.5.11-2026.0.0.23395
# .1 patches 0001-0004 (720p and sequence numbers tested on the MacBook Pro
# 14,3 with a C920; published to oi-local 2026-11-18)
# .2 + 0005-0007 (VIDIOC_ENUMINPUT, TRY_FMT, CROPCAP for GStreamer, no empty
# frames, commit at stream start; published to oi-local 2026-11-18;
# read(2) still returns 0 bytes)
# .3 + 0008 (frame intervals as simple fractions, 1/30 instead of
# 333333/10000000, so GStreamer caps with 30/1 negotiate; DQBUF clears DONE;
# published to oi-local 2026-11-19)
# .4 + 0009 (camera terminal controls: auto exposure, exposure time, exposure
# dynamic framerate (AE priority), focus; more processing unit controls;
# V4L2_CTRL_FLAG_NEXT_CTRL, VIDIOC_QUERYMENU; published to oi-local
# 2026-11-19; found no control on the C920, see 0010)
# .5 + 0010 (the VC descriptor length checks measured the VC header, so the
# camera terminal and processing unit were never parsed and no control
# worked, brightness included; published to oi-local 2026-11-19)
# .6 + 0011 (several opens, as V4L2: Chromium enumerates during a call;
# lost isoc packets mark the frame broken; a FID toggle without end of
# frame completes the frame, as uvcvideo; published to oi-local 2026-11-20)
PKG_VERSION=	$(OSNET_VERSION).7
GATE_SHORT=	$(shell echo $(GATE_COMMIT) | cut -c1-10)
REPO?=		$(HOME)/oi-local-repo
# the publish gate (tools/oi-manifest-gate.sh): new version, no
# build-host paths or names
GATE?=		$(HOME)/src/oi-userland/tools/oi-manifest-gate
PROTO=		$(BUILD_DIR)/proto

publish: check
	rm -rf $(PROTO)
	mkdir -p $(PROTO)
	cp $(BUILD_DIR)/$(MODULE) $(PROTO)/usbvc
	cp $(SRC_DIR)/usr/src/man/man4d/usbvc.4d $(PROTO)/usbvc.4d
	cp $(COMPONENT_LICENSE_FILE) $(PROTO)/
	/usr/bin/pkgmogrify -D PKG_VERSION=$(PKG_VERSION) \
	    -D OSNET_VERSION=$(OSNET_VERSION) -D GATE_COMMIT=$(GATE_COMMIT) \
	    -D GATE_SHORT=$(GATE_SHORT) $(COMPONENT_NAME).p5m > $(BUILD_DIR)/manifest
	/usr/bin/pkglint $(BUILD_DIR)/manifest
	$(GATE) $(REPO) $(BUILD_DIR)/manifest $(PROTO)
	/usr/bin/pkgsend -s $(REPO) publish -d $(PROTO) \
	    --fmri-in-manifest $(BUILD_DIR)/manifest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean publish
