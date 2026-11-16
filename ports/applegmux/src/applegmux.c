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
 * Apple gmux: the microcontroller of the dual-GPU MacBook Pros (and the
 * Mac Pro 2013) that switches the panel between the integrated and the
 * discrete GPU, powers the discrete GPU, and drives the panel backlight
 * whichever GPU is active.  ACPI describes it as device APP000B.
 *
 * On these machines neither GPU's backlight controls the panel, so the
 * gmux is the only way to dim it.  This driver offers:
 *
 *  - the backlight: read, and set by callers that opened /dev/applegmux
 *    for writing (device permissions, 0644 root by default);
 *  - the mux state (panel, DDC, external port), read only.
 *
 * It never switches the mux or the discrete GPU's power: the firmware
 * chose them at boot (the EFI variable gpu-power-prefs), and the graphics
 * drivers expect that choice.
 *
 * Three generations reach the gmux differently (the register map and
 * the protocols are those documented in Linux's apple-gmux driver; this
 * is a new implementation):
 *
 *  - pre-Retina (Lattice XP2): I/O ports, registers at base + offset;
 *  - Retina before the T2 (Renesas R4F2113, e.g. MacBookPro11,3 and
 *    14,3): I/O ports, indexed: the register number goes to the READ or
 *    WRITE port, the data through the VALUE port, with a handshake bit;
 *  - T2 Macs: 16 bytes of memory; register number, command and data.
 *
 * The driver is a pseudo device that looks for APP000B itself, as
 * applesmc does for APP0001; without a gmux it does not attach.  It uses
 * no interrupt: the gmux's GPE only reports switch completion.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>
#include "applegmux.h"

/*
 * Access to the gmux registers, for the protocol section below: kernel
 * versions here, simulated ones in files/applegmux_test.c.
 */
#define	GMUX_INB(hw, off)	inb((hw)->gh_port + (off))
#define	GMUX_INL(hw, off)	inl((hw)->gh_port + (off))
#define	GMUX_OUTB(hw, off, v)	outb((hw)->gh_port + (off), (v))
#define	GMUX_MR8(hw, off)	((hw)->gh_mmio[(off)])
#define	GMUX_MW8(hw, off, v)	((hw)->gh_mmio[(off)] = (v))
#define	GMUX_MR32BE(hw)		BE_32(*(volatile uint32_t *)(hw)->gh_mmio)
#define	GMUX_MW32BE(hw, v)	\
	(*(volatile uint32_t *)(hw)->gh_mmio = BE_32(v))
#define	GMUX_DELAY(us)		drv_usecwait(us)

/* applegmux protocol: begin */

/* Registers */
#define	GMUX_VERSION_MAJOR	0x04
#define	GMUX_VERSION_MINOR	0x05
#define	GMUX_VERSION_RELEASE	0x06
#define	GMUX_SWITCH_DISPLAY	0x10
#define	GMUX_SWITCH_DDC		0x28
#define	GMUX_SWITCH_EXTERNAL	0x40
#define	GMUX_MAX_BRIGHTNESS	0x70
#define	GMUX_BRIGHTNESS		0x74
/* Indexed: the data, the register to read, the register to write */
#define	GMUX_IDX_VALUE		0xc2
#define	GMUX_IDX_READ		0xd0
#define	GMUX_IDX_WRITE		0xd4
#define	GMUX_IDX_DONE		0x01	/* in GMUX_IDX_WRITE */
/* Indexed: scratch bytes that only an indexed gmux reads back */
#define	GMUX_IDX_PROBE		0xcc
/* The I/O window must reach the brightness register. */
#define	GMUX_MIN_IO_LEN		(GMUX_BRIGHTNESS + 4)

/* MMIO (T2): data at 0, register at 0x0e, command at 0x0f */
#define	GMUX_MMIO_LEN		16
#define	GMUX_MMIO_SELECT	0x0e
#define	GMUX_MMIO_COMMAND	0x0f
#define	GMUX_MMIO_READ		0x00
#define	GMUX_MMIO_WRITE		0x40

