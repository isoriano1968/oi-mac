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
 *
 * The STREAMS structure, the console ioctls and the polled-I/O wiring
 * follow illumos' kb8042 (usr/src/uts/common/io/kb8042/kb8042.c) and
 * the report unpacking follows usbkbm
 * (usr/src/uts/common/io/usb/clients/usbkbm/usbkbm.c), both CDDL,
 * Copyright Sun Microsystems / Oracle.
 */

/*
 * The topcase keyboard as an illumos console keyboard.
 *
 * The minor node is DDI_NT_KEYBOARD, so /etc/dacf.conf's
 * minor-nodetype="ddi_keyboard" rule makes consconfig open it and link
 * it under conskbd, as it does for kb8042; no dacf.conf change is
 * needed.  Key usages are USB HID usages, so kbtrans runs with the USB
 * keymap (KB_USB), and with KB_PC (X in raw mode) they are translated
 * to PC scancodes as usbkbm does.
 *
 * Fn layer: the MacBook Pros with a Touch Bar have no physical Esc or
 * F1-F12, and the Touch Bar needs its own (USB) driver.  Until that
 * exists, Fn plus the digit row gives F1-F12 and Fn+` gives Esc.  The
 * usual Apple Fn combinations are added too: Fn+Backspace = Delete,
 * Fn+Return = Insert, Fn+arrows = Home/End/PageUp/PageDown.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/kbd.h>
#include <sys/kbio.h>
#include <sys/consdev.h>
#include <sys/policy.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kbtrans.h>

#include "atopcase.h"

extern struct keyboard *kbtrans_usbkb_maptab_init(void);
extern void kbtrans_usbkb_maptab_fini(struct keyboard **);
extern keymap_entry_t kbtrans_keycode_usb2pc(int);

static struct keyboard *atopcase_keyindex;

/* USB HID usages (HID Usage Tables, keyboard page) */
#define	U_1		0x1E
#define	U_0		0x27
#define	U_RETURN	0x28
#define	U_ESC		0x29
#define	U_BACKSPACE	0x2A
#define	U_MINUS		0x2D
#define	U_EQUAL		0x2E
#define	U_GRAVE		0x35
#define	U_F1		0x3A
#define	U_F11		0x44
#define	U_F12		0x45
#define	U_INSERT	0x49
#define	U_HOME		0x4A
#define	U_PGUP		0x4B
#define	U_DELETE	0x4C
#define	U_END		0x4D
#define	U_PGDN		0x4E
#define	U_RIGHT		0x4F
#define	U_LEFT		0x50
#define	U_DOWN		0x51
#define	U_UP		0x52
#define	U_ERRORROLLOVER	0x01

/* Modifier bits of the report and their usages (usbkbm.h) */
static const struct {
	uint8_t	bit;
	uint8_t	usage;
} atopcase_mods[] = {
	{ 0x01, 224 },	/* left control */
	{ 0x02, 225 },	/* left shift */
	{ 0x04, 226 },	/* left alt (option) */
	{ 0x08, 227 },	/* left meta (command) */
	{ 0x10, 228 },	/* right control */
	{ 0x20, 229 },	/* right shift */
	{ 0x40, 230 },	/* right alt (option) */
	{ 0x80, 231 },	/* right meta (command) */
};

/* Fn+key: 0 = no Fn meaning, the key is passed on unchanged */
int atopcase_fn_layer = 1;

static uint8_t
atopcase_fn_map(uint8_t u)
{
	if (u >= U_1 && u <= U_0)
		return (U_F1 + (u - U_1));	/* F1-F10 */
	switch (u) {
	case U_MINUS:		return (U_F11);
	case U_EQUAL:		return (U_F12);
	case U_GRAVE:		return (U_ESC);
	case U_BACKSPACE:	return (U_DELETE);
	case U_RETURN:		return (U_INSERT);
	case U_LEFT:		return (U_HOME);
	case U_RIGHT:		return (U_END);
	case U_UP:		return (U_PGUP);
	case U_DOWN:		return (U_PGDN);
	default:		return (u);
	}
}

typedef void (*atopcase_key_cb_t)(atopcase_kbd_t *, int, enum keystate);

