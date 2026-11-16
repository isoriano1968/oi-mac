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
 * Userland test of the applegmux protocol section (src/applegmux.c,
 * between "applegmux protocol: begin" and "end", cut out by the Makefile
 * into applegmux_proto.h).  It runs the driver's code against simulated
 * gmuxes: a classic one (registers on ports), an indexed one (register
 * number to READ/WRITE, data through VALUE, a done bit that comes after a
 * few polls and is cleared by reading READ), a T2 one (memory, big-endian
 * data, a command byte that stays busy for a few reads), an absent one,
 * and an indexed one that never completes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "applegmux.h"

#ifndef __illumos__
typedef unsigned int uint_t;
#endif

typedef struct gmux_hw gmux_hw_t;

static uint8_t sim_inb(uint_t port);
static uint32_t sim_inl(uint_t port);
static void sim_outb(uint_t port, uint8_t v);
static uint8_t sim_mr8(uint_t off);
static void sim_mw8(uint_t off, uint8_t v);
static uint32_t sim_mr32be(void);
static void sim_mw32be(uint32_t v);
static void sim_delay(uint_t us);

#define	GMUX_INB(hw, off)	sim_inb((hw)->gh_port + (off))
#define	GMUX_INL(hw, off)	sim_inl((hw)->gh_port + (off))
#define	GMUX_OUTB(hw, off, v)	sim_outb((hw)->gh_port + (off), (v))
#define	GMUX_MR8(hw, off)	((void)(hw), sim_mr8(off))
#define	GMUX_MW8(hw, off, v)	sim_mw8((off), (v))
#define	GMUX_MR32BE(hw)		sim_mr32be()
#define	GMUX_MW32BE(hw, v)	sim_mw32be(v)
#define	GMUX_DELAY(us)		sim_delay(us)

#include "applegmux_proto.h"

/* The simulated machine */
#define	BASE	0x700

static enum { SIM_NONE, SIM_PIO, SIM_INDEXED, SIM_MMIO } sim;
static uint32_t regs[256];	/* the gmux's registers */
static uint8_t io[256];		/* PIO, indexed: the port window */
static uint8_t mm[16];		/* MMIO window */
static int lag;			/* polls until a transfer is done */
static int busy;		/* polls left */
static int stuck;		/* never completes */
static int done;		/* indexed: done bit */
static int pending = -1;	/* indexed: register being read */
static unsigned long delays, ops;

static void
sim_reset(int kind)
{
	sim = kind;
	memset(regs, 0, sizeof (regs));
	memset(io, 0xff, sizeof (io));
	memset(mm, 0, sizeof (mm));
	busy = done = stuck = 0;
	pending = -1;
	lag = 3;
	delays = ops = 0;
	regs[GMUX_VERSION_MAJOR] = 0;
	if (kind == SIM_NONE)
		memset(mm, 0xff, sizeof (mm));
}

static void
sim_delay(uint_t us)
{
	delays += us;
}

static void
idx_finish(void)
{
	if (pending >= 0) {
		uint32_t v = regs[pending];
		io[GMUX_IDX_VALUE] = v;
		io[GMUX_IDX_VALUE + 1] = v >> 8;
		io[GMUX_IDX_VALUE + 2] = v >> 16;
		io[GMUX_IDX_VALUE + 3] = v >> 24;
		pending = -1;
	}
	done = 1;
}

static uint8_t
sim_inb(uint_t port)
{
	uint_t off = port - BASE;

	if (port < BASE || off > 0xff)
		abort();
	switch (sim) {
	case SIM_NONE:
		return (0xff);
	case SIM_PIO:
		if (off >= GMUX_VERSION_MAJOR && off <= GMUX_VERSION_RELEASE)
			return (regs[GMUX_VERSION_MAJOR] >> (8 * (2 -
			    (off - GMUX_VERSION_MAJOR))) & 0xff);
		if (off < 0xc0)
			return (regs[off & ~3U] >> (8 * (off & 3)) & 0xff);
		return (io[off]);
	case SIM_INDEXED:
		if (off == GMUX_IDX_WRITE) {
			if (!done && busy > 0 && --busy == 0 && !stuck)
				idx_finish();
			return (done ? GMUX_IDX_DONE : 0);
		}
		if (off == GMUX_IDX_READ) {
			done = 0;
			return (0);
		}
		if (off >= GMUX_IDX_PROBE && off <= GMUX_IDX_PROBE + 2)
			return (io[off]);
		if (off >= GMUX_IDX_VALUE && off < GMUX_IDX_VALUE + 4)
			return (io[off]);
		return (0xff);	/* version etc.: not readable directly */
	case SIM_MMIO:
		abort();
	}
	return (0xff);
}

