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
 * devfsadm(8) link module for atopcase(4D): /dev/atopcase/keyboard and
 * /dev/atopcase/mouse (the trackpad as a relative VUID mouse, read by Xorg
 * through /usr/share/X11/xorg.conf.d/60-atopcase-trackpad.conf).  As
 * ports/applesmc/files/applesmc_link.c.
 */

#include <string.h>
#include <devfsadm.h>

static int
atopcase_link(di_minor_t minor, di_node_t node)
{
	char path[64];
	const char *name = di_minor_name(minor);

	if (name == NULL || (strcmp(name, "keyboard") != 0 &&
	    strcmp(name, "mouse") != 0))
		return (DEVFSADM_CONTINUE);
	(void) snprintf(path, sizeof (path), "atopcase/%s", name);
	(void) devfsadm_mklink(path, node, minor, 0);
	return (DEVFSADM_CONTINUE);
}

static devfsadm_create_t atopcase_cbt[] = {
	{ "keyboard", "ddi_keyboard", "atopcase",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, atopcase_link,
	},
	{ "mouse", "ddi_mouse", "atopcase",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, atopcase_link,
	},
};

DEVFSADM_CREATE_INIT_V0(atopcase_cbt);

static devfsadm_remove_t atopcase_remove_cbt[] = {
	{ "keyboard", "^atopcase/keyboard$", RM_ALWAYS | RM_PRE | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all
	},
	{ "mouse", "^atopcase/mouse$", RM_ALWAYS | RM_PRE | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all
	},
};

DEVFSADM_REMOVE_INIT_V0(atopcase_remove_cbt);
