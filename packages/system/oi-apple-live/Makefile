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

#
# Copyright 2026 isoriano1968
#

# system/oi-apple-live: what the OI-Apple media (text and GNOME live) need
# besides packages of their own: X on the console keyboard/mouse (atopcase
# on the MacBooks), and svc:/system/oi-apple-live, which turns the
# live-only pieces (X on the firmware framebuffer, the "Install
# OpenIndiana" launcher) on in the live system and off on the installed
# one, and makes the first-boot fixes there.  Replaces the hand copies of
# notes/macbookpro-desktop-stick (macdesk_image_mod).
BUILD_BITS=		NO_ARCH
BUILD_STYLE=		pkg
include ../../../make-rules/shared-macros.mk

COMPONENT_NAME=		oi-apple-live
COMPONENT_VERSION=	1.0
# 1: rebuilt without the real name in the delivered files
# 2: live /var/pkg on a tmpfs (pkg refresh filled the live root)
COMPONENT_REVISION=	2
COMPONENT_SUMMARY=	OI-Apple live media setup and first-boot fixes
COMPONENT_PROJECT_URL=	https://www.openindiana.org/
COMPONENT_FMRI=		system/oi-apple-live
COMPONENT_CLASSIFICATION=	System/Administration and Configuration
COMPONENT_LICENSE=	CDDL
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

include $(WS_MAKE_RULES)/common.mk

F=	$(COMPONENT_DIR)/files
COMPONENT_POST_INSTALL_ACTION += \
	$(INSTALL) -D -m 0444 $(F)/20-oi-apple-input.conf \
	    $(PROTO_DIR)/etc/X11/xorg.conf.d/20-oi-apple-input.conf ; \
	$(INSTALL) -D -m 0444 $(F)/10-oi-apple-live-fb.conf \
	    $(PROTO_DIR)/usr/share/oi-apple/live/10-oi-apple-live-fb.conf ; \
	$(INSTALL) -D -m 0555 $(F)/oi-apple-install \
	    $(PROTO_DIR)/usr/bin/oi-apple-install ; \
	$(INSTALL) -D -m 0444 $(F)/oi-apple-install.desktop \
	    $(PROTO_DIR)/usr/share/applications/oi-apple-install.desktop ; \
	$(INSTALL) -D -m 0555 $(F)/svc-oi-apple-live \
	    $(PROTO_DIR)/lib/svc/method/svc-oi-apple-live ; \
	$(INSTALL) -D -m 0444 $(F)/oi-apple-live.xml \
	    $(PROTO_DIR)/lib/svc/manifest/system/oi-apple-live.xml ;

# Auto-generated dependencies
REQUIRED_PACKAGES += SUNWcs
REQUIRED_PACKAGES += shell/ksh93
