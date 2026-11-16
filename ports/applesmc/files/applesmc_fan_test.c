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
 * Userland unit test of the applesmc(4D) fan write policy: which keys may
 * be written and how values are clamped.  The Makefile cuts the block
 * between "applesmc fan policy: begin" and "end" out of the patched
 * applesmc.c into build/applesmc_fan_policy.h, so this tests the code the
 * driver runs.  Values are those of a Mac Pro (Late 2013): F0Mn 790,
 * F0Mx 1900 rpm (fpe2 = rpm * 4); the "flt " fans are MacBook Pro 2016-2017
 * style, values in rpm as little-endian IEEE singles (bounds assumed:
 * 2160-5927 rpm).
 */

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "applesmc_fan_policy.h"

static int failures, tests;

#define	CHECK(cond)	do {						\
	tests++;							\
	if (!(cond)) {							\
		(void) printf("FAIL line %d: %s\n", __LINE__, #cond);	\
		failures++;						\
	}								\
} while (0)

static int
try(const smc_fan_t *fans, uint_t n, const char *key, const char *type,
    uint8_t len, uint16_t in, uint16_t *out)
{
	uint8_t d[2];
	int ret;

	smc_fpe2_put(d, in);
	ret = smc_fan_check(fans, n, key, type, len, d);
	*out = smc_fpe2_get(d);
	return (ret);
}

/* A "flt " write: in and out are rpm * 4, the data an IEEE single in rpm. */
static int
tryf(const smc_fan_t *fans, uint_t n, const char *key, const char *type,
    uint8_t len, uint16_t in, uint16_t *out)
{
	uint8_t d[4];
	int ret;

	smc_flt_put(d, in);
	ret = smc_fan_check(fans, n, key, type, len, d);
	*out = smc_flt_get(d);
	return (ret);
}

/* The bytes of a float as the SMC sends "flt " (little endian, as x86). */
static void
fltbytes(float f, uint8_t b[4])
{
	(void) memcpy(b, &f, 4);
}

