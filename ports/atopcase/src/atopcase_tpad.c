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
 * atopcase trackpad: a relative mouse on the minor node "mouse"
 * (/dev/atopcase/mouse), as VUID Firm_events in the format usbms(4M) gives
 * (LOC_X_DELTA, LOC_Y_DELTA, MS_LEFT/MIDDLE/RIGHT, the vertical wheel).
 *
 * The topcase trackpad starts in HID mode: until a driver switches it to
 * raw mode (feature report 2, FreeBSD bcm5974_set_device_mode_hid()) it
 * sends ordinary HID mouse reports, movement deltas and the button.  That
 * is what this file uses: it parses the trackpad's own report descriptor
 * (fetched by atopcase_proto_init) for the input report with relative X
 * and Y, and takes X, Y, the wheel and up to three buttons from it.
 * Multitouch (raw mode, Wellspring9 model 6 finger records) comes later.
 *
 * Xorg reads the node with the mouse driver, Protocol "VUID" (an
 * xorg.conf.d snippet in the package); the node is not linked under consms,
 * since dacf.conf has no rule for it.  ioctls come either as I_STR (from a
 * multiplexor) or transparent (ioctl(2) on the node), and both are handled.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/time.h>
#include <sys/vuid_event.h>
#include <sys/vuid_wheel.h>
#include <sys/msio.h>
#include <sys/policy.h>
#include <sys/sysmacros.h>

#include "atopcase.h"

/* atopcase tpad parser: begin (also built by the port's userland unit test) */

/* HID items (HID 1.11, 6.2.2) */
#define	HID_ITEM_TYPE_MAIN	0
#define	HID_ITEM_TYPE_GLOBAL	1
#define	HID_ITEM_TYPE_LOCAL	2

#define	HID_MAIN_INPUT		0x8
#define	HID_MAIN_OUTPUT		0x9
#define	HID_MAIN_COLLECTION	0xa
#define	HID_MAIN_FEATURE	0xb
#define	HID_MAIN_END_COLL	0xc

#define	HID_GLOBAL_PAGE		0x0
#define	HID_GLOBAL_LOGMIN	0x1
#define	HID_GLOBAL_RSIZE	0x7
#define	HID_GLOBAL_RID		0x8
#define	HID_GLOBAL_RCOUNT	0x9
#define	HID_GLOBAL_PUSH		0xa
#define	HID_GLOBAL_POP		0xb

#define	HID_LOCAL_USAGE		0x0
#define	HID_LOCAL_USAGE_MIN	0x1
#define	HID_LOCAL_USAGE_MAX	0x2

#define	HID_INPUT_CONSTANT	0x01
#define	HID_INPUT_VARIABLE	0x02
#define	HID_INPUT_RELATIVE	0x04

#define	HID_PAGE_DESKTOP	0x01
#define	HID_PAGE_BUTTON		0x09
#define	HID_USAGE_X		0x30
#define	HID_USAGE_Y		0x31
#define	HID_USAGE_WHEEL		0x38

#define	TP_MAX_USAGES		16
#define	TP_MAX_REPORT		64		/* bytes after the report id */

/*
 * Parse the trackpad report descriptor: find the first input report with a
 * relative X and Y, and in that report the wheel and buttons 1-3.  Returns
 * B_TRUE when such a report was found.
 */
