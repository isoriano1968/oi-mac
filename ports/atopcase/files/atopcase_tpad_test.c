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
 * Userland unit test of the atopcase trackpad HID parser: the Makefile cuts
 * the block between "atopcase tpad parser: begin" and "end" out of
 * src/atopcase_tpad.c into build/atopcase_tpad_parser.h, so this tests the
 * code the driver runs.  Descriptors: the HID 1.11 boot mouse (no report
 * id), a mouse with report id 2 and a wheel, and a descriptor without a
 * relative X/Y (rejected).
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#define	ATOPCASE_DBG(lvl, ...)	do { } while (0)
#define	ATOPCASE_TP_NBUTTONS	3

typedef struct atopcase_tpad_field {
	boolean_t	tf_valid, tf_rel, tf_signed;
	uint8_t		tf_rid, tf_size;
	uint16_t	tf_pos;
} atopcase_tpad_field_t;

typedef struct atopcase_tpad_layout {
	boolean_t		tl_uses_rid;
	uint8_t			tl_rid;
	uint16_t		tl_bits;
	atopcase_tpad_field_t	tl_x, tl_y, tl_wheel;
	atopcase_tpad_field_t	tl_btn[ATOPCASE_TP_NBUTTONS];
} atopcase_tpad_layout_t;

typedef struct atopcase_tpad {
	atopcase_tpad_layout_t	at_layout;
	uint_t			at_nbuttons;
} atopcase_tpad_t;

#include "atopcase_tpad_parser.h"

static int failures, tests;

#define	CHECK(cond)	do {						\
	tests++;							\
	if (!(cond)) {							\
		(void) printf("FAIL line %d: %s\n", __LINE__, #cond);	\
		failures++;						\
	}								\
} while (0)

/* HID 1.11 appendix B.2: boot protocol mouse, 3 buttons, X/Y 8 bit */
static const uint8_t boot_mouse[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
	0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
	0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81,
	0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xc0, 0xc0
};

/*
 * Report id 2: 2 buttons, 6 bits padding, X/Y 12 bit (logical -2047..2047,
 * 2-byte items), wheel 8 bit; plus a feature report (id 3) that must not
 * move the input offsets.
 */
static const uint8_t rid_mouse[] = {
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09, 0x01,
	0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00,
	0x25, 0x01, 0x95, 0x02, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
	0x75, 0x06, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
	0x16, 0x01, 0xf8, 0x26, 0xff, 0x07, 0x75, 0x0c, 0x95, 0x02,
	0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08,
	0x95, 0x01, 0x81, 0x06, 0xc0, 0x85, 0x03, 0x06, 0x00, 0xff,
	0x09, 0x01, 0x75, 0x08, 0x95, 0x02, 0xb1, 0x02, 0xc0
};

/* Absolute X/Y (a touch screen): no mouse report */
static const uint8_t abs_pointer[] = {
	0x05, 0x01, 0x09, 0x01, 0xa1, 0x01, 0x05, 0x01, 0x09, 0x30,
	0x09, 0x31, 0x15, 0x00, 0x26, 0xff, 0x0f, 0x75, 0x10, 0x95,
	0x02, 0x81, 0x02, 0xc0
};

int
main(void)
{
	atopcase_tpad_t tp;
	uint8_t rep[8];

	/* boot mouse */
	memset(&tp, 0, sizeof (tp));
	CHECK(atopcase_tpad_parse(&tp, boot_mouse, sizeof (boot_mouse)));
	CHECK(!tp.at_layout.tl_uses_rid);
	CHECK(tp.at_layout.tl_bits == 24);
	CHECK(tp.at_nbuttons == 3);
	CHECK(tp.at_layout.tl_btn[0].tf_pos == 0 &&
	    tp.at_layout.tl_btn[2].tf_pos == 2);
	CHECK(tp.at_layout.tl_x.tf_pos == 8 && tp.at_layout.tl_x.tf_size == 8);
	CHECK(tp.at_layout.tl_y.tf_pos == 16);
	CHECK(!tp.at_layout.tl_wheel.tf_valid);
	rep[0] = 0x05; rep[1] = 0xfb; rep[2] = 0x07;	/* L+M, -5, +7 */
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_btn[0], rep) == 1);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_btn[1], rep) == 0);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_btn[2], rep) == 1);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_x, rep) == -5);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_y, rep) == 7);

	/* report id 2, 12-bit X/Y, wheel, feature report ignored */
	memset(&tp, 0, sizeof (tp));
	CHECK(atopcase_tpad_parse(&tp, rid_mouse, sizeof (rid_mouse)));
	CHECK(tp.at_layout.tl_uses_rid && tp.at_layout.tl_rid == 2);
	CHECK(tp.at_layout.tl_bits == 8 + 24 + 8);
	CHECK(tp.at_nbuttons == 2);
	CHECK(tp.at_layout.tl_x.tf_pos == 8 && tp.at_layout.tl_x.tf_size == 12);
	CHECK(tp.at_layout.tl_y.tf_pos == 20);
	CHECK(tp.at_layout.tl_x.tf_signed && tp.at_layout.tl_x.tf_rel);
	CHECK(tp.at_layout.tl_wheel.tf_valid &&
	    tp.at_layout.tl_wheel.tf_pos == 32);
	/* data after the id: btn1, x = -300 (0xed4), y = +1000 (0x3e8), z -1 */
	rep[0] = 0x01;
	rep[1] = 0xd4; rep[2] = 0x8e; rep[3] = 0x3e;
	rep[4] = 0xff;
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_btn[0], rep) == 1);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_x, rep) == -300);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_y, rep) == 1000);
	CHECK(atopcase_tpad_get(&tp.at_layout.tl_wheel, rep) == -1);

	/* absolute pointer: rejected */
	memset(&tp, 0, sizeof (tp));
	CHECK(!atopcase_tpad_parse(&tp, abs_pointer, sizeof (abs_pointer)));
	/* truncated and empty descriptors: rejected, no overrun */
	memset(&tp, 0, sizeof (tp));
	CHECK(!atopcase_tpad_parse(&tp, boot_mouse, 30));
	CHECK(!atopcase_tpad_parse(&tp, boot_mouse, 0));

	(void) printf("atopcase tpad parser: %d tests, %d failures\n", tests,
	    failures);
	return (failures == 0 ? 0 : 1);
}
