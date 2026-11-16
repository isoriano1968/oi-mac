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

# Spleen as console fonts for the illumos loader and the kernel console
# (tem): the pristine BDFs, converted with vtfontcvt(1) into
# /boot/fonts/spleen-WxH.fnt.
#
# The system's vtfontcvt only handles glyphs up to 16 pixels wide
# ("Unsupported wbytes 4"); the VFNT format, the loader and tem have no such
# limit.  So the build makes its own vtfontcvt from the pristine
# illumos-gate source of the OS (archive _1; only download, no unpack: the
# three files it needs are extracted) with files/0001-vtfontcvt-glyphs-up-
# to-32-pixels-wide.patch, which is meant for illumos upstream.  For 8 and 16
# wide fonts its output is byte-identical to the stock tool's.  32x64 is for HiDPI panels (MacBook Pro
# Retina, 2880x1800), where the largest stock font (16x32) is tiny.  The
# package only adds the fonts; selecting one is system/apple/hidpi's job
# (a /boot/conf.d snippet with loadfont).

BUILD_BITS=		NO_ARCH
BUILD_STYLE=		justmake

include ../../../make-rules/shared-macros.mk

COMPONENT_NAME=		spleen
COMPONENT_VERSION=	2.2.0
COMPONENT_SRC=		$(COMPONENT_NAME)-$(COMPONENT_VERSION)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.gz
COMPONENT_SUMMARY=	Spleen bitmap font as console fonts (12x24, 16x32, 32x64)
COMPONENT_PROJECT_URL=	https://github.com/fcambus/spleen
COMPONENT_ARCHIVE_URL=	\
	$(COMPONENT_PROJECT_URL)/releases/download/$(COMPONENT_VERSION)/$(COMPONENT_ARCHIVE)
COMPONENT_ARCHIVE_HASH=	\
	sha256:ec42925c6b56d2138c862b2f97147c872e472f674bf03423417d827a08d69a89
COMPONENT_FMRI=		system/font/console/spleen
COMPONENT_CLASSIFICATION=	System/Fonts
COMPONENT_LICENSE=	BSD-2-Clause
COMPONENT_LICENSE_FILE=	spleen.license

# illumos-gate of the OS build (osnet 23395), for vtfontcvt only; not
# unpacked (no COMPONENT_SRC_1), the files are taken in the build action.
GATE_COMMIT=		586678ac1bcbd77644a6ec4dac7f79d676cfb4ec
COMPONENT_ARCHIVE_1=	illumos-gate-$(GATE_COMMIT).tar.gz
COMPONENT_ARCHIVE_HASH_1= \
	sha256:ad278ac9dcb58e92e0a441a5a302c25692ca4c1ee435d7464b79858c04e40acf
COMPONENT_ARCHIVE_URL_1= \
	https://github.com/illumos/illumos-gate/archive/$(GATE_COMMIT).tar.gz

TEST_TARGET=		$(NO_TESTS)

include $(WS_MAKE_RULES)/common.mk

CONSOLE_FONTS=		12x24 16x32 32x64
GATE_SRC=		illumos-gate-$(GATE_COMMIT)/usr/src
VTFONTCVT=		$(@D)/vtfontcvt-tool/vtfontcvt

# The build's own vtfontcvt: cmd/vtfontcvt and common/lz4 of the gate, with
# our patch, compiled as cmd/vtfontcvt/Makefile.common does (gnu99, lz4.c).
# Its default output is the uncompressed VFNT format of the stock
# /boot/fonts/*.fnt.
COMPONENT_BUILD_ACTION= \
	$(RM) -r $(@D)/vtfontcvt-tool; $(MKDIR) $(@D)/vtfontcvt-tool; \
	$(GNUBIN)/tar -xzf $(USERLAND_ARCHIVES)$(COMPONENT_ARCHIVE_1) \
	    -C $(@D)/vtfontcvt-tool --strip-components=1 \
	    $(GATE_SRC)/cmd/vtfontcvt/vtfontcvt.c \
	    $(GATE_SRC)/cmd/vtfontcvt/fnv_hash.h \
	    $(GATE_SRC)/common/lz4 || exit 1; \
	$(GPATCH) -d $(@D)/vtfontcvt-tool -p1 -N --no-backup-if-mismatch \
	    < $(COMPONENT_DIR)/files/0001-vtfontcvt-glyphs-up-to-32-pixels-wide.patch \
	    || exit 1; \
	$(CC) -std=gnu99 -O2 \
	    -I$(@D)/vtfontcvt-tool/usr/src/common/lz4 \
	    -I$(@D)/vtfontcvt-tool/usr/src/cmd/vtfontcvt \
	    -o $(VTFONTCVT) \
	    $(@D)/vtfontcvt-tool/usr/src/cmd/vtfontcvt/vtfontcvt.c \
	    $(@D)/vtfontcvt-tool/usr/src/common/lz4/lz4.c -lumem || exit 1; \
	for s in $(CONSOLE_FONTS); do \
		$(VTFONTCVT) -v -o $(@D)/spleen-$$s.fnt \
		    $(SOURCE_DIR)/spleen-$$s.bdf || exit 1; \
	done

COMPONENT_INSTALL_ACTION= \
	$(MKDIR) $(PROTO_DIR)/boot/fonts; \
	for s in $(CONSOLE_FONTS); do \
		$(INSTALL) -m 0444 $(@D)/spleen-$$s.fnt \
		    $(PROTO_DIR)/boot/fonts/spleen-$$s.fnt || exit 1; \
	done
