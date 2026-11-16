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
 * Userland unit test of appletb_find_usage(): the Makefile cuts the block
 * between "appletb hid lookup: begin" and "end" out of src/appletb.c into
 * build/appletb_hid_lookup.h.  The descriptor is built like the Touch Bar's
 * (the real one, 634 bytes, is to be read on the machine): a keyboard
 * application collection with an input report (id 1) and the mode output
 * report (id 2, usage page 0x00ff usage 4), and a vendor collection
 * 0xff12:0x01 with the display feature report (id 3, usage 0x21) and a
 * 64-bit field as in the real descriptor.
 */

#include <sys/types.h>
#include <stdio.h>
#include <strings.h>

#define	REPORT_INPUT	1
#define	REPORT_OUTPUT	2
#define	REPORT_FEATURE	3

typedef struct appletb_field {
	boolean_t	af_found;
	uint8_t		af_rid;
	uint8_t		af_type;
} appletb_field_t;

#include "appletb_hid_lookup.h"

static int failures, tests;

#define	CHECK(cond)	do {						\
	tests++;							\
	if (!(cond)) {							\
		(void) printf("FAIL line %d: %s\n", __LINE__, #cond);	\
		failures++;						\
	}								\
} while (0)

static const uint8_t tb[] = {
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,		/* GD Keyboard app */
	0x85, 0x01,					/* report id 1 */
	0x05, 0x07, 0x19, 0x00, 0x29, 0x65,		/* keys 0-0x65 */
	0x15, 0x00, 0x25, 0x65, 0x75, 0x08, 0x95, 0x06,
	0x81, 0x00,					/* input array */
	0x85, 0x02,					/* report id 2 */
	0x05, 0xff, 0x09, 0x04,				/* 0x00ff:0004 mode */
	0x15, 0x00, 0x25, 0x03, 0x75, 0x08, 0x95, 0x01,
	0x91, 0x02,					/* output */
	0xc0,
	0x06, 0x12, 0xff, 0x09, 0x01, 0xa1, 0x01,	/* 0xff12:0001 app */
	0x85, 0x03,
	0x09, 0x51, 0x75, 0x40, 0x95, 0x01, 0xb1, 0x02,	/* 64-bit field */
	0x09, 0x21, 0x75, 0x08, 0x95, 0x0a, 0xb1, 0x02,	/* display */
	0xc0
};

int
main(void)
{
	appletb_field_t f;

	CHECK(appletb_find_usage(tb, sizeof (tb), 0x00010006, 0x00ff0004, &f));
	CHECK(f.af_found && f.af_rid == 2 && f.af_type == REPORT_OUTPUT);

	CHECK(appletb_find_usage(tb, sizeof (tb), 0xff120001, 0xff120021, &f));
	CHECK(f.af_found && f.af_rid == 3 && f.af_type == REPORT_FEATURE);

	/* a usage outside the asked collection is not taken */
	CHECK(!appletb_find_usage(tb, sizeof (tb), 0xff120001, 0x00ff0004,
	    &f));
	CHECK(!f.af_found);
	/* keyboard key range: usage 0x0007003a (F1) is in the input array */
	CHECK(appletb_find_usage(tb, sizeof (tb), 0x00010006, 0x0007003a, &f));
	CHECK(f.af_rid == 1 && f.af_type == REPORT_INPUT);
	/* truncated descriptors: no overrun, not found */
	CHECK(!appletb_find_usage(tb, 25, 0x00010006, 0x00ff0004, &f));
	CHECK(!appletb_find_usage(tb, 0, 0x00010006, 0x00ff0004, &f));

	(void) printf("appletb hid lookup: %d tests, %d failures\n", tests,
	    failures);
	return (failures == 0 ? 0 : 1);
}