static uint32_t
sim_inl(uint_t port)
{
	return (sim_inb(port) | sim_inb(port + 1) << 8 |
	    sim_inb(port + 2) << 16 | (uint32_t)sim_inb(port + 3) << 24);
}

static void
sim_outb(uint_t port, uint8_t v)
{
	uint_t off = port - BASE;

	if (port < BASE || off > 0xff)
		abort();
	switch (sim) {
	case SIM_NONE:
		return;
	case SIM_PIO:
		if (off < 0xc0) {
			uint32_t m = 0xffU << (8 * (off & 3));
			regs[off & ~3U] = (regs[off & ~3U] & ~m) |
			    ((uint32_t)v << (8 * (off & 3)));
		}
		return;
	case SIM_INDEXED:
		if (off == GMUX_IDX_READ) {
			if (done || busy)
				abort();	/* driver did not wait */
			ops++;
			pending = v;
			busy = lag;
			return;
		}
		if (off == GMUX_IDX_WRITE) {
			if (done || busy)
				abort();
			ops++;
			regs[v] = io[GMUX_IDX_VALUE] |
			    io[GMUX_IDX_VALUE + 1] << 8 |
			    io[GMUX_IDX_VALUE + 2] << 16 |
			    (uint32_t)io[GMUX_IDX_VALUE + 3] << 24;
			busy = lag;
			return;
		}
		io[off] = v;
		return;
	case SIM_MMIO:
		abort();
	}
}

static uint8_t
sim_mr8(uint_t off)
{
	if (off >= sizeof (mm) || sim == SIM_PIO || sim == SIM_INDEXED)
		abort();
	if (sim == SIM_MMIO && off == GMUX_MMIO_COMMAND && mm[off] != 0) {
		if (busy > 0 && --busy == 0 && !stuck) {
			uint_t sz = mm[off] & 0x3f, reg = mm[GMUX_MMIO_SELECT];
			uint32_t v = regs[reg];

			if (mm[off] & GMUX_MMIO_WRITE) {
				regs[reg] = sz == 4 ? (uint32_t)mm[0] << 24 |
				    mm[1] << 16 | mm[2] << 8 | mm[3] : mm[0];
			} else if (sz == 4) {
				mm[0] = v >> 24; mm[1] = v >> 16;
				mm[2] = v >> 8; mm[3] = v;
			} else {
				mm[0] = v;
			}
			mm[off] = 0;
		}
	}
	return (mm[off]);
}

static void
sim_mw8(uint_t off, uint8_t v)
{
	if (off >= sizeof (mm) || sim != SIM_MMIO)
		abort();
	if (off == GMUX_MMIO_COMMAND) {
		if (mm[off] != 0)
			abort();	/* driver did not wait */
		ops++;
		busy = lag;
	}
	mm[off] = v;
}

static uint32_t
sim_mr32be(void)
{
	return ((uint32_t)sim_mr8(0) << 24 | sim_mr8(1) << 16 |
	    sim_mr8(2) << 8 | sim_mr8(3));
}

static void
sim_mw32be(uint32_t v)
{
	sim_mw8(0, v >> 24);
	sim_mw8(1, v >> 16);
	sim_mw8(2, v >> 8);
	sim_mw8(3, v);
}

static int fails;

#define	CHECK(c) do { if (!(c)) { \
	(void) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
	fails++; } } while (0)

