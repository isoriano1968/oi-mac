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
 * appletb: the Touch Bar of the MacBook Pro 2016-2017 (MacBookPro13,2-3,
 * 14,2-3), behind the Apple T1 "iBridge" (USB 05ac:8600).
 *
 * In its default configuration (1) the iBridge has four interfaces: two
 * for the FaceTime camera and two HID interfaces, the Touch Bar and the
 * ambient light sensor.  The Touch Bar's key presses are ordinary HID
 * keyboard reports (Esc, F1-F12), which hid(4D) and usbkbm(4M) already
 * deliver to the console keyboard.  What no generic driver does is to turn
 * the bar on and choose what it shows: without that it stays dark, or
 * offers only Esc.  Two requests on the default pipe do it (protocol facts
 * from the Linux apple-ib-tb driver, used as a reference only):
 *
 *   mode:    bmRequestType 0x40 (host-to-device, vendor, device),
 *            bRequest SET_REPORT (0x09), wValue (report type << 8) | report
 *            id of the output report that holds usage 0x00ff0004 in the
 *            keyboard application collection, wIndex the Touch Bar
 *            interface, one data byte: 0 Esc only, 1 F1-F12, 2 special
 *            (brightness, media, volume), 3 off.
 *   display: a class SET_REPORT (0x21) to the Touch Bar interface for the
 *            report holding usage 0xff120021 (collection 0xff120001):
 *            { report id, 1, state, 0 ... } (11 bytes); state 1 on, 2 dim,
 *            4 off.
 *
 * This driver binds to the other HID interface (the light sensor, which
 * nothing else uses), so hid keeps the Touch Bar interface and its keys.
 * It finds the Touch Bar interface by reading the report descriptors of
 * the iBridge's HID interfaces, sends both requests at attach, after a
 * resume and after a reconnect, and does nothing else.  USBA does not
 * restrict wIndex or the recipient of a control request, so an interface
 * driver can address its sibling interface on the shared default pipe.
 *
 * Tunables (/etc/system "set appletb:appletb_mode = 2"):
 *   appletb_mode  0-3 as above, default 1 (F1-F12; Esc stays on the left).
 *   appletb_disp  1 on (default), 2 dim, 4 off.
 * Switching with the Fn key (special keys while Fn is held, or the other
 * way round) needs the internal keyboard's Fn state from atopcase(4D) and
 * comes later.
 */

#define	USBDRV_MAJOR_VER	2
#define	USBDRV_MINOR_VER	0

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/strsun.h>
#include <sys/usb/usba.h>

#define	APPLETB_MODE_ESC	0
#define	APPLETB_MODE_FN		1
#define	APPLETB_MODE_SPCL	2
#define	APPLETB_MODE_OFF	3

#define	APPLETB_DISP_ON		1
#define	APPLETB_DISP_DIM	2
#define	APPLETB_DISP_OFF	4

int appletb_mode = APPLETB_MODE_FN;
int appletb_disp = APPLETB_DISP_ON;

/* HID class */
#define	APPLETB_HID_CLASS	3
#define	HID_REQ_SET_REPORT	0x09
#define	HID_DESCR_REPORT	0x22
#define	APPLETB_RDESC_MAX	2048

/* Usages, (page << 16) | usage */
#define	USAGE_GD_KEYBOARD	0x00010006	/* application collection */
#define	USAGE_TB_MODE		0x00ff0004
#define	USAGE_APPLE_APP		0xff120001	/* application collection */
#define	USAGE_TB_DISP		0xff120021

#define	APPLETB_DISP_REPORT_LEN	11

/* HID report types for wValue */
#define	REPORT_INPUT		1
#define	REPORT_OUTPUT		2
#define	REPORT_FEATURE		3

typedef struct appletb_field {
	boolean_t	af_found;
	uint8_t		af_rid;
	uint8_t		af_type;	/* REPORT_* */
} appletb_field_t;

typedef struct appletb {
	dev_info_t		*atb_dip;
	usb_client_dev_data_t	*atb_dev_data;
	kmutex_t		atb_lock;
	int			atb_own_if;
	int			atb_tb_if;	/* -1: not found */
	appletb_field_t		atb_mode;
	appletb_field_t		atb_disp;
} appletb_t;

static void *appletb_statep;

/* appletb hid lookup: begin (also built by the port's userland unit test) */

/*
 * Find, in a report descriptor, the report (id, type) of the first main
 * item that carries usage `usage' inside the application collection
 * `app'.  Returns B_TRUE when found.
 */