static void
atopcase_key_streams(atopcase_kbd_t *kb, int key, enum keystate state)
{
	if (kb->ak_vkbd_type == KB_PC)
		key = kbtrans_keycode_usb2pc(key);
	kbtrans_streams_key(kb->ak_kbtrans, key, state);
}

static void
atopcase_key_polled(atopcase_kbd_t *kb, int key, enum keystate state)
{
	uint_t next = (kb->ak_pbuf_tail + 1) % 32;

	if (next == kb->ak_pbuf_head)
		return;		/* buffer full: drop */
	kb->ak_pbuf_key[kb->ak_pbuf_tail] = key;
	kb->ak_pbuf_state[kb->ak_pbuf_tail] = state;
	kb->ak_pbuf_tail = next;
}

/*
 * Turn a report into key press and release events: modifiers by bit,
 * keys by comparing the six usages with the previous report (as
 * usbkbm_unpack_usb_packet).  A key's Fn meaning is chosen when it is
 * pressed and remembered, so its release matches even if Fn is let go
 * first.
 */
static void
atopcase_unpack(atopcase_kbd_t *kb, const uint8_t *rep, atopcase_key_cb_t cb)
{
	uint8_t mods = rep[ATOPCASE_KBD_MODS];
	const uint8_t *keys = rep + ATOPCASE_KBD_KEYS;
	boolean_t fn = (rep[ATOPCASE_KBD_FN] != 0);
	uint8_t emit[ATOPCASE_KBD_NKEYS];
	int i, j, m;

	/* All six slots ErrorRollOver: keep the previous state */
	for (i = 0; i < ATOPCASE_KBD_NKEYS; i++)
		if (keys[i] != U_ERRORROLLOVER)
			break;
	if (i == ATOPCASE_KBD_NKEYS)
		return;

	for (m = 0; m < sizeof (atopcase_mods) / sizeof (atopcase_mods[0]);
	    m++) {
		uint8_t bit = atopcase_mods[m].bit;

		if ((mods & bit) != (kb->ak_last_mods & bit))
			cb(kb, atopcase_mods[m].usage,
			    (mods & bit) ? KEY_PRESSED : KEY_RELEASED);
	}
	kb->ak_last_mods = mods;

	/* Releases: previous keys no longer in the report */
	for (j = 0; j < ATOPCASE_KBD_NKEYS; j++) {
		if (kb->ak_last_raw[j] == 0)
			continue;
		for (i = 0; i < ATOPCASE_KBD_NKEYS; i++)
			if (keys[i] == kb->ak_last_raw[j])
				break;
		if (i == ATOPCASE_KBD_NKEYS)
			cb(kb, kb->ak_last_emit[j], KEY_RELEASED);
	}

	/* Presses: keys not in the previous report */
	for (i = 0; i < ATOPCASE_KBD_NKEYS; i++) {
		emit[i] = 0;
		if (keys[i] == 0 || keys[i] == U_ERRORROLLOVER)
			continue;
		for (j = 0; j < ATOPCASE_KBD_NKEYS; j++)
			if (keys[i] == kb->ak_last_raw[j])
				break;
		if (j < ATOPCASE_KBD_NKEYS) {
			emit[i] = kb->ak_last_emit[j];	/* still held */
			continue;
		}
		/* modifier usages also come as bits: ignore them here */
		if (keys[i] >= 224 && keys[i] <= 231)
			continue;
		emit[i] = (fn && atopcase_fn_layer) ?
		    atopcase_fn_map(keys[i]) : keys[i];
		cb(kb, emit[i], KEY_PRESSED);
	}

	for (i = 0; i < ATOPCASE_KBD_NKEYS; i++) {
		kb->ak_last_raw[i] = keys[i];
		kb->ak_last_emit[i] = emit[i];
	}
}