#define	GMUX_BRIGHTNESS_MASK	0x00ffffffU
#define	GMUX_MMIO_MAX		0xffffU

/* Handshake polls, 100 us apart: 20 ms */
#define	GMUX_POLLS		200
#define	GMUX_POLL_US		100

typedef struct gmux_hw {
	uint_t		gh_type;	/* APPLEGMUX_TYPE_* */
	uint16_t	gh_port;	/* PIO, indexed */
	volatile uint8_t *gh_mmio;	/* MMIO */
} gmux_hw_t;

/*
 * Indexed: wait until no completed transfer is pending; reading the READ
 * port acknowledges one.
 */
static int
gmux_idx_ready(gmux_hw_t *hw)
{
	int i;

	for (i = 0; i < GMUX_POLLS; i++) {
		if ((GMUX_INB(hw, GMUX_IDX_WRITE) & GMUX_IDX_DONE) == 0)
			return (0);
		(void) GMUX_INB(hw, GMUX_IDX_READ);
		GMUX_DELAY(GMUX_POLL_US);
	}
	return ((GMUX_INB(hw, GMUX_IDX_WRITE) & GMUX_IDX_DONE) == 0 ?
	    0 : ETIMEDOUT);
}

/* Indexed: wait for the transfer to complete, and acknowledge it. */
static int
gmux_idx_complete(gmux_hw_t *hw)
{
	int i;

	for (i = 0; ; i++) {
		if (GMUX_INB(hw, GMUX_IDX_WRITE) & GMUX_IDX_DONE) {
			(void) GMUX_INB(hw, GMUX_IDX_READ);
			return (0);
		}
		if (i == GMUX_POLLS)
			return (ETIMEDOUT);
		GMUX_DELAY(GMUX_POLL_US);
	}
}

/* MMIO: wait until the gmux has taken the last command. */
static int
gmux_mmio_wait(gmux_hw_t *hw)
{
	int i;

	for (i = 0; ; i++) {
		if (GMUX_MR8(hw, GMUX_MMIO_COMMAND) == 0)
			return (0);
		if (i == GMUX_POLLS)
			return (ETIMEDOUT);
		GMUX_DELAY(GMUX_POLL_US);
	}
}

static int
gmux_read8(gmux_hw_t *hw, uint8_t reg, uint8_t *valp)
{
	int ret;

	switch (hw->gh_type) {
	case APPLEGMUX_TYPE_PIO:
		*valp = GMUX_INB(hw, reg);
		return (0);
	case APPLEGMUX_TYPE_INDEXED:
		if ((ret = gmux_idx_ready(hw)) != 0)
			return (ret);
		GMUX_OUTB(hw, GMUX_IDX_READ, reg);
		if ((ret = gmux_idx_complete(hw)) != 0)
			return (ret);
		*valp = GMUX_INB(hw, GMUX_IDX_VALUE);
		return (0);
	case APPLEGMUX_TYPE_MMIO:
		if ((ret = gmux_mmio_wait(hw)) != 0)
			return (ret);
		GMUX_MW8(hw, GMUX_MMIO_SELECT, reg);
		GMUX_MW8(hw, GMUX_MMIO_COMMAND, GMUX_MMIO_READ | 1);
		if ((ret = gmux_mmio_wait(hw)) != 0)
			return (ret);
		*valp = GMUX_MR8(hw, 0);
		return (0);
	}
	return (ENXIO);
}

