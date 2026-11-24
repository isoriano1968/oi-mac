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

# The AMD microcode amdgpu loads for Polaris GPUs (Polaris 10/11/12 and
# their 20/21 refreshes: Radeon RX 4x0/5x0, Radeon Pro 4x0/5x0 in the 2016-
# 2019 iMacs and MacBook Pros): unmodified files from the pristine
# linux-firmware release, under AMD's redistribution licence
# (LICENSES/LICENSE.amdgpu; WHENCE: "Redistributable").  The list is exactly
# what Linux v7.2's amdgpu requests for CHIP_POLARIS10/11/12
# (MODULE_FIRMWARE and the UVD/VCE firmware names).  Installed where the
# illumos-drm amdgpu module looks for its firmware, next to the Tahiti
# files: /kernel/firmware/amdgpu.  Nothing is built.

BUILD_BITS=		NO_ARCH
BUILD_STYLE=		justmake

include ../../../make-rules/shared-macros.mk

COMPONENT_NAME=		linux-firmware
COMPONENT_VERSION=	20261019
COMPONENT_SRC=		$(COMPONENT_NAME)-$(COMPONENT_VERSION)
COMPONENT_ARCHIVE=	$(COMPONENT_SRC).tar.xz
COMPONENT_SUMMARY=	AMD GPU microcode for Polaris (amdgpu)
COMPONENT_PROJECT_URL=	https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git
COMPONENT_ARCHIVE_URL=	\
	https://cdn.kernel.org/pub/linux/kernel/firmware/$(COMPONENT_ARCHIVE)
COMPONENT_ARCHIVE_HASH=	\
	sha256:a10d1ab5baf2c74ba92e8c7cf313e691696c26bddfa6df00db795e48d70cd4ca
COMPONENT_FMRI=		system/firmware/amdgpu-polaris
COMPONENT_CLASSIFICATION=	System/Hardware
COMPONENT_LICENSE=	AMD amdgpu firmware licence (redistributable)
COMPONENT_LICENSE_FILE=	amdgpu-polaris-firmware.license

TEST_TARGET=		$(NO_TESTS)

include $(WS_MAKE_RULES)/common.mk

POLARIS_CHIPS=		polaris10 polaris11 polaris12

COMPONENT_BUILD_ACTION=	:

COMPONENT_INSTALL_ACTION= \
	$(MKDIR) $(PROTO_DIR)/kernel/firmware/amdgpu; \
	for c in $(POLARIS_CHIPS); do \
		for f in $(SOURCE_DIR)/amdgpu/$$c\_*.bin; do \
			$(INSTALL) -m 0444 $$f \
			    $(PROTO_DIR)/kernel/firmware/amdgpu/ || exit 1; \
		done; \
	done