/* Called by the protocol code with the payload of a REPORT(KBRD) message */
void
atopcase_kbd_report(atopcase_t *sc, const uint8_t *rep, uint16_t len)
{
	atopcase_kbd_t *kb = &sc->as_kbd;

	if (len < ATOPCASE_KBD_REPORT_LEN) {
		ATOPCASE_DBG(1, "short keyboard report (%u bytes)", len);
		return;
	}
	ATOPCASE_DBG(3, "kbd report %02x %02x %02x %02x %02x %02x %02x %02x "
	    "%02x %02x", rep[0], rep[1], rep[2], rep[3], rep[4], rep[5],
	    rep[6], rep[7], rep[8], rep[9]);

	if (sc->as_polled) {
		atopcase_unpack(kb, rep, atopcase_key_polled);
		return;
	}

	mutex_enter(&kb->ak_lock);
	if (kb->ak_open && kb->ak_kbtrans != NULL)
		atopcase_unpack(kb, rep, atopcase_key_streams);
	mutex_exit(&kb->ak_lock);
}

/* kbtrans LED_* bits to the HID output report bits */
int
atopcase_kbd_leds_to_hid(int leds)
{
	int hid = 0;

	if (leds & LED_NUM_LOCK)
		hid |= ATOPCASE_HID_LED_NUM;
	if (leds & LED_CAPS_LOCK)
		hid |= ATOPCASE_HID_LED_CAPS;
	return (hid);
}

/* --- kbtrans callbacks --- */

static void
atopcase_streams_setled(struct kbtrans_hardware *hw, int leds)
{
	atopcase_t *sc = (atopcase_t *)hw;

	/* Called with ak_lock possibly held: only hand it to the worker */
	atopcase_request_leds(sc, leds);
}

static void
atopcase_polled_setled(struct kbtrans_hardware *hw, int leds)
{
	/* No SPI writes from kmdb */
}

static boolean_t
atopcase_polled_keycheck(struct kbtrans_hardware *hw, kbtrans_key_t *key,
    enum keystate *state)
{
	atopcase_t *sc = (atopcase_t *)hw;
	atopcase_kbd_t *kb = &sc->as_kbd;

	if (kb->ak_pbuf_head == kb->ak_pbuf_tail) {
		sc->as_polled = B_TRUE;
		(void) atopcase_receive_packet(sc);
		sc->as_polled = B_FALSE;
	}
	if (kb->ak_pbuf_head == kb->ak_pbuf_tail)
		return (B_FALSE);

	*key = kb->ak_pbuf_key[kb->ak_pbuf_head];
	*state = kb->ak_pbuf_state[kb->ak_pbuf_head];
	kb->ak_pbuf_head = (kb->ak_pbuf_head + 1) % 32;
	return (B_TRUE);
}

static struct kbtrans_callbacks atopcase_callbacks = {
	atopcase_streams_setled,
	atopcase_polled_setled,
	atopcase_polled_keycheck,
};

static boolean_t
atopcase_polled_ischar(cons_polledio_arg_t arg)
{
	atopcase_t *sc = (atopcase_t *)arg;

	return (kbtrans_ischar(sc->as_kbd.ak_kbtrans));
}

static int
atopcase_polled_getchar(cons_polledio_arg_t arg)
{
	atopcase_t *sc = (atopcase_t *)arg;

	return (kbtrans_getchar(sc->as_kbd.ak_kbtrans));
}

/* --- STREAMS --- */