static int
gmux_read32(gmux_hw_t *hw, uint8_t reg, uint32_t *valp)
{
	int ret;

	switch (hw->gh_type) {
	case APPLEGMUX_TYPE_PIO:
		*valp = GMUX_INL(hw, reg);
		return (0);
	case APPLEGMUX_TYPE_INDEXED:
		if ((ret = gmux_idx_ready(hw)) != 0)
			return (ret);
		GMUX_OUTB(hw, GMUX_IDX_READ, reg);
		if ((ret = gmux_idx_complete(hw)) != 0)
			return (ret);
		*valp = GMUX_INL(hw, GMUX_IDX_VALUE);
		return (0);
	case APPLEGMUX_TYPE_MMIO:
		if ((ret = gmux_mmio_wait(hw)) != 0)
			return (ret);
		GMUX_MW8(hw, GMUX_MMIO_SELECT, reg);
		GMUX_MW8(hw, GMUX_MMIO_COMMAND, GMUX_MMIO_READ | 4);
		if ((ret = gmux_mmio_wait(hw)) != 0)
			return (ret);
		*valp = GMUX_MR32BE(hw);
		return (0);
	}
	return (ENXIO);
}

/* Little-endian, one byte at a time (PIO, indexed); big-endian on MMIO. */
static int
gmux_write32(gmux_hw_t *hw, uint8_t reg, uint32_t val)
{
	int i, ret;

	switch (hw->gh_type) {
	case APPLEGMUX_TYPE_PIO:
		for (i = 0; i < 4; i++)
			GMUX_OUTB(hw, reg + i, (val >> (i * 8)) & 0xff);
		return (0);
	case APPLEGMUX_TYPE_INDEXED:
		for (i = 0; i < 4; i++) {
			GMUX_OUTB(hw, GMUX_IDX_VALUE + i,
			    (val >> (i * 8)) & 0xff);
		}
		if ((ret = gmux_idx_ready(hw)) != 0)
			return (ret);
		GMUX_OUTB(hw, GMUX_IDX_WRITE, reg);
		return (gmux_idx_complete(hw));
	case APPLEGMUX_TYPE_MMIO:
		if ((ret = gmux_mmio_wait(hw)) != 0)
			return (ret);
		GMUX_MW32BE(hw, val);
		GMUX_MW8(hw, GMUX_MMIO_SELECT, reg);
		GMUX_MW8(hw, GMUX_MMIO_COMMAND, GMUX_MMIO_WRITE | 4);
		return (gmux_mmio_wait(hw));
	}
	return (ENXIO);
}

/*
 * Which kind of gmux is behind an I/O window of iolen bytes at gh_port:
 * a classic one answers its version registers; on an indexed one they
 * read 0xff, but it keeps what is written to its probe bytes.  Sets
 * gh_type, or returns ENODEV.
 */
static int
gmux_detect_io(gmux_hw_t *hw, uint_t iolen)
{
	uint16_t v;

	if (iolen < GMUX_MIN_IO_LEN)
		return (ENODEV);
	if (GMUX_INB(hw, GMUX_VERSION_MAJOR) != 0xff ||
	    GMUX_INB(hw, GMUX_VERSION_MINOR) != 0xff ||
	    GMUX_INB(hw, GMUX_VERSION_RELEASE) != 0xff) {
		hw->gh_type = APPLEGMUX_TYPE_PIO;
		return (0);
	}
	GMUX_OUTB(hw, GMUX_IDX_PROBE, 0xaa);
	GMUX_OUTB(hw, GMUX_IDX_PROBE + 1, 0x55);
	GMUX_OUTB(hw, GMUX_IDX_PROBE + 2, 0x00);
	v = GMUX_INB(hw, GMUX_IDX_PROBE) |
	    (GMUX_INB(hw, GMUX_IDX_PROBE + 1) << 8);
	if (v != 0x55aa)
		return (ENODEV);
	hw->gh_type = APPLEGMUX_TYPE_INDEXED;
	return (0);
}

/*
 * A T2 gmux clears its command byte when idle, or holds a command in
 * progress; 0xff means nothing answers there.
 */