/* Common checks once a gmux of the given type is set up */
static void
exercise(gmux_hw_t *hw, uint32_t max_expect)
{
	uint8_t ver[3], d, ddc, ext;
	uint32_t max, b;

	CHECK(gmux_version(hw, ver) == 0);
	CHECK(ver[0] == 4 && ver[1] == 9 && ver[2] == 1);
	CHECK(gmux_max_brightness(hw, &max) == 0);
	CHECK(max == max_expect);
	CHECK(gmux_get_brightness(hw, &b) == 0);
	CHECK(b == 0x5678);
	CHECK(gmux_set_brightness(hw, max, 0x1234) == 0);
	CHECK(regs[GMUX_BRIGHTNESS] == 0x1234);
	CHECK(gmux_get_brightness(hw, &b) == 0 && b == 0x1234);
	CHECK(gmux_set_brightness(hw, max, max + 1) == EINVAL);
	CHECK(regs[GMUX_BRIGHTNESS] == 0x1234);
	CHECK(gmux_set_brightness(hw, 0, 1) == ENOTSUP);
	CHECK(gmux_switch_state(hw, &d, &ddc, &ext) == 0);
	CHECK(d == 3 && ddc == 2 && ext == 3);
}

static void
setregs(int pio)
{
	/* version 4.9.1: on PIO three byte registers, else one word */
	regs[GMUX_VERSION_MAJOR] = pio ? 0x040901 : 0x04090100;
	regs[GMUX_MAX_BRIGHTNESS] = 0xffffffff;	/* clamped to 24 bits */
	regs[GMUX_BRIGHTNESS] = 0xff005678;	/* top byte ignored */
	regs[GMUX_SWITCH_DISPLAY] = 3;		/* discrete */
	regs[GMUX_SWITCH_DDC] = 2;
	regs[GMUX_SWITCH_EXTERNAL] = 3;
}

int
main(void)
{
	gmux_hw_t hw;
	uint32_t b;

	/* absent: no gmux on the ports, 0xff in memory */
	sim_reset(SIM_NONE);
	memset(&hw, 0, sizeof (hw));
	hw.gh_port = BASE;
	CHECK(gmux_detect_io(&hw, 0x100) == ENODEV);
	CHECK(gmux_detect_mmio(&hw) == ENODEV);
	CHECK(hw.gh_type == 0);

	/* classic */
	sim_reset(SIM_PIO);
	setregs(1);
	memset(&hw, 0, sizeof (hw));
	hw.gh_port = BASE;
	CHECK(gmux_detect_io(&hw, GMUX_MIN_IO_LEN - 1) == ENODEV);
	CHECK(gmux_detect_io(&hw, GMUX_MIN_IO_LEN) == 0);
	CHECK(hw.gh_type == APPLEGMUX_TYPE_PIO);
	exercise(&hw, 0xffffff);

	/* indexed, with a 3-poll lag */
	sim_reset(SIM_INDEXED);
	setregs(0);
	memset(&hw, 0, sizeof (hw));
	hw.gh_port = BASE;
	CHECK(gmux_detect_io(&hw, 0x100) == 0);
	CHECK(hw.gh_type == APPLEGMUX_TYPE_INDEXED);
	exercise(&hw, 0xffffff);
	CHECK(done == 0 && busy == 0);	/* every transfer acknowledged */
	CHECK(delays > 0);
	(void) printf("indexed: %lu transfers, %lu us polling\n", ops,
	    delays);

	/* indexed, completing at once */
	sim_reset(SIM_INDEXED);
	setregs(0);
	lag = 1;
	exercise(&hw, 0xffffff);

	/* indexed that never completes: a timeout, not a hang */
	sim_reset(SIM_INDEXED);
	setregs(0);
	stuck = 1;
	CHECK(gmux_get_brightness(&hw, &b) == ETIMEDOUT);
	CHECK(delays == GMUX_POLLS * GMUX_POLL_US);

	/* T2 */
	sim_reset(SIM_MMIO);
	setregs(0);
	memset(&hw, 0, sizeof (hw));
	CHECK(gmux_detect_mmio(&hw) == 0);
	CHECK(hw.gh_type == APPLEGMUX_TYPE_MMIO);
	exercise(&hw, 0xffff);
	CHECK(mm[GMUX_MMIO_COMMAND] == 0);

	/* T2 stuck */
	sim_reset(SIM_MMIO);
	setregs(0);
	stuck = 1;
	CHECK(gmux_get_brightness(&hw, &b) == ETIMEDOUT);

	if (fails != 0) {
		(void) printf("applegmux_test: %d failures\n", fails);
		return (1);
	}
	(void) printf("applegmux_test: all passed\n");
	return (0);
}