static int
atopcase_open(queue_t *qp, dev_t *devp, int flag, int sflag, cred_t *credp)
{
	atopcase_t *sc = atopcase_state;
	atopcase_kbd_t *kb;
	int err;

	if (sc == NULL || !sc->as_minor_created)
		return (ENXIO);
	if (getminor(*devp) == ATOPCASE_MINOR_MOUSE)
		return (atopcase_tpad_open(sc, qp, sflag, credp));
	if (getminor(*devp) != ATOPCASE_MINOR_KBD)
		return (ENXIO);
	kb = &sc->as_kbd;

	if (qp->q_ptr != NULL)
		return (0);

	if (secpolicy_console(credp) != 0)
		return (EPERM);

	mutex_enter(&kb->ak_lock);
	if (kb->ak_open) {
		mutex_exit(&kb->ak_lock);
		return (EBUSY);
	}
	mutex_exit(&kb->ak_lock);

	qp->q_ptr = sc;
	WR(qp)->q_ptr = sc;

	err = kbtrans_streams_init(qp, sflag, (struct kbtrans_hardware *)sc,
	    &atopcase_callbacks, &kb->ak_kbtrans, 0, 0);
	if (err != 0) {
		qp->q_ptr = NULL;
		WR(qp)->q_ptr = NULL;
		return (err);
	}
	kbtrans_streams_set_keyboard(kb->ak_kbtrans, KB_USB,
	    atopcase_keyindex);

	kb->ak_polledio.cons_polledio_version = CONSPOLLEDIO_V1;
	kb->ak_polledio.cons_polledio_argument = (cons_polledio_arg_t)sc;
	kb->ak_polledio.cons_polledio_putchar = NULL;
	kb->ak_polledio.cons_polledio_getchar =
	    (int (*)(cons_polledio_arg_t))atopcase_polled_getchar;
	kb->ak_polledio.cons_polledio_ischar =
	    (boolean_t (*)(cons_polledio_arg_t))atopcase_polled_ischar;
	kb->ak_polledio.cons_polledio_enter = NULL;
	kb->ak_polledio.cons_polledio_exit = NULL;
	/* conskbd multiplexes polled input through keycheck (as kb8042) */
	kb->ak_polledio.cons_polledio_setled =
	    (void (*)(cons_polledio_arg_t, int))atopcase_polled_setled;
	kb->ak_polledio.cons_polledio_keycheck =
	    (boolean_t (*)(cons_polledio_arg_t, int *,
	    enum keystate *))atopcase_polled_keycheck;

	qprocson(qp);

	mutex_enter(&kb->ak_lock);
	kb->ak_rq = qp;
	kb->ak_open = B_TRUE;
	kb->ak_last_mods = 0;
	bzero(kb->ak_last_raw, sizeof (kb->ak_last_raw));
	bzero(kb->ak_last_emit, sizeof (kb->ak_last_emit));
	mutex_exit(&kb->ak_lock);

	kbtrans_streams_enable(kb->ak_kbtrans);
	return (0);
}

static int
atopcase_close(queue_t *qp, int flag, cred_t *credp)
{
	atopcase_t *sc;
	atopcase_kbd_t *kb;
	struct kbtrans *kbt;

	/* The mouse stream's q_ptr is the trackpad state, not the softc */
	if (atopcase_state != NULL && qp->q_ptr == &atopcase_state->as_tpad)
		return (atopcase_tpad_close(qp));
	sc = qp->q_ptr;
	kb = &sc->as_kbd;

	mutex_enter(&kb->ak_lock);
	kb->ak_open = B_FALSE;
	kb->ak_rq = NULL;
	kbt = kb->ak_kbtrans;
	kb->ak_kbtrans = NULL;
	mutex_exit(&kb->ak_lock);

	/* kb8042's order: kbtrans first, then the queue */
	if (kbt != NULL)
		(void) kbtrans_streams_fini(kbt);
	qprocsoff(qp);
	qp->q_ptr = NULL;
	WR(qp)->q_ptr = NULL;
	return (0);
}

