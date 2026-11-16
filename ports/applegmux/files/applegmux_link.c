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
 * devfsadm(8) link module for applegmux(4D): /dev/applegmux.  As
 * ports/applesmc/files/applesmc_link.c.
 */

#include <devfsadm.h>

static int
applegmux_link(di_minor_t minor, di_node_t node)
{
	(void) devfsadm_mklink(di_minor_name(minor), node, minor, 0);
	return (DEVFSADM_CONTINUE);
}

static devfsadm_create_t applegmux_cbt[] = {
	{ "pseudo", "ddi_pseudo", "applegmux",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, applegmux_link,
	},
};

DEVFSADM_CREATE_INIT_V0(applegmux_cbt);

static devfsadm_remove_t applegmux_remove_cbt[] = {
	{ "pseudo", "^applegmux$", RM_ALWAYS | RM_PRE | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all
	},
};

DEVFSADM_REMOVE_INIT_V0(applegmux_remove_cbt);
