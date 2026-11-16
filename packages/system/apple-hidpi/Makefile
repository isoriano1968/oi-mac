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

# HiDPI defaults for Intel Macs with Retina panels (MacBook Pro 2012-2017,
# iMac 5K): the Spleen 32x64 console font through a /boot/conf.d loader
# snippet, and GNOME at scale 2 (also for GDM) through a GSettings schema
# override.  Installed per model by the OI-Apple updater (models.conf), not
# on normal-DPI Macs.

include ../../../make-rules/shared-macros.mk

COMPONENT_NAME=		apple-hidpi
COMPONENT_VERSION=	1.0
COMPONENT_SUMMARY=	HiDPI defaults for Retina Macs: console font, GNOME scale 2
COMPONENT_FMRI=		system/apple/hidpi
COMPONENT_CLASSIFICATION=	System/Hardware
COMPONENT_LICENSE=	CDDL
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

include $(WS_MAKE_RULES)/prep-download.mk
include $(WS_MAKE_RULES)/justmake.mk
include $(WS_MAKE_RULES)/ips.mk

PATH=$(PATH.gnu)

# No upstream source: the package is the two files in files/.
download::
$(BUILD_DIR)/.prep: Makefile $(COMPONENT_NAME).p5m
	$(MKDIR) -p "$(@D)"
	$(TOUCH) $@
prep::	$(BUILD_DIR)/.prep
clean::
	$(RM) -r $(BUILD_DIR)
clobber:: clean
build:: $(BUILD_DIR)/.prep
install: $(BUILD_DIR)/.prep
test: $(NO_TESTS)

REQUIRED_PACKAGES += system/font/console/spleen
REQUIRED_PACKAGES += gnome/config/gsettings-desktop-schemas