static boolean_t
appletb_find_usage(const uint8_t *d, size_t len, uint32_t app, uint32_t usage,
    appletb_field_t *af)
{
#define	ATB_MAX_DEPTH	16
#define	ATB_MAX_USAGES	32
	uint32_t page = 0, usages[ATB_MAX_USAGES], coll[ATB_MAX_DEPTH];
	uint32_t umin = 0, umax = 0;
	uint_t nusages = 0, depth = 0, i;
	boolean_t range = B_FALSE;
	uint8_t rid = 0;
	size_t off = 0;

	bzero(af, sizeof (*af));
	while (off < len) {
		uint8_t pfx = d[off++];
		uint_t size = pfx & 0x3, type = (pfx >> 2) & 0x3;
		uint_t tag = pfx >> 4;
		uint32_t v = 0;

		if (pfx == 0xfe) {		/* long item */
			if (off + 2 > len)
				break;
			off += 2 + d[off];
			continue;
		}
		if (size == 3)
			size = 4;
		if (off + size > len)
			break;
		for (i = 0; i < size; i++)
			v |= (uint32_t)d[off + i] << (8 * i);
		off += size;

		if (type == 1) {			/* global */
			if (tag == 0x0)
				page = v;
			else if (tag == 0x8)
				rid = v & 0xff;
			continue;
		}
		if (type == 2) {			/* local */
			uint32_t full = size == 4 ? v : (page << 16) | v;

			if (tag == 0x0 && nusages < ATB_MAX_USAGES)
				usages[nusages++] = full;
			else if (tag == 0x1) {
				umin = full;
				range = B_TRUE;
			} else if (tag == 0x2) {
				umax = full;
				range = B_TRUE;
			}
			continue;
		}
		if (type != 0)
			continue;

		/* main items */
		switch (tag) {
		case 0xa:				/* collection */
			if (depth < ATB_MAX_DEPTH)
				coll[depth] = nusages > 0 ? usages[0] : 0;
			depth++;
			break;
		case 0xc:				/* end collection */
			if (depth > 0)
				depth--;
			break;
		case 0x8:				/* input */
		case 0x9:				/* output */
		case 0xb: {				/* feature */
			boolean_t hit = B_FALSE;

			if (depth == 0 || coll[0] != app)
				break;
			for (i = 0; i < nusages; i++) {
				if (usages[i] == usage)
					hit = B_TRUE;
			}
			if (range && usage >= umin && usage <= umax)
				hit = B_TRUE;
			if (hit) {
				af->af_found = B_TRUE;
				af->af_rid = rid;
				af->af_type = tag == 0x8 ? REPORT_INPUT :
				    tag == 0x9 ? REPORT_OUTPUT :
				    REPORT_FEATURE;
				return (B_TRUE);
			}
			break;
		}
		}
		nusages = 0;
		range = B_FALSE;
		umin = umax = 0;
	}
	return (B_FALSE);
#undef	ATB_MAX_DEPTH
#undef	ATB_MAX_USAGES
}

/* appletb hid lookup: end */

/* GET_DESCRIPTOR(report) of interface `ifno'; the caller frees *mpp. */
static int
appletb_get_rdesc(appletb_t *atb, int ifno, mblk_t **mpp)
{
	usb_ctrl_setup_t setup;
	usb_cr_t cr;
	usb_cb_flags_t cbf;

	bzero(&setup, sizeof (setup));
	setup.bmRequestType = USB_DEV_REQ_DEV_TO_HOST |
	    USB_DEV_REQ_TYPE_STANDARD | USB_DEV_REQ_RCPT_IF;
	setup.bRequest = USB_REQ_GET_DESCR;
	setup.wValue = HID_DESCR_REPORT << 8;
	setup.wIndex = (uint16_t)ifno;
	setup.wLength = APPLETB_RDESC_MAX;
	setup.attrs = USB_ATTRS_SHORT_XFER_OK;
	*mpp = NULL;
	if (usb_pipe_ctrl_xfer_wait(atb->atb_dev_data->dev_default_ph, &setup,
	    mpp, &cr, &cbf, 0) != USB_SUCCESS) {
		if (*mpp != NULL) {
			freemsg(*mpp);
			*mpp = NULL;
		}
		return (EIO);
	}
	return (*mpp != NULL ? 0 : EIO);
}

/*
 * Look at every HID interface of the current configuration for the Touch
 * Bar's mode and display reports.
 */
