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

#ifndef _SYS_APPLEGMUX_H
#define	_SYS_APPLEGMUX_H

/*
 * applegmux(4D): the gmux of dual-GPU Apple machines (ACPI APP000B).
 * /dev/applegmux offers the panel backlight and the state of the display
 * mux, read only.  The layout of applegmux_info_t is the same for 32-bit
 * and 64-bit callers.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	APPLEGMUX_DEV		"/dev/applegmux"

/* gi_type: how the gmux is reached */
#define	APPLEGMUX_TYPE_PIO	1	/* pre-Retina MacBook Pros: ports */
#define	APPLEGMUX_TYPE_INDEXED	2	/* Retina, before the T2: indexed */
#define	APPLEGMUX_TYPE_MMIO	3	/* T2 Macs: memory mapped */

typedef struct applegmux_info {
	uint32_t	gi_type;
	uint8_t		gi_ver_major;
	uint8_t		gi_ver_minor;
	uint8_t		gi_ver_release;
	uint8_t		gi_pad0;
	uint32_t	gi_max_brightness;	/* 0: no backlight control */
	uint32_t	gi_brightness;
	/*
	 * The mux registers as read; bit 0 of gi_display set means the
	 * panel is on the discrete GPU.  gi_ddc: 1 integrated, else
	 * discrete (pre-Retina only).  gi_external: 2 integrated, else
	 * discrete.
	 */
	uint8_t		gi_display;
	uint8_t		gi_ddc;
	uint8_t		gi_external;
	uint8_t		gi_pad1;
	uint64_t	gi_base;	/* I/O port or physical address */
} applegmux_info_t;

#define	APPLEGMUX_IOC		(('g' << 16) | ('m' << 8))
/* applegmux_info_t */
#define	APPLEGMUX_IOC_INFO		(APPLEGMUX_IOC | 1)
/* uint32_t: the brightness, 0 .. gi_max_brightness */
#define	APPLEGMUX_IOC_GET_BRIGHTNESS	(APPLEGMUX_IOC | 2)
/* uint32_t; needs the device open for writing */
#define	APPLEGMUX_IOC_SET_BRIGHTNESS	(APPLEGMUX_IOC | 3)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_APPLEGMUX_H */