static void
atopcase_ioctl(atopcase_t *sc, queue_t *qp, mblk_t *mp)
{
	atopcase_kbd_t *kb = &sc->as_kbd;
	struct iocblk *iocp = (struct iocblk *)mp->b_rptr;
	mblk_t *datap;
	int err, tmp;

	switch (iocp->ioc_cmd) {
	case CONSOPENPOLLEDIO:
		err = miocpullup(mp, sizeof (struct cons_polledio *));
		if (err != 0) {
			miocnak(qp, mp, 0, err);
			return;
		}
		*(struct cons_polledio **)mp->b_cont->b_rptr = &kb->ak_polledio;
		mp->b_datap->db_type = M_IOCACK;
		iocp->ioc_error = 0;
		qreply(qp, mp);
		return;

	case CONSCLOSEPOLLEDIO:
		miocack(qp, mp, 0, 0);
		return;

	case CONSSETABORTENABLE:
		miocack(qp, mp, 0, 0);
		return;

	case CONSSETKBDTYPE:
		err = miocpullup(mp, sizeof (int));
		if (err != 0) {
			miocnak(qp, mp, 0, err);
			return;
		}
		tmp = *(int *)mp->b_cont->b_rptr;
		if (tmp != KB_PC && tmp != KB_USB) {
			miocnak(qp, mp, 0, EINVAL);
			return;
		}
		kb->ak_vkbd_type = tmp;
		miocack(qp, mp, 0, 0);
		return;

	case KIOCLAYOUT:
		if ((datap = allocb(sizeof (int), BPRI_HI)) == NULL) {
			miocnak(qp, mp, 0, ENOMEM);
			return;
		}
		*(int *)datap->b_wptr = kb->ak_layout;
		datap->b_wptr += sizeof (int);
		if (mp->b_cont != NULL)
			freemsg(mp->b_cont);
		mp->b_cont = datap;
		iocp->ioc_count = sizeof (int);
		mp->b_datap->db_type = M_IOCACK;
		iocp->ioc_error = 0;
		qreply(qp, mp);
		return;

	case KIOCSLAYOUT:
		if (iocp->ioc_count != TRANSPARENT) {
			miocnak(qp, mp, 0, EINVAL);
			return;
		}
		kb->ak_layout = (int)*(intptr_t *)mp->b_cont->b_rptr;
		miocack(qp, mp, 0, 0);
		return;

	case KIOCCMD:
		/* bell and click: nothing to do on this keyboard */
		miocack(qp, mp, 0, 0);
		return;

	default:
		miocnak(qp, mp, 0, EINVAL);
		return;
	}
}

static int
atopcase_wsrv(queue_t *qp)
{
	atopcase_t *sc = qp->q_ptr;
	mblk_t *mp;

	if (atopcase_state != NULL && qp->q_ptr == &atopcase_state->as_tpad)
		return (atopcase_tpad_wsrv(qp));

	while ((mp = getq(qp)) != NULL) {
		if (sc->as_kbd.ak_kbtrans != NULL &&
		    kbtrans_streams_message(sc->as_kbd.ak_kbtrans, mp) ==
		    KBTRANS_MESSAGE_HANDLED)
			continue;

		switch (mp->b_datap->db_type) {
		case M_IOCTL:
			atopcase_ioctl(sc, qp, mp);
			continue;
		case M_IOCDATA:
			miocnak(qp, mp, 0, EINVAL);
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

static int
atopcase_rsrv(queue_t *qp)
{
	mblk_t *mp;

	while ((mp = getq(qp)) != NULL)
		freemsg(mp);
	return (0);
}

static struct module_info atopcase_minfo = {
	.mi_idnum = 0x6174,
	.mi_idname = "atopcase",
	.mi_minpsz = 0,
	.mi_maxpsz = 32,
	.mi_hiwat = 256,
	.mi_lowat = 128
};

static struct qinit atopcase_rinit = {
	.qi_putp = NULL,
	.qi_srvp = atopcase_rsrv,
	.qi_qopen = atopcase_open,
	.qi_qclose = atopcase_close,
	.qi_minfo = &atopcase_minfo,
};

static struct qinit atopcase_winit = {
	.qi_putp = putq,
	.qi_srvp = atopcase_wsrv,
	.qi_qopen = atopcase_open,
	.qi_qclose = atopcase_close,
	.qi_minfo = &atopcase_minfo,
};

struct streamtab atopcase_str_info = {
	.st_rdinit = &atopcase_rinit,
	.st_wrinit = &atopcase_winit,
	.st_muxrinit = NULL,
	.st_muxwinit = NULL
};

void
atopcase_kbd_init(atopcase_t *sc)
{
	atopcase_kbd_t *kb = &sc->as_kbd;

	mutex_init(&kb->ak_lock, NULL, MUTEX_DRIVER, NULL);
	kb->ak_vkbd_type = KB_USB;
	kb->ak_layout = KBTRANS_USBKB_DEFAULT_LAYOUT;
	if (atopcase_keyindex == NULL)
		atopcase_keyindex = kbtrans_usbkb_maptab_init();
}

void
atopcase_kbd_fini(atopcase_t *sc)
{
	mutex_destroy(&sc->as_kbd.ak_lock);
	if (atopcase_keyindex != NULL)
		kbtrans_usbkb_maptab_fini(&atopcase_keyindex);
}