static int
gmux_detect_mmio(gmux_hw_t *hw)
{
	if (GMUX_MR8(hw, GMUX_MMIO_COMMAND) == 0xff)
		return (ENODEV);
	hw->gh_type = APPLEGMUX_TYPE_MMIO;
	return (0);
}

/* Version: three registers on a classic gmux, one word on the others. */
static int
gmux_version(gmux_hw_t *hw, uint8_t ver[3])
{
	uint32_t v;
	int ret;

	if (hw->gh_type == APPLEGMUX_TYPE_PIO) {
		if ((ret = gmux_read8(hw, GMUX_VERSION_MAJOR, &ver[0])) != 0 ||
		    (ret = gmux_read8(hw, GMUX_VERSION_MINOR, &ver[1])) != 0 ||
		    (ret = gmux_read8(hw, GMUX_VERSION_RELEASE, &ver[2])) != 0)
			return (ret);
		return (0);
	}
	if ((ret = gmux_read32(hw, GMUX_VERSION_MAJOR, &v)) != 0)
		return (ret);
	ver[0] = v >> 24;
	ver[1] = v >> 16;
	ver[2] = v >> 8;
	return (0);
}

/*
 * The backlight range.  Every T2 gmux takes 0-0xffff, although some
 * iMacs report 0x3ff; the others report it, and only 24 bits count.
 */
static int
gmux_max_brightness(gmux_hw_t *hw, uint32_t *maxp)
{
	int ret;

	if (hw->gh_type == APPLEGMUX_TYPE_MMIO) {
		*maxp = GMUX_MMIO_MAX;
		return (0);
	}
	if ((ret = gmux_read32(hw, GMUX_MAX_BRIGHTNESS, maxp)) != 0)
		return (ret);
	if (*maxp > GMUX_BRIGHTNESS_MASK)
		*maxp = GMUX_BRIGHTNESS_MASK;
	return (0);
}

static int
gmux_get_brightness(gmux_hw_t *hw, uint32_t *valp)
{
	int ret;

	if ((ret = gmux_read32(hw, GMUX_BRIGHTNESS, valp)) != 0)
		return (ret);
	*valp &= GMUX_BRIGHTNESS_MASK;
	return (0);
}

static int
gmux_set_brightness(gmux_hw_t *hw, uint32_t max, uint32_t val)
{
	if (max == 0)
		return (ENOTSUP);
	if (val > max)
		return (EINVAL);
	return (gmux_write32(hw, GMUX_BRIGHTNESS, val));
}

/* The mux registers, as read (see applegmux_info_t). */
static int
gmux_switch_state(gmux_hw_t *hw, uint8_t *display, uint8_t *ddc,
    uint8_t *external)
{
	int ret;

	if ((ret = gmux_read8(hw, GMUX_SWITCH_DISPLAY, display)) != 0 ||
	    (ret = gmux_read8(hw, GMUX_SWITCH_DDC, ddc)) != 0 ||
	    (ret = gmux_read8(hw, GMUX_SWITCH_EXTERNAL, external)) != 0)
		return (ret);
	return (0);
}

/* applegmux protocol: end */

typedef struct applegmux {
	dev_info_t	*gm_dip;
	kmutex_t	gm_lock;
	gmux_hw_t	gm_hw;
	uint64_t	gm_base;	/* I/O port or physical address */
	uint8_t		gm_ver[3];
	uint32_t	gm_max;
	uint32_t	gm_saved;	/* brightness across suspend */
	boolean_t	gm_suspended;
} applegmux_t;

static applegmux_t *applegmux;

/*
 * ACPI: find APP000B and its first I/O port range or memory range.
 */
typedef struct gmux_crs {
	uint16_t	gc_port;
	uint_t		gc_portlen;
	uint64_t	gc_mem;
} gmux_crs_t;

