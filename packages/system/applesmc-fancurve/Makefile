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
# Copyright 2026 isoriano1968
#

#
# system/applesmc-fancurve -- a temperature curve for the fans of an Intel
# Mac (applesmc-fancurve and svc:/system/applesmc-fancurve, enabled), on top
# of smcfan(8) from driver/sensor/applesmc.  The files are in files/; nothing
# is built.
#
BUILD_BITS =		NO_ARCH
BUILD_STYLE =		pkg

include ../../../make-rules/shared-macros.mk

COMPONENT_NAME =	applesmc-fancurve
COMPONENT_VERSION =	1.1
COMPONENT_SUMMARY =	Fan temperature curve for Intel Macs (Apple SMC)
COMPONENT_PROJECT_URL =	https://github.com/isoriano1968
COMPONENT_FMRI =	system/applesmc-fancurve
COMPONENT_CLASSIFICATION = System/Hardware
COMPONENT_LICENSE =	CDDL
COMPONENT_LICENSE_FILE = applesmc-fancurve.license

include $(WS_MAKE_RULES)/common.mk


# driver/sensor/applesmc (smcfan) is a manual dependency in the manifest:
# it is not on the build machine
REQUIRED_PACKAGES += SUNWcs
REQUIRED_PACKAGES += shell/bash