static void
appletb_find_touchbar(appletb_t *atb)
{
	usb_cfg_data_t *cfg = atb->atb_dev_data->dev_curr_cfg;
	uint_t i;

	atb->atb_tb_if = -1;
	if (cfg == NULL)
		return;
	for (i = 0; i < cfg->cfg_n_if; i++) {
		usb_if_data_t *ifd = &cfg->cfg_if[i];
		usb_if_descr_t *id;
		appletb_field_t mode, disp;
		mblk_t *mp;

		if (ifd->if_n_alt == 0 || ifd->if_alt == NULL)
			continue;
		id = &ifd->if_alt[0].altif_descr;
		if (id->bInterfaceClass != APPLETB_HID_CLASS)
			continue;
		if (appletb_get_rdesc(atb, id->bInterfaceNumber, &mp) != 0) {
			dev_err(atb->atb_dip, CE_NOTE, "!interface %u: no "
			    "report descriptor", id->bInterfaceNumber);
			continue;
		}
		(void) appletb_find_usage(mp->b_rptr, MBLKL(mp),
		    USAGE_GD_KEYBOARD, USAGE_TB_MODE, &mode);
		(void) appletb_find_usage(mp->b_rptr, MBLKL(mp),
		    USAGE_APPLE_APP, USAGE_TB_DISP, &disp);
		dev_err(atb->atb_dip, CE_CONT, "?interface %u: report "
		    "descriptor %lu bytes%s%s\n", id->bInterfaceNumber,
		    (ulong_t)MBLKL(mp), mode.af_found ? ", touch bar mode" : "",
		    disp.af_found ? ", touch bar display" : "");
		freemsg(mp);
		if (mode.af_found) {
			atb->atb_tb_if = id->bInterfaceNumber;
			atb->atb_mode = mode;
			atb->atb_disp = disp;
			return;
		}
	}
}

static int
appletb_xfer_out(appletb_t *atb, uchar_t bmRequestType, uint16_t wValue,
    const uint8_t *data, uint16_t len)
{
	usb_ctrl_setup_t setup;
	usb_cr_t cr;
	usb_cb_flags_t cbf;
	mblk_t *mp;
	int tries, rv = USB_FAILURE;

	for (tries = 0; tries < 5; tries++) {
		if ((mp = allocb(len, BPRI_HI)) == NULL)
			return (ENOMEM);
		bcopy(data, mp->b_wptr, len);
		mp->b_wptr += len;
		bzero(&setup, sizeof (setup));
		setup.bmRequestType = bmRequestType;
		setup.bRequest = HID_REQ_SET_REPORT;
		setup.wValue = wValue;
		setup.wIndex = (uint16_t)atb->atb_tb_if;
		setup.wLength = len;
		rv = usb_pipe_ctrl_xfer_wait(atb->atb_dev_data->dev_default_ph,
		    &setup, &mp, &cr, &cbf, 0);
		if (mp != NULL)
			freemsg(mp);
		if (rv == USB_SUCCESS)
			return (0);
		if (cr != USB_CR_STALL)
			break;
		delay(drv_usectohz(1000 << tries));	/* stalled: retry */
	}
	return (EIO);
}

/* Turn the bar on and set its mode, as the tunables say. */
static void
appletb_apply(appletb_t *atb)
{
	uint8_t disp[APPLETB_DISP_REPORT_LEN];
	uint8_t mode = (uint8_t)appletb_mode;

	mutex_enter(&atb->atb_lock);
	if (atb->atb_tb_if < 0) {
		mutex_exit(&atb->atb_lock);
		return;
	}
	if (atb->atb_disp.af_found) {
		bzero(disp, sizeof (disp));
		disp[0] = atb->atb_disp.af_rid;
		disp[1] = 1;
		disp[2] = (uint8_t)appletb_disp;
		if (appletb_xfer_out(atb, USB_DEV_REQ_HOST_TO_DEV |
		    USB_DEV_REQ_TYPE_CLASS | USB_DEV_REQ_RCPT_IF,
		    (atb->atb_disp.af_type << 8) | atb->atb_disp.af_rid,
		    disp, sizeof (disp)) != 0)
			dev_err(atb->atb_dip, CE_WARN, "!cannot set the touch "
			    "bar display to %d", appletb_disp);
	}
	if (appletb_xfer_out(atb, USB_DEV_REQ_HOST_TO_DEV |
	    USB_DEV_REQ_TYPE_VENDOR | USB_DEV_REQ_RCPT_DEV,
	    (atb->atb_mode.af_type << 8) | atb->atb_mode.af_rid,
	    &mode, 1) != 0)
		dev_err(atb->atb_dip, CE_WARN, "!cannot set the touch bar "
		    "mode to %d", appletb_mode);
	mutex_exit(&atb->atb_lock);
}