static ACPI_STATUS
gmux_crs_cb(ACPI_RESOURCE *res, void *ctx)
{
	gmux_crs_t *gc = ctx;

	switch (res->Type) {
	case ACPI_RESOURCE_TYPE_IO:
		if (gc->gc_portlen == 0) {
			gc->gc_port = res->Data.Io.Minimum;
			gc->gc_portlen = res->Data.Io.AddressLength;
		}
		break;
	case ACPI_RESOURCE_TYPE_FIXED_IO:
		if (gc->gc_portlen == 0) {
			gc->gc_port = res->Data.FixedIo.Address;
			gc->gc_portlen = res->Data.FixedIo.AddressLength;
		}
		break;
	case ACPI_RESOURCE_TYPE_MEMORY32:
		if (gc->gc_mem == 0)
			gc->gc_mem = res->Data.Memory32.Minimum;
		break;
	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		if (gc->gc_mem == 0)
			gc->gc_mem = res->Data.FixedMemory32.Address;
		break;
	default:
		break;
	}
	return (AE_OK);
}

static ACPI_STATUS
gmux_find_cb(ACPI_HANDLE hdl, UINT32 lvl, void *ctx, void **rv)
{
	_NOTE(ARGUNUSED(lvl, rv));
	*(ACPI_HANDLE *)ctx = hdl;
	return (AE_CTRL_TERMINATE);
}

/*
 * Find the gmux and set up access to it: as Linux, an I/O window long
 * enough for the brightness register means a classic or indexed gmux,
 * otherwise a memory range means a T2 gmux (whose ACPI range is 8 bytes,
 * of the 16 it uses).
 */
static boolean_t
gmux_find(applegmux_t *gm)
{
	ACPI_HANDLE hdl = NULL;
	gmux_crs_t gc;
	gmux_hw_t *hw = &gm->gm_hw;

	if (AcpiSubsystemStatus() != AE_OK)
		return (B_FALSE);
	if (ACPI_FAILURE(AcpiGetDevices("APP000B", gmux_find_cb, &hdl,
	    NULL)) || hdl == NULL)
		return (B_FALSE);

	bzero(&gc, sizeof (gc));
	if (ACPI_FAILURE(AcpiWalkResources(hdl, "_CRS", gmux_crs_cb, &gc)))
		return (B_FALSE);

	if (gc.gc_portlen >= GMUX_MIN_IO_LEN) {
		hw->gh_port = gc.gc_port;
		gm->gm_base = gc.gc_port;
		return (gmux_detect_io(hw, gc.gc_portlen) == 0);
	}
	if (gc.gc_mem == 0)
		return (B_FALSE);
	hw->gh_mmio = AcpiOsMapMemory(gc.gc_mem, GMUX_MMIO_LEN);
	if (hw->gh_mmio == NULL)
		return (B_FALSE);
	gm->gm_base = gc.gc_mem;
	if (gmux_detect_mmio(hw) != 0) {
		AcpiOsUnmapMemory((void *)hw->gh_mmio, GMUX_MMIO_LEN);
		hw->gh_mmio = NULL;
		return (B_FALSE);
	}
	return (B_TRUE);
}

static const char *
gmux_type_name(uint_t type)
{
	switch (type) {
	case APPLEGMUX_TYPE_PIO:
		return ("classic");
	case APPLEGMUX_TYPE_INDEXED:
		return ("indexed");
	case APPLEGMUX_TYPE_MMIO:
		return ("T2");
	}
	return ("unknown");
}

static int
applegmux_open(dev_t *devp, int flag, int otyp, cred_t *cr)
{
	_NOTE(ARGUNUSED(flag, cr));

	if (applegmux == NULL || getminor(*devp) != 0)
		return (ENXIO);
	if (otyp != OTYP_CHR)
		return (EINVAL);
	return (0);
}

static int
applegmux_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	_NOTE(ARGUNUSED(dev, flag, otyp, cr));
	return (0);
}

