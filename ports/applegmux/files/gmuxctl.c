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
 * gmuxctl: show the Apple gmux (applegmux(4D)) and set the backlight.
 *
 *   gmuxctl              type, version, backlight, mux state
 *   gmuxctl -b           the backlight in percent
 *   gmuxctl -s VALUE     set the backlight: N (raw), N%, +N% or -N%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "applegmux.h"

static const char *
type_name(uint32_t t)
{
	switch (t) {
	case APPLEGMUX_TYPE_PIO:
		return ("classic (I/O ports)");
	case APPLEGMUX_TYPE_INDEXED:
		return ("indexed (I/O ports)");
	case APPLEGMUX_TYPE_MMIO:
		return ("T2 (memory)");
	}
	return ("unknown");
}

static unsigned
pct(uint32_t v, uint32_t max)
{
	return (max == 0 ? 0 : (unsigned)(((uint64_t)v * 100 + max / 2) / max));
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: gmuxctl [-b | -s N|N%%|+N%%|-N%%]\n");
	exit(2);
}

/* The new value for a -s argument, or -1 */
static int64_t
parse(const char *s, uint32_t cur, uint32_t max)
{
	char *end;
	int sign = 0;
	long v;
	int64_t n;

	if (*s == '+' || *s == '-')
		sign = *s++ == '+' ? 1 : -1;
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno != 0 || end == s || v < 0)
		return (-1);
	if (strcmp(end, "%") == 0) {
		if (v > 100)
			return (-1);
		n = ((int64_t)v * max + 50) / 100;
	} else if (*end == '\0' && sign == 0) {
		n = v;
	} else {
		return (-1);
	}
	if (sign != 0)
		n = cur + sign * n;
	if (n < 0)
		n = 0;
	if (n > max)
		n = max;
	return (n);
}

int
main(int argc, char **argv)
{
	applegmux_info_t gi;
	const char *set = NULL;
	int c, fd, bonly = 0;

	while ((c = getopt(argc, argv, "bs:")) != -1) {
		switch (c) {
		case 'b':
			bonly = 1;
			break;
		case 's':
			set = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind != argc || (bonly && set != NULL))
		usage();

	if ((fd = open(APPLEGMUX_DEV, set != NULL ? O_RDWR : O_RDONLY)) < 0) {
		perror(APPLEGMUX_DEV);
		return (1);
	}
	if (ioctl(fd, APPLEGMUX_IOC_INFO, &gi) != 0) {
		perror("APPLEGMUX_IOC_INFO");
		return (1);
	}

	if (set != NULL) {
		int64_t n = parse(set, gi.gi_brightness, gi.gi_max_brightness);
		uint32_t v;

		if (n < 0)
			usage();
		v = (uint32_t)n;
		if (ioctl(fd, APPLEGMUX_IOC_SET_BRIGHTNESS, &v) != 0) {
			perror("APPLEGMUX_IOC_SET_BRIGHTNESS");
			return (1);
		}
		return (0);
	}

	if (bonly) {
		(void) printf("%u\n", pct(gi.gi_brightness,
		    gi.gi_max_brightness));
		return (0);
	}

	(void) printf("gmux:      %s, version %u.%u.%u, at 0x%llx\n",
	    type_name(gi.gi_type), gi.gi_ver_major, gi.gi_ver_minor,
	    gi.gi_ver_release, (unsigned long long)gi.gi_base);
	if (gi.gi_max_brightness == 0)
		(void) printf("backlight: none\n");
	else
		(void) printf("backlight: %u of %u (%u%%)\n", gi.gi_brightness,
		    gi.gi_max_brightness, pct(gi.gi_brightness,
		    gi.gi_max_brightness));
	(void) printf("panel:     %s GPU\n",
	    (gi.gi_display & 1) ? "discrete" : "integrated");
	(void) printf("external:  %s GPU\n",
	    gi.gi_external == 2 ? "integrated" : "discrete");
	if (gi.gi_type == APPLEGMUX_TYPE_PIO)
		(void) printf("DDC:       %s GPU\n",
		    gi.gi_ddc == 1 ? "integrated" : "discrete");
	(void) printf("registers: display 0x%02x ddc 0x%02x external 0x%02x\n",
	    gi.gi_display, gi.gi_ddc, gi.gi_external);
	return (0);
}
