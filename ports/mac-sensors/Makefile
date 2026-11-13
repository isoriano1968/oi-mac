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

# mac-sensors: "Mac Sensors", a GTK 4 app that shows the temperatures and
# fans of an Intel Mac (SMC through smcstat(8) of driver/sensor/applesmc) and
# of any machine (illumos ksensors) on a drawing of the machine.
#
# This project's own code: the source is this directory (no upstream
# archive).  Installed as an application with a desktop file and a hicolor
# icon; the interpreter is fixed to the Python whose pygobject and pycairo
# packages it depends on.
#
#   gmake check / gmake publish / gmake clean   (GNU make, on OpenIndiana)

COMPONENT_NAME=		mac-sensors
COMPONENT_VERSION=	1.2
COMPONENT_LICENSE=	CDDL-1.0
COMPONENT_LICENSE_FILE=	$(COMPONENT_NAME).license
APP_ID=			org.openindiana.MacSensors

PYVER=			3.9
PYTHON=			/usr/bin/python$(PYVER)

BUILD_DIR=		$(CURDIR)/build
PROTO=			$(BUILD_DIR)/proto
REPO?=			$(HOME)/oi-repo
# branch version: year.0.0.revision, as oi-userland
PKG_VERSION=		$(COMPONENT_VERSION)-2026.0.0.0

all: $(BUILD_DIR)/.built

$(BUILD_DIR)/.built: mac-sensors.py $(APP_ID).desktop $(APP_ID).svg
	rm -rf $(PROTO)
	mkdir -p $(PROTO)/usr/bin $(PROTO)/usr/share/applications \
	    $(PROTO)/usr/share/icons/hicolor/scalable/apps
	sed -e '1s|^#!.*|#!$(PYTHON)|' mac-sensors.py > $(PROTO)/usr/bin/mac-sensors
	chmod 0555 $(PROTO)/usr/bin/mac-sensors
	cp $(APP_ID).desktop $(PROTO)/usr/share/applications/
	cp $(APP_ID).svg $(PROTO)/usr/share/icons/hicolor/scalable/apps/
	touch $@

check: all
	$(PYTHON) -m py_compile $(PROTO)/usr/bin/mac-sensors
	rm -rf $(PROTO)/usr/bin/__pycache__
	/usr/bin/desktop-file-validate $(PROTO)/usr/share/applications/$(APP_ID).desktop
	/usr/bin/xmllint --noout $(PROTO)/usr/share/icons/hicolor/scalable/apps/$(APP_ID).svg

publish: check
	cp $(COMPONENT_LICENSE_FILE) $(PROTO)/
	/usr/bin/pkgmogrify -D PKG_VERSION=$(PKG_VERSION) -D PYVER=$(PYVER) \
	    -D APP_ID=$(APP_ID) -D COMPONENT_VERSION=$(COMPONENT_VERSION) \
	    $(COMPONENT_NAME).p5m > $(BUILD_DIR)/manifest
	/usr/bin/pkglint $(BUILD_DIR)/manifest
	/usr/bin/pkgsend -s $(REPO) publish -d $(PROTO) \
	    --fmri-in-manifest $(BUILD_DIR)/manifest

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all check clean publish