static boolean_t
atopcase_tpad_parse(atopcase_tpad_t *tp, const uint8_t *d, uint16_t len)
{
	uint32_t page = 0, rsize = 0, rcount = 0, usages[TP_MAX_USAGES];
	uint32_t umin = 0, umax = 0;
	int32_t logmin = 0;
	uint_t nusages = 0, i;
	boolean_t have_range = B_FALSE;
	uint8_t rid = 0;
	uint16_t pos[256];
	uint16_t off = 0;

	bzero(pos, sizeof (pos));
	bzero(&tp->at_layout, sizeof (tp->at_layout));

	while (off < len) {
		uint8_t pfx = d[off++];
		uint_t size = pfx & 0x3, type = (pfx >> 2) & 0x3, tag = pfx >> 4;
		uint32_t v = 0;

		if (pfx == 0xfe) {		/* long item: skip */
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

		switch (type) {
		case HID_ITEM_TYPE_GLOBAL:
			switch (tag) {
			case HID_GLOBAL_PAGE:
				page = v;
				break;
			case HID_GLOBAL_LOGMIN:
				/* sign-extend the item */
				if (size == 1)
					logmin = (int8_t)v;
				else if (size == 2)
					logmin = (int16_t)v;
				else
					logmin = (int32_t)v;
				break;
			case HID_GLOBAL_RSIZE:
				rsize = v;
				break;
			case HID_GLOBAL_RID:
				rid = v & 0xff;
				tp->at_layout.tl_uses_rid = B_TRUE;
				break;
			case HID_GLOBAL_RCOUNT:
				rcount = v;
				break;
			case HID_GLOBAL_PUSH:
			case HID_GLOBAL_POP:
				/* not seen in mouse descriptors */
				ATOPCASE_DBG(1, "trackpad descriptor uses "
				    "push/pop");
				break;
			}
			break;

		case HID_ITEM_TYPE_LOCAL:
			switch (tag) {
			case HID_LOCAL_USAGE:
				if (nusages < TP_MAX_USAGES)
					usages[nusages++] = size == 4 ? v :
					    (page << 16) | v;
				break;
			case HID_LOCAL_USAGE_MIN:
				umin = size == 4 ? v : (page << 16) | v;
				have_range = B_TRUE;
				break;
			case HID_LOCAL_USAGE_MAX:
				umax = size == 4 ? v : (page << 16) | v;
				have_range = B_TRUE;
				break;
			}
			break;

		case HID_ITEM_TYPE_MAIN:
			if (tag == HID_MAIN_INPUT) {
				for (i = 0; i < rcount; i++) {
					uint32_t u;
					atopcase_tpad_field_t *f = NULL;

					if (nusages > 0)
						u = usages[MIN(i, nusages - 1)];
					else if (have_range && umin + i <= umax)
						u = umin + i;
					else
						u = 0;

					if ((v & HID_INPUT_CONSTANT) != 0 ||
					    (v & HID_INPUT_VARIABLE) == 0)
						u = 0;	/* padding, arrays */

					if (u == ((HID_PAGE_DESKTOP << 16) |
					    HID_USAGE_X))
						f = &tp->at_layout.tl_x;
					else if (u == ((HID_PAGE_DESKTOP <<
					    16) | HID_USAGE_Y))
						f = &tp->at_layout.tl_y;
					else if (u == ((HID_PAGE_DESKTOP <<
					    16) | HID_USAGE_WHEEL))
						f = &tp->at_layout.tl_wheel;
					else if ((u >> 16) == HID_PAGE_BUTTON &&
					    (u & 0xffff) >= 1 &&
					    (u & 0xffff) <= ATOPCASE_TP_NBUTTONS)
						f = &tp->at_layout.tl_btn[
						    (u & 0xffff) - 1];

					if (f != NULL && !f->tf_valid) {
						f->tf_valid = B_TRUE;
						f->tf_rid = rid;
						f->tf_pos = pos[rid];
						f->tf_size = MIN(rsize, 32);
						f->tf_rel = (v &
						    HID_INPUT_RELATIVE) != 0;
						f->tf_signed = logmin < 0;
					}
					pos[rid] += rsize;
				}
			}
			/* locals are reset after every main item */
			nusages = 0;
			have_range = B_FALSE;
			umin = umax = 0;
			break;
		}
	}

	if (!tp->at_layout.tl_x.tf_valid || !tp->at_layout.tl_y.tf_valid ||
	    !tp->at_layout.tl_x.tf_rel || !tp->at_layout.tl_y.tf_rel ||
	    tp->at_layout.tl_x.tf_rid != tp->at_layout.tl_y.tf_rid)
		return (B_FALSE);

	/* Only fields of the X/Y report count. */
	rid = tp->at_layout.tl_x.tf_rid;
	tp->at_layout.tl_rid = rid;
	tp->at_layout.tl_bits = pos[rid];
	if (tp->at_layout.tl_wheel.tf_rid != rid ||
	    !tp->at_layout.tl_wheel.tf_rel)
		tp->at_layout.tl_wheel.tf_valid = B_FALSE;
	tp->at_nbuttons = 0;
	for (i = 0; i < ATOPCASE_TP_NBUTTONS; i++) {
		if (tp->at_layout.tl_btn[i].tf_rid != rid)
			tp->at_layout.tl_btn[i].tf_valid = B_FALSE;
		if (tp->at_layout.tl_btn[i].tf_valid)
			tp->at_nbuttons = i + 1;
	}
	return (tp->at_layout.tl_bits <= TP_MAX_REPORT * 8);
}

/* A field's value; relative fields are signed even without logical min. */
static int32_t
atopcase_tpad_get(const atopcase_tpad_field_t *f, const uint8_t *data)
{
	uint32_t v = 0;
	uint_t i;

	if (!f->tf_valid || f->tf_size == 0)
		return (0);
	for (i = 0; i < f->tf_size; i++) {
		uint_t bit = f->tf_pos + i;

		if (data[bit / 8] & (1 << (bit % 8)))
			v |= 1U << i;
	}
	if ((f->tf_signed || f->tf_rel) && f->tf_size < 32 &&
	    (v & (1U << (f->tf_size - 1))) != 0)
		v |= ~0U << f->tf_size;
	return ((int32_t)v);
}

/* atopcase tpad parser: end */

static mblk_t *
atopcase_tpad_event(mblk_t *chain, ushort_t id, uchar_t pair_type,
    uchar_t pair, int value)
{
	mblk_t *mp;
	Firm_event *fe;

	if ((mp = allocb(sizeof (Firm_event), BPRI_HI)) == NULL)
		return (chain);
	fe = (Firm_event *)mp->b_wptr;
	fe->id = id;
	fe->pair_type = pair_type;
	fe->pair = pair;
	fe->value = value;
	uniqtime32(&fe->time);
	mp->b_wptr += sizeof (Firm_event);
	if (chain != NULL)
		linkb(chain, mp);
	return (chain != NULL ? chain : mp);
}

/* Called by the protocol code with the payload of a REPORT(TPAD) message */
void
atopcase_tpad_report(atopcase_t *sc, const uint8_t *rep, uint16_t len)
{
	atopcase_tpad_t *tp = &sc->as_tpad;
	const atopcase_tpad_layout_t *lo = &tp->at_layout;
	const uint8_t *data = rep;
	int32_t dx, dy, dz;
	uint_t i, buttons = 0;
	mblk_t *chain = NULL, *mp, *next;

	if (!tp->at_parsed || sc->as_polled)
		return;
	if (lo->tl_uses_rid) {
		if (len < 1 || rep[0] != lo->tl_rid)
			return;
		data++;
		len--;
	}
	if ((uint_t)len * 8 < lo->tl_bits) {
		ATOPCASE_DBG(2, "short trackpad report (%u bytes)", len);
		return;
	}

	dx = atopcase_tpad_get(&lo->tl_x, data);
	dy = atopcase_tpad_get(&lo->tl_y, data);
	dz = atopcase_tpad_get(&lo->tl_wheel, data);
	for (i = 0; i < ATOPCASE_TP_NBUTTONS; i++) {
		if (atopcase_tpad_get(&lo->tl_btn[i], data) != 0)
			buttons |= 1U << i;
	}
	ATOPCASE_DBG(3, "tpad dx %d dy %d dz %d buttons %x", dx, dy, dz,
	    buttons);

	mutex_enter(&tp->at_lock);
	if (!tp->at_open || tp->at_format != VUID_FIRM_EVENT) {
		tp->at_buttons = buttons;
		mutex_exit(&tp->at_lock);
		return;
	}

	/* buttons 1-3: MS_LEFT, MS_MIDDLE, MS_RIGHT (HID: left, right, mid) */
	for (i = 0; i < ATOPCASE_TP_NBUTTONS; i++) {
		static const ushort_t vuid[ATOPCASE_TP_NBUTTONS] = {
			MS_LEFT, MS_RIGHT, MS_MIDDLE
		};
		uint_t bit = 1U << i;

		if (((buttons ^ tp->at_buttons) & bit) == 0)
			continue;
		chain = atopcase_tpad_event(chain,
		    vuid_id_addr(tp->at_vuidaddr) | vuid_id_offset(vuid[i]),
		    FE_PAIR_NONE, 0, (buttons & bit) != 0);
	}
	tp->at_buttons = buttons;

	/* usbms: LOC_Y_DELTA is positive upwards, HID Y downwards */
	if (dx != 0)
		chain = atopcase_tpad_event(chain,
		    vuid_id_addr(tp->at_vuidaddr) | vuid_id_offset(LOC_X_DELTA),
		    FE_PAIR_ABSOLUTE, (uchar_t)LOC_X_ABSOLUTE, dx);
	if (dy != 0)
		chain = atopcase_tpad_event(chain,
		    vuid_id_addr(tp->at_vuidaddr) | vuid_id_offset(LOC_Y_DELTA),
		    FE_PAIR_ABSOLUTE, (uchar_t)LOC_Y_ABSOLUTE, -dy);
	if (dz != 0 && tp->at_wheel_enabled)
		chain = atopcase_tpad_event(chain,
		    vuid_id_addr(vuid_first(VUID_WHEEL)) | vuid_id_offset(0),
		    FE_PAIR_NONE, 0, dz);

	/* One message per event, as usbms sends them */
	for (mp = chain; mp != NULL; mp = next) {
		next = mp->b_cont;
		mp->b_cont = NULL;
		if (canputnext(tp->at_rq))
			putnext(tp->at_rq, mp);
		else
			freemsg(mp);
	}
	mutex_exit(&tp->at_lock);
}

/* --- the stream --- */

int
atopcase_tpad_open(atopcase_t *sc, queue_t *qp, int sflag, cred_t *credp)
{
	atopcase_tpad_t *tp = &sc->as_tpad;

	if (sflag != 0)
		return (EINVAL);		/* not a module, no clone */
	if (qp->q_ptr != NULL)
		return (0);
	if (secpolicy_console(credp) != 0)
		return (EPERM);
	if (!tp->at_parsed)
		return (ENXIO);

	mutex_enter(&tp->at_lock);
	if (tp->at_open) {
		mutex_exit(&tp->at_lock);
		return (EBUSY);
	}
	mutex_exit(&tp->at_lock);

	qp->q_ptr = tp;
	WR(qp)->q_ptr = tp;
	qprocson(qp);

	mutex_enter(&tp->at_lock);
	tp->at_rq = qp;
	tp->at_format = VUID_NATIVE;
	tp->at_vuidaddr = VKEY_FIRST;
	tp->at_wheel_enabled = tp->at_layout.tl_wheel.tf_valid;
	tp->at_open = B_TRUE;
	mutex_exit(&tp->at_lock);
	return (0);
}

int
atopcase_tpad_close(queue_t *qp)
{
	atopcase_tpad_t *tp = qp->q_ptr;

	mutex_enter(&tp->at_lock);
	tp->at_open = B_FALSE;
	tp->at_rq = NULL;
	mutex_exit(&tp->at_lock);

	qprocsoff(qp);
	qp->q_ptr = NULL;
	WR(qp)->q_ptr = NULL;
	return (0);
}

/* cp_private of our mcopyin, to tell its M_IOCDATA from an mcopyout's */
#define	TP_COPYIN	((void *)(uintptr_t)1)

/*
 * Answer an ioctl with `len' bytes of `data': in place for I_STR, with
 * mcopyout for a transparent ioctl (mp is then the M_IOCTL or M_IOCDATA).
 */
static void
atopcase_tpad_reply(queue_t *qp, mblk_t *mp, const void *data, size_t len,
    boolean_t transp)
{
	mblk_t *datap;

	if ((datap = allocb(len, BPRI_HI)) == NULL) {
		miocnak(qp, mp, 0, ENOMEM);
		return;
	}
	bcopy(data, datap->b_wptr, len);
	datap->b_wptr += len;
	if (transp) {
		mcopyout(mp, NULL, len, NULL, datap);
		qreply(qp, mp);
		return;
	}
	if (mp->b_cont != NULL)
		freemsg(mp->b_cont);
	mp->b_cont = datap;
	miocack(qp, mp, len, 0);
}

/* The size of an ioctl's input, 0 for none. */
static size_t
atopcase_tpad_insize(int cmd)
{
	switch (cmd) {
	case VUIDSFORMAT:
		return (sizeof (int));
	case VUIDSADDR:
	case VUIDGADDR:
		return (sizeof (Vuid_addr_probe));
	case MSIOSETPARMS:
		return (sizeof (Ms_parms));
	case VUIDGWHEELINFO:
		return (sizeof (wheel_info));
	case VUIDGWHEELSTATE:
	case VUIDSWHEELSTATE:
		return (sizeof (wheel_state));
	case MSIOSRESOLUTION:
		return (sizeof (Ms_screen_resolution));
	default:
		return (0);
	}
}

/* Carry out an ioctl whose input (if any) is in `in'. */
static void
atopcase_tpad_do_ioctl(atopcase_tpad_t *tp, queue_t *qp, mblk_t *mp, int cmd,
    const void *in, boolean_t transp)
{
	int ival;

	switch (cmd) {
	case VUIDSFORMAT:
		ival = *(const int *)in;
		if (ival != VUID_NATIVE && ival != VUID_FIRM_EVENT) {
			miocnak(qp, mp, 0, EINVAL);
			return;
		}
		mutex_enter(&tp->at_lock);
		tp->at_format = ival;
		mutex_exit(&tp->at_lock);
		miocack(qp, mp, 0, 0);
		return;

	case VUIDGFORMAT:
		ival = tp->at_format;
		atopcase_tpad_reply(qp, mp, &ival, sizeof (ival), transp);
		return;

	case VUIDSADDR:
	case VUIDGADDR: {
		Vuid_addr_probe ap;

		bcopy(in, &ap, sizeof (ap));
		if (ap.base != VKEY_FIRST) {
			miocnak(qp, mp, 0, ENODEV);
			return;
		}
		if (cmd == VUIDSADDR) {
			tp->at_vuidaddr = ap.data.next;
			miocack(qp, mp, 0, 0);
		} else {
			ap.data.current = tp->at_vuidaddr;
			atopcase_tpad_reply(qp, mp, &ap, sizeof (ap), transp);
		}
		return;
	}

	case MSIOBUTTONS:
		ival = tp->at_nbuttons;
		atopcase_tpad_reply(qp, mp, &ival, sizeof (ival), transp);
		return;

	case MSIOGETPARMS: {
		Ms_parms parms;

		bzero(&parms, sizeof (parms));
		atopcase_tpad_reply(qp, mp, &parms, sizeof (parms), transp);
		return;
	}

	case MSIOSETPARMS:
	case MSIOSRESOLUTION:
		/* nothing to set: relative deltas only */
		miocack(qp, mp, 0, 0);
		return;

	case VUIDGWHEELCOUNT:
		ival = tp->at_layout.tl_wheel.tf_valid ? 1 : 0;
		atopcase_tpad_reply(qp, mp, &ival, sizeof (ival), transp);
		return;

	case VUIDGWHEELINFO: {
		wheel_info wi;

		bcopy(in, &wi, sizeof (wi));
		if (wi.vers != VUID_WHEEL_INFO_VERS || wi.id != 0 ||
		    !tp->at_layout.tl_wheel.tf_valid) {
			miocnak(qp, mp, 0, EINVAL);
			return;
		}
		wi.format = VUID_WHEEL_FORMAT_VERTICAL;
		atopcase_tpad_reply(qp, mp, &wi, sizeof (wi), transp);
		return;
	}

	case VUIDGWHEELSTATE:
	case VUIDSWHEELSTATE: {
		wheel_state ws;

		bcopy(in, &ws, sizeof (ws));
		if (ws.vers != VUID_WHEEL_STATE_VERS || ws.id != 0 ||
		    !tp->at_layout.tl_wheel.tf_valid) {
			miocnak(qp, mp, 0, EINVAL);
			return;
		}
		if (cmd == VUIDSWHEELSTATE) {
			mutex_enter(&tp->at_lock);
			tp->at_wheel_enabled =
			    (ws.stateflags & VUID_WHEEL_STATE_ENABLED) != 0;
			mutex_exit(&tp->at_lock);
			miocack(qp, mp, 0, 0);
		} else {
			ws.stateflags = tp->at_wheel_enabled ?
			    VUID_WHEEL_STATE_ENABLED : 0;
			atopcase_tpad_reply(qp, mp, &ws, sizeof (ws), transp);
		}
		return;
	}

	default:
		miocnak(qp, mp, 0, EINVAL);
		return;
	}
}

static void
atopcase_tpad_ioctl(atopcase_tpad_t *tp, queue_t *qp, mblk_t *mp)
{
	struct iocblk *iocp = (struct iocblk *)mp->b_rptr;
	size_t insize = atopcase_tpad_insize(iocp->ioc_cmd);
	boolean_t transp = iocp->ioc_count == TRANSPARENT;
	int err;

	if (insize == 0) {
		atopcase_tpad_do_ioctl(tp, qp, mp, iocp->ioc_cmd, NULL, transp);
		return;
	}
	if (transp) {
		/* fetch the argument first; carried on in M_IOCDATA */
		mcopyin(mp, TP_COPYIN, insize, NULL);
		qreply(qp, mp);
		return;
	}
	if ((err = miocpullup(mp, insize)) != 0) {
		miocnak(qp, mp, 0, err);
		return;
	}
	atopcase_tpad_do_ioctl(tp, qp, mp, iocp->ioc_cmd, mp->b_cont->b_rptr,
	    B_FALSE);
}

static void
atopcase_tpad_iocdata(atopcase_tpad_t *tp, queue_t *qp, mblk_t *mp)
{
	struct copyresp *csp = (struct copyresp *)mp->b_rptr;
	size_t insize = atopcase_tpad_insize(csp->cp_cmd);
	uint8_t in[sizeof (Ms_parms) + sizeof (wheel_info) + 32];
	int err;

	if (csp->cp_rval != 0) {		/* the copy failed */
		freemsg(mp);
		return;
	}
	if (csp->cp_private != TP_COPYIN) {	/* our mcopyout is done */
		miocack(qp, mp, 0, 0);
		return;
	}
	/* the argument of a transparent ioctl */
	if (insize == 0 || insize > sizeof (in) ||
	    (err = miocpullup(mp, insize)) != 0) {
		miocnak(qp, mp, 0, insize == 0 || insize > sizeof (in) ?
		    EINVAL : err);
		return;
	}
	bcopy(mp->b_cont->b_rptr, in, insize);
	csp->cp_private = NULL;
	atopcase_tpad_do_ioctl(tp, qp, mp, csp->cp_cmd, in, B_TRUE);
}

int
atopcase_tpad_wsrv(queue_t *qp)
{
	atopcase_tpad_t *tp = qp->q_ptr;
	mblk_t *mp;

	while ((mp = getq(qp)) != NULL) {
		switch (mp->b_datap->db_type) {
		case M_IOCTL:
			atopcase_tpad_ioctl(tp, qp, mp);
			continue;
		case M_IOCDATA:
			atopcase_tpad_iocdata(tp, qp, mp);
			continue;
		case M_FLUSH:
			*mp->b_rptr &= ~FLUSHW;
			if (*mp->b_rptr & FLUSHR)
				qreply(qp, mp);
			else
				freemsg(mp);
			continue;
		default:
			freemsg(mp);
			continue;
		}
	}
	return (0);
}

void
atopcase_tpad_init(atopcase_t *sc)
{
	mutex_init(&sc->as_tpad.at_lock, NULL, MUTEX_DRIVER, NULL);
}

/*
 * After atopcase_proto_init: parse the trackpad descriptor.  B_TRUE when
 * the trackpad has a relative mouse report, and the mouse node can be made.
 */
boolean_t
atopcase_tpad_setup(atopcase_t *sc)
{
	atopcase_tpad_t *tp = &sc->as_tpad;
	uint_t i;

	tp->at_parsed = sc->as_tp.acc_rdesc_len > 0 &&
	    atopcase_tpad_parse(tp, sc->as_tp.acc_rdesc,
	    sc->as_tp.acc_rdesc_len);
	if (!tp->at_parsed) {
		dev_err(sc->as_dip, CE_NOTE, "!trackpad: no relative X/Y "
		    "report in its descriptor (%u bytes); no mouse node",
		    sc->as_tp.acc_rdesc_len);
		return (B_FALSE);
	}
	ATOPCASE_DBG(1, "trackpad: report id %u (%s), %u bits: x %u/%u "
	    "y %u/%u wheel %s, %u buttons", tp->at_layout.tl_rid,
	    tp->at_layout.tl_uses_rid ? "used" : "none",
	    tp->at_layout.tl_bits, tp->at_layout.tl_x.tf_pos,
	    tp->at_layout.tl_x.tf_size, tp->at_layout.tl_y.tf_pos,
	    tp->at_layout.tl_y.tf_size,
	    tp->at_layout.tl_wheel.tf_valid ? "yes" : "no", tp->at_nbuttons);
	for (i = 0; i < ATOPCASE_TP_NBUTTONS; i++) {
		if (tp->at_layout.tl_btn[i].tf_valid)
			ATOPCASE_DBG(2, "trackpad: button %u at bit %u", i + 1,
			    tp->at_layout.tl_btn[i].tf_pos);
	}
	return (B_TRUE);
}

void
atopcase_tpad_fini(atopcase_t *sc)
{
	mutex_destroy(&sc->as_tpad.at_lock);
}
