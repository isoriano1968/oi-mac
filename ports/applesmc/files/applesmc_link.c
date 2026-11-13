/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 isoriano1968
 */

/*
 * devfsadm link module for applesmc(4D): /dev/applesmc.
 *
 * In an illumos-gate build this rule is part of SUNW_misc_link_i386.so
 * (patches/0001, usr/src/cmd/devfsadm/i386/misc_link_i386.c); the
 * out-of-tree package cannot change that module, nor deliver into /dev,
 * so it ships the same rule as its own module, as gfx-drm does with
 * SUNW_drm_link_i386.so.
 */

#include <devfsadm.h>

static int
applesmc_link(di_minor_t minor, di_node_t node)
{
	(void) devfsadm_mklink(di_minor_name(minor), node, minor, 0);
	return (DEVFSADM_CONTINUE);
}

static devfsadm_create_t applesmc_cbt[] = {
	{ "pseudo", "ddi_pseudo", "applesmc",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, applesmc_link,
	},
};

DEVFSADM_CREATE_INIT_V0(applesmc_cbt);

static devfsadm_remove_t applesmc_remove_cbt[] = {
	{ "pseudo", "^applesmc$", RM_ALWAYS | RM_PRE | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all
	},
};

DEVFSADM_REMOVE_INIT_V0(applesmc_remove_cbt);
