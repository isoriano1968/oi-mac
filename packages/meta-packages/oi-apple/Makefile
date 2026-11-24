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
# metapackages/amix-cross-dev -- everything to start developing for Amiga
# UNIX (AMIX) on OpenIndiana: the AMIX sysroot and both cross toolchains,

#
# Copyright 2026 isoriano1968
#

# The OI-Apple package set (notes/oi-apple-installer-eval.md, 4.1):
# metapackages/oi-apple (the common set and the updater) and one group
# package per area, group/apple/<area>, each only depend actions, so the
# updater (system/oi-apple-updater) can add the ones a Mac needs
# (models.conf) and they can be removed one at a time.  All on oi-local.
BUILD_STYLE =		pkg
include ../../../make-rules/shared-macros.mk

COMPONENT_NAME =	oi-apple
COMPONENT_VERSION =	1.0
COMPONENT_SUMMARY =	OpenIndiana on Intel Macs: the OI-Apple package set
COMPONENT_CLASSIFICATION = Meta Packages/Group Packages
COMPONENT_FMRI =	metapackages/oi-apple
COMPONENT_PROJECT_URL =	https://www.openindiana.org/

include $(WS_MAKE_RULES)/common.mk