static int
applegmux_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cr,
    int *rvalp)
{
	applegmux_t *gm = applegmux;
	applegmux_info_t gi;
	uint32_t val;
	int ret;

	_NOTE(ARGUNUSED(dev, cr, rvalp));

	if (gm == NULL)
		return (ENXIO);

	switch (cmd) {
	case APPLEGMUX_IOC_INFO:
		bzero(&gi, sizeof (gi));
		gi.gi_type = gm->gm_hw.gh_type;
		gi.gi_ver_major = gm->gm_ver[0];
		gi.gi_ver_minor = gm->gm_ver[1];
		gi.gi_ver_release = gm->gm_ver[2];
		gi.gi_max_brightness = gm->gm_max;
		gi.gi_base = gm->gm_base;
		mutex_enter(&gm->gm_lock);
		if (gm->gm_suspended) {
			ret = EAGAIN;
		} else if ((ret = gmux_get_brightness(&gm->gm_hw,
		    &gi.gi_brightness)) == 0) {
			ret = gmux_switch_state(&gm->gm_hw, &gi.gi_display,
			    &gi.gi_ddc, &gi.gi_external);
		}
		mutex_exit(&gm->gm_lock);
		if (ret != 0)
			return (ret == ETIMEDOUT ? EIO : ret);
		if (ddi_copyout(&gi, (void *)arg, sizeof (gi),
		    mode & FKIOCTL) != 0)
			return (EFAULT);
		return (0);

	case APPLEGMUX_IOC_GET_BRIGHTNESS:
		mutex_enter(&gm->gm_lock);
		ret = gm->gm_suspended ? EAGAIN :
		    gmux_get_brightness(&gm->gm_hw, &val);
		mutex_exit(&gm->gm_lock);
		if (ret != 0)
			return (ret == ETIMEDOUT ? EIO : ret);
		if (ddi_copyout(&val, (void *)arg, sizeof (val),
		    mode & FKIOCTL) != 0)
			return (EFAULT);
		return (0);

	case APPLEGMUX_IOC_SET_BRIGHTNESS:
		if (!(mode & FWRITE))
			return (EBADF);
		if (ddi_copyin((void *)arg, &val, sizeof (val),
		    mode & FKIOCTL) != 0)
			return (EFAULT);
		mutex_enter(&gm->gm_lock);
		ret = gm->gm_suspended ? EAGAIN :
		    gmux_set_brightness(&gm->gm_hw, gm->gm_max, val);
		mutex_exit(&gm->gm_lock);
		return (ret == ETIMEDOUT ? EIO : ret);

	default:
		return (ENOTTY);
	}
}

