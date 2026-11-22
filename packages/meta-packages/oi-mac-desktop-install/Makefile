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

# group/oi-mac/desktop_install: what the OI Mac live media and the systems
# installed from them need, as one group package (as OI's mate_install).
# The media install entire, the incorporations and this group, not a
# hand-made package list; an installed system keeps the group, so
# "pkg update" keeps it complete.  The versions come from the
# incorporations (osnet, userland with the OI Mac locks, install).
BUILD_STYLE =		pkg

include ../../../make-rules/shared-macros.mk

COMPONENT_NAME =	oi-mac-desktop-install
COMPONENT_VERSION =	1.0
COMPONENT_REVISION =	1
COMPONENT_SUMMARY =	OI Mac: the live desktop package set
COMPONENT_CLASSIFICATION = Meta Packages/Group Packages
COMPONENT_FMRI =	group/oi-mac/desktop_install
COMPONENT_PROJECT_URL =	https://www.openindiana.org/

include $(WS_MAKE_RULES)/common.mk

PKG_OPTIONS +=		-I $(COMPONENT_DIR)/includes