int
main(void)
{
	smc_fan_t fans[SMC_MAX_FANS] = { 0 };
	uint16_t v;
	uint8_t b[2], b4[4];

	fans[0].sf_valid = B_TRUE;
	fans[0].sf_min = 790 * 4;
	fans[0].sf_max = 1900 * 4;
	fans[2].sf_valid = B_TRUE;
	fans[2].sf_min = 1200 * 4;
	fans[2].sf_max = 1200 * 4;

	/* key matching */
	CHECK(smc_fan_wkey("F0Mn") == 0);
	CHECK(smc_fan_wkey("F0Tg") == 0);
	CHECK(smc_fan_wkey("F9Mn") == 9);
	CHECK(smc_fan_wkey("F0Mx") == -1);
	CHECK(smc_fan_wkey("F0Ac") == -1);
	CHECK(smc_fan_wkey("F0Md") == -1);
	CHECK(smc_fan_wkey("FS! ") == -1);
	CHECK(smc_fan_wkey("FaMn") == -1);
	CHECK(smc_fan_wkey("f0Mn") == -1);
	CHECK(smc_fan_wkey("F0mn") == -1);
	CHECK(smc_fan_wkey("TC0P") == -1);
	CHECK(smc_fan_wkey("#KEY") == -1);

	/* fpe2 encoding: Linux buffer[0] = rpm >> 6, buffer[1] = rpm << 2 */
	smc_fpe2_put(b, 926 * 4);
	CHECK(b[0] == ((926 >> 6) & 0xff) && b[1] == ((926 << 2) & 0xff));
	CHECK(smc_fpe2_get(b) == 926 * 4);

	/* in range: unchanged */
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 1160 * 4, &v) == 0 &&
	    v == 1160 * 4);
	CHECK(try(fans, 1, "F0Tg", "fpe2", 2, 1530 * 4, &v) == 0 &&
	    v == 1530 * 4);
	/* clamped below to the attach minimum, above to the maximum */
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 0, &v) == 0 && v == 790 * 4);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 789 * 4, &v) == 0 &&
	    v == 790 * 4);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 1901 * 4, &v) == 0 &&
	    v == 1900 * 4);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 0xffff, &v) == 0 &&
	    v == 1900 * 4);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 790 * 4, &v) == 0 &&
	    v == 790 * 4);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 2, 1900 * 4, &v) == 0 &&
	    v == 1900 * 4);

	/* other keys, types, lengths, fans */
	CHECK(try(fans, 1, "F0Mx", "fpe2", 2, 1000 * 4, &v) == EPERM);
	CHECK(try(fans, 1, "F0Ac", "fpe2", 2, 1000 * 4, &v) == EPERM);
	CHECK(try(fans, 1, "FS! ", "ui16", 2, 1, &v) == EPERM);
	CHECK(try(fans, 1, "TC0P", "sp78", 2, 1, &v) == EPERM);
	CHECK(try(fans, 1, "F0Mn", "ui16", 2, 1000 * 4, &v) == EINVAL);
	CHECK(try(fans, 1, "F0Mn", "flt ", 2, 1000 * 4, &v) == EINVAL);
	CHECK(try(fans, 1, "F0Mn", "fpe2", 4, 1000 * 4, &v) == EINVAL);
	CHECK(try(fans, 1, "F1Mn", "fpe2", 2, 1000 * 4, &v) == ENOENT);
	CHECK(try(fans, 3, "F1Mn", "fpe2", 2, 1000 * 4, &v) == ENOENT);
	CHECK(try(fans, 3, "F2Mn", "fpe2", 2, 1000 * 4, &v) == 0 &&
	    v == 1200 * 4);
	CHECK(try(fans, SMC_MAX_FANS, "F9Mn", "fpe2", 2, 1, &v) == ENOENT);

	/* a fan whose bounds are inverted is never written */
	fans[1].sf_valid = B_TRUE;
	fans[1].sf_min = 2000 * 4;
	fans[1].sf_max = 1000 * 4;
	CHECK(try(fans, 2, "F1Mn", "fpe2", 2, 1500 * 4, &v) == EINVAL);

	/* "flt ": the integer conversion matches the FPU for every value */
	{
		uint_t r, bad = 0;

		for (r = 0; r <= 0xffff; r++) {
			uint8_t m[4];

			smc_flt_put(b4, (uint16_t)r);
			fltbytes(r / 4.0f, m);
			if (memcmp(b4, m, 4) != 0 || smc_flt_get(b4) != r)
				bad++;
		}
		CHECK(bad == 0);
	}
	fltbytes(2160.0f, b4);			/* 0x45070000 */
	CHECK(b4[0] == 0x00 && b4[1] == 0x00 && b4[2] == 0x07 &&
	    b4[3] == 0x45 && smc_flt_get(b4) == 2160 * 4);
	fltbytes(1234.6f, b4);			/* rounded down */
	CHECK(smc_flt_get(b4) == 4938);
	fltbytes(-5.0f, b4);
	CHECK(smc_flt_get(b4) == 0);
	fltbytes(1e9f, b4);
	CHECK(smc_flt_get(b4) == 0xffff);
	fltbytes(0.1f, b4);
	CHECK(smc_flt_get(b4) == 0);
	b4[0] = 0; b4[1] = 0; b4[2] = 0xc0; b4[3] = 0x7f;	/* NaN */
	CHECK(smc_flt_get(b4) == 0);

	fans[3].sf_valid = B_TRUE;
	fans[3].sf_flt = B_TRUE;
	fans[3].sf_min = 2160 * 4;
	fans[3].sf_max = 5927 * 4;
	CHECK(tryf(fans, 4, "F3Mn", "flt ", 4, 3000 * 4, &v) == 0 &&
	    v == 3000 * 4);
	CHECK(tryf(fans, 4, "F3Tg", "flt ", 4, 4000 * 4, &v) == 0 &&
	    v == 4000 * 4);
	CHECK(tryf(fans, 4, "F3Mn", "flt ", 4, 1000 * 4, &v) == 0 &&
	    v == 2160 * 4);
	CHECK(tryf(fans, 4, "F3Mn", "flt ", 4, 0xffff, &v) == 0 &&
	    v == 5927 * 4);
	/* a "flt " fan takes only "flt " with length 4, an fpe2 fan only fpe2 */
	CHECK(tryf(fans, 4, "F3Mn", "fpe2", 2, 3000 * 4, &v) == EINVAL);
	CHECK(tryf(fans, 4, "F3Mn", "flt ", 2, 3000 * 4, &v) == EINVAL);
	CHECK(tryf(fans, 4, "F3Mn", "fpe2", 4, 3000 * 4, &v) == EINVAL);
	CHECK(tryf(fans, 4, "F3Mx", "flt ", 4, 3000 * 4, &v) == EPERM);
	CHECK(tryf(fans, 4, "F0Mn", "flt ", 4, 1000 * 4, &v) == EINVAL);

	(void) printf("applesmc fan policy: %d tests, %d failures\n", tests,
	    failures);
	return (failures == 0 ? 0 : 1);
}