static int
appletb_reconnect(dev_info_t *dip)
{
	appletb_t *atb = ddi_get_soft_state(appletb_statep,
	    ddi_get_instance(dip));

	if (atb != NULL)
		appletb_apply(atb);
	return (USB_SUCCESS);
}

static int
appletb_disconnect(dev_info_t *dip)
{
	return (USB_SUCCESS);
}

static int
appletb_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	appletb_t *atb;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		if ((atb = ddi_get_soft_state(appletb_statep, instance)) ==
		    NULL)
			return (DDI_FAILURE);
		appletb_apply(atb);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	if (ddi_soft_state_zalloc(appletb_statep, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);
	atb = ddi_get_soft_state(appletb_statep, instance);
	atb->atb_dip = dip;
	atb->atb_tb_if = -1;

	if (usb_client_attach(dip, USBDRV_VERSION, 0) != USB_SUCCESS) {
		ddi_soft_state_free(appletb_statep, instance);
		return (DDI_FAILURE);
	}
	if (usb_get_dev_data(dip, &atb->atb_dev_data, USB_PARSE_LVL_ALL, 0) !=
	    USB_SUCCESS) {
		usb_client_detach(dip, NULL);
		ddi_soft_state_free(appletb_statep, instance);
		return (DDI_FAILURE);
	}
	mutex_init(&atb->atb_lock, NULL, MUTEX_DRIVER,
	    atb->atb_dev_data->dev_iblock_cookie);
	atb->atb_own_if = atb->atb_dev_data->dev_curr_if;

	appletb_find_touchbar(atb);
	if (atb->atb_tb_if < 0) {
		dev_err(dip, CE_WARN, "no touch bar interface found");
	} else {
		if (atb->atb_tb_if == atb->atb_own_if)
			dev_err(dip, CE_WARN, "bound to the touch bar interface "
			    "itself (%d): its keys do not reach the console; "
			    "the driver alias should name the other HID "
			    "interface", atb->atb_tb_if);
		dev_err(dip, CE_CONT, "?touch bar on interface %d: mode report "
		    "%u (type %u), display report %s%u\n", atb->atb_tb_if,
		    atb->atb_mode.af_rid, atb->atb_mode.af_type,
		    atb->atb_disp.af_found ? "" : "none ",
		    atb->atb_disp.af_rid);
		appletb_apply(atb);
	}

	if (usb_register_hotplug_cbs(dip, appletb_disconnect,
	    appletb_reconnect) != USB_SUCCESS)
		dev_err(dip, CE_NOTE, "!no reconnect callback");

	ddi_report_dev(dip);
	return (DDI_SUCCESS);
}

static int
appletb_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	appletb_t *atb = ddi_get_soft_state(appletb_statep, instance);

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
	if (atb == NULL)
		return (DDI_FAILURE);

	usb_unregister_hotplug_cbs(dip);
	mutex_destroy(&atb->atb_lock);
	usb_client_detach(dip, atb->atb_dev_data);
	ddi_soft_state_free(appletb_statep, instance);
	return (DDI_SUCCESS);
}

static struct cb_ops appletb_cb_ops = {
	.cb_open = nulldev,
	.cb_close = nulldev,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = nodev,
	.cb_devmap = nodev,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_str = NULL,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops appletb_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = ddi_no_info,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = appletb_attach,
	.devo_detach = appletb_detach,
	.devo_reset = nodev,
	.devo_cb_ops = &appletb_cb_ops,
	.devo_bus_ops = NULL,
	.devo_power = NULL,
	.devo_quiesce = ddi_quiesce_not_needed
};

static struct modldrv appletb_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Apple T1 Touch Bar",
	.drv_dev_ops = &appletb_dev_ops
};

static struct modlinkage appletb_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &appletb_modldrv, NULL }
};

int
_init(void)
{
	int rv;

	if ((rv = ddi_soft_state_init(&appletb_statep, sizeof (appletb_t),
	    1)) != 0)
		return (rv);
	if ((rv = mod_install(&appletb_modlinkage)) != 0)
		ddi_soft_state_fini(&appletb_statep);
	return (rv);
}

int
_fini(void)
{
	int rv;

	if ((rv = mod_remove(&appletb_modlinkage)) == 0)
		ddi_soft_state_fini(&appletb_statep);
	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&appletb_modlinkage, modinfop));
}