static int
applegmux_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **res)
{
	_NOTE(ARGUNUSED(dip, arg));

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (applegmux == NULL)
			return (DDI_FAILURE);
		*res = applegmux->gm_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*res = (void *)0;
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static void
applegmux_cleanup(applegmux_t *gm)
{
	ddi_remove_minor_node(gm->gm_dip, NULL);
	if (gm->gm_hw.gh_mmio != NULL)
		AcpiOsUnmapMemory((void *)gm->gm_hw.gh_mmio, GMUX_MMIO_LEN);
	mutex_destroy(&gm->gm_lock);
	kmem_free(gm, sizeof (*gm));
}

/*
 * Suspend keeps the brightness, resume writes it back: the gmux comes
 * out of S3 at its own default.
 */
static int
applegmux_resume(applegmux_t *gm)
{
	int ret = 0;

	mutex_enter(&gm->gm_lock);
	if (gm->gm_suspended && gm->gm_max != 0)
		ret = gmux_write32(&gm->gm_hw, GMUX_BRIGHTNESS, gm->gm_saved);
	gm->gm_suspended = B_FALSE;
	mutex_exit(&gm->gm_lock);
	if (ret != 0)
		dev_err(gm->gm_dip, CE_WARN, "could not restore the "
		    "brightness after resume");
	return (DDI_SUCCESS);
}

static int
applegmux_suspend(applegmux_t *gm)
{
	mutex_enter(&gm->gm_lock);
	if (gm->gm_max != 0 &&
	    gmux_get_brightness(&gm->gm_hw, &gm->gm_saved) != 0)
		gm->gm_saved = gm->gm_max;
	gm->gm_suspended = B_TRUE;
	mutex_exit(&gm->gm_lock);
	return (DDI_SUCCESS);
}

static int
applegmux_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	applegmux_t *gm;
	uint8_t display = 0, ddc = 0, external = 0;
	uint32_t bl = 0;

	switch (cmd) {
	case DDI_RESUME:
		if (applegmux == NULL || applegmux->gm_dip != dip)
			return (DDI_FAILURE);
		return (applegmux_resume(applegmux));
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	if (ddi_get_instance(dip) != 0 || applegmux != NULL)
		return (DDI_FAILURE);

	gm = kmem_zalloc(sizeof (*gm), KM_SLEEP);
	gm->gm_dip = dip;
	mutex_init(&gm->gm_lock, NULL, MUTEX_DRIVER, NULL);

	if (!gmux_find(gm)) {
		applegmux_cleanup(gm);
		return (DDI_FAILURE);
	}

	mutex_enter(&gm->gm_lock);
	if (gmux_version(&gm->gm_hw, gm->gm_ver) != 0 ||
	    gmux_max_brightness(&gm->gm_hw, &gm->gm_max) != 0) {
		mutex_exit(&gm->gm_lock);
		dev_err(dip, CE_WARN, "%s gmux at 0x%" PRIx64 " does not "
		    "answer", gmux_type_name(gm->gm_hw.gh_type), gm->gm_base);
		applegmux_cleanup(gm);
		return (DDI_FAILURE);
	}
	(void) gmux_get_brightness(&gm->gm_hw, &bl);
	(void) gmux_switch_state(&gm->gm_hw, &display, &ddc, &external);
	mutex_exit(&gm->gm_lock);

	if (ddi_create_minor_node(dip, "applegmux", S_IFCHR, 0, DDI_PSEUDO,
	    0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create minor node");
		applegmux_cleanup(gm);
		return (DDI_FAILURE);
	}

	applegmux = gm;
	ddi_report_dev(dip);
	dev_err(dip, CE_CONT, "?gmux %u.%u.%u (%s) at 0x%" PRIx64
	    ": backlight %u of %u, panel on the %s GPU\n", gm->gm_ver[0],
	    gm->gm_ver[1], gm->gm_ver[2], gmux_type_name(gm->gm_hw.gh_type),
	    gm->gm_base, bl, gm->gm_max,
	    (display & 1) ? "discrete" : "integrated");
	return (DDI_SUCCESS);
}

static int
applegmux_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	applegmux_t *gm = applegmux;

	if (gm == NULL || gm->gm_dip != dip)
		return (DDI_FAILURE);

	switch (cmd) {
	case DDI_SUSPEND:
		return (applegmux_suspend(gm));
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	applegmux = NULL;
	applegmux_cleanup(gm);
	return (DDI_SUCCESS);
}

static struct cb_ops applegmux_cb_ops = {
	.cb_open = applegmux_open,
	.cb_close = applegmux_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = applegmux_ioctl,
	.cb_devmap = nodev,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops applegmux_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = applegmux_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = applegmux_attach,
	.devo_detach = applegmux_detach,
	.devo_reset = nodev,
	.devo_cb_ops = &applegmux_cb_ops,
	.devo_power = NULL,
	.devo_quiesce = ddi_quiesce_not_needed
};

static struct modldrv applegmux_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Apple gmux backlight",
	.drv_dev_ops = &applegmux_dev_ops
};

static struct modlinkage applegmux_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &applegmux_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&applegmux_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&applegmux_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&applegmux_modlinkage));
}
