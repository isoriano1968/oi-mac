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

# osnet-incorporation-apple: OpenIndiana's
# consolidation/osnet/osnet-incorporation of the osnet build our
# osnet-apple packages are made for (23395), republished unchanged as
# 23395.$(APPLE_REV) on oi-local.
#
# Why: osnet-apple (SUNWcs, loader, extended-system-utilities, ...) comes
# from the sticky oi-local and incorporates osnet-incorporation 23395, so
# a newer osnet waits for an osnet-apple rebuild instead of putting the
# stock modules back.  But with OI's own osnet-incorporation installed,
# pkg update wants hipster's newest one (23435 since 2026-11-20), cannot
# reach it and ends in "No solution" (rc 1).  Installed from oi-local, the
# incorporation's newest version is this one: pkg update reports "No
# updates available" (rc 4, tested on an installed r1 image; whether other
# updates get through is untested), and osnet moves only with a
# new osnet-apple and a new revision here.  (Not pkg freeze: that would
# be hidden user state on installed systems.)
#
# Its incorporate locks stay OI's (@...23395): osnet-apple's 23395.N
# versions satisfy them, the stock 23395 packages too.
#
# Pristine source: OI's own package (manifest) from the hipster repository.
#
#   gmake            fetch, transform -> build/manifest
#   gmake publish    into $(REPO), through the gate
#   gmake lint

COMPONENT_NAME=		osnet-incorporation-apple
FMRI=			consolidation/osnet/osnet-incorporation
OSNET_VERSION=		0.5.11-2026.0.0.23395
APPLE_REV=		1
HIPSTER=		https://pkg.openindiana.org/hipster
REPO?=			$(HOME)/oi-local-repo
LINT_CACHE?=		$(HOME)/src/lint-cache-hipster
BUILD_DIR=		$(CURDIR)/build

all: $(BUILD_DIR)/manifest

$(BUILD_DIR)/.fetched:
	rm -rf $(BUILD_DIR)/raw
	mkdir -p $(BUILD_DIR)/raw
	/usr/bin/pkgrecv -s $(HIPSTER) -d $(BUILD_DIR)/raw --raw \
	    $(FMRI)@$(OSNET_VERSION)
	cp $(BUILD_DIR)/raw/*/*/manifest $(BUILD_DIR)/manifest.oi
	touch $@

$(BUILD_DIR)/manifest: $(BUILD_DIR)/.fetched osnet-incorporation-apple.mog
	/usr/bin/pkgmogrify -D NEW=$(OSNET_VERSION).$(APPLE_REV) \
	    $(BUILD_DIR)/manifest.oi osnet-incorporation-apple.mog > $@
	grep '^set name=pkg.fmri' $@
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
