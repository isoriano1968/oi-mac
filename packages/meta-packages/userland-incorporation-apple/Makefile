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

# userland-incorporation-apple: OpenIndiana's
# consolidation/userland/userland-incorporation of the hipster build our
# stack is built on (35449), republished as 35449.$(APPLE_REV) on oi-local,
# with the version locks of the packages oi-local replaces (GNOME 45,
# Xorg 21.1, Mesa 26, libdrm, PulseAudio, HAL, ...) moved to oi-local's
# versions.  An image (the OI-Apple GNOME medium) then installs our builds
# with the locks on, instead of turning them off with change-facet.
#
# Pristine source: OI's own package (manifest) from the hipster repository.
# The transform is made from what oi-local carries at build time
# (gen-mog.sh), so it follows the merged repository; with PINS, from an
# image's version list instead (35449.3: pins-oi-mac-r1, the OI Mac r1
# image).
#
#   gmake            fetch, transform -> build/manifest
#   gmake publish    into $(REPO)
#   gmake lint

COMPONENT_NAME=		userland-incorporation-apple
FMRI=			consolidation/userland/userland-incorporation
UL_VERSION=		0.5.11-2026.0.0.35449
APPLE_REV=		6
PINS?=			$(CURDIR)/pins-oi-mac-r1
HIPSTER=		https://pkg.openindiana.org/hipster
REPO?=			$(HOME)/oi-local-repo
LINT_CACHE?=		$(HOME)/src/lint-cache-hipster
BUILD_DIR=		$(CURDIR)/build

all: $(BUILD_DIR)/manifest

$(BUILD_DIR)/.fetched:
	rm -rf $(BUILD_DIR)/raw
	mkdir -p $(BUILD_DIR)/raw
	/usr/bin/pkgrecv -s $(HIPSTER) -d $(BUILD_DIR)/raw --raw \
	    $(FMRI)@$(UL_VERSION)
	cp $(BUILD_DIR)/raw/*/*/manifest $(BUILD_DIR)/manifest.oi
	touch $@

$(BUILD_DIR)/apple.mog: $(BUILD_DIR)/.fetched gen-mog.sh $(PINS)
	sh gen-mog.sh $(REPO) $(BUILD_DIR)/manifest.oi \
	    $(UL_VERSION).$(APPLE_REV) $(PINS) > $@

$(BUILD_DIR)/manifest: $(BUILD_DIR)/apple.mog
	/usr/bin/pkgmogrify $(BUILD_DIR)/manifest.oi $(BUILD_DIR)/apple.mog > $@
	grep -c 'type=incorporate' $@

lint: $(BUILD_DIR)/manifest
	/usr/bin/pkglint -c $(LINT_CACHE) $(BUILD_DIR)/manifest

GATE?=			$(HOME)/src/oi-userland/tools/oi-manifest-gate

publish: $(BUILD_DIR)/manifest
	$(GATE) $(REPO) $(BUILD_DIR)/manifest $(BUILD_DIR)
	/usr/bin/pkgsend -s $(REPO) publish --fmri-in-manifest \
	    $(BUILD_DIR)/manifest
	/usr/bin/pkgrepo refresh -s $(REPO)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all lint publish clean
