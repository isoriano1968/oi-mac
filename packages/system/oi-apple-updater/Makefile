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

# The OI-Apple Updater (notes/oi-apple-installer-eval.md, 4.3):
# oi-apple-update installs metapackages/oi-apple and the group packages a
# Mac needs (models.conf) from oi-local into a new boot environment.
BUILD_BITS=		NO_ARCH
BUILD_STYLE=		pkg
include ../../../make-rules/shared-macros.mk

COMPONENT_NAME=		oi-apple-updater
COMPONENT_VERSION=	1.0
# rebuilt without the real name in the delivered files
COMPONENT_REVISION=	1
COMPONENT_SUMMARY=	OI-Apple Updater: the Apple packages each Intel Mac needs
COMPONENT_PROJECT_URL=	https://www.openindiana.org/
COMPONENT_FMRI=		system/apple/oi-apple-updater
COMPONENT_CLASSIFICATION=	System/Administration and Configuration
COMPONENT_LICENSE=	CDDL
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license

include $(WS_MAKE_RULES)/common.mk

F=	$(COMPONENT_DIR)/files
COMPONENT_POST_INSTALL_ACTION += \
	$(INSTALL) -D -m 0555 $(F)/oi-apple-update \
	    $(PROTO_DIR)/usr/sbin/oi-apple-update ; \
	$(INSTALL) -D -m 0444 $(F)/models.conf \
	    $(PROTO_DIR)/usr/share/oi-apple/models.conf ; \
	$(INSTALL) -D -m 0444 $(F)/unlock.list \
	    $(PROTO_DIR)/usr/share/oi-apple/unlock.list ; \
	$(INSTALL) -D -m 0444 $(F)/oi-apple-update.8 \
	    $(PROTO_DIR)/usr/share/man/man8/oi-apple-update.8 ;
# Auto-generated dependencies
REQUIRED_PACKAGES += shell/ksh93
