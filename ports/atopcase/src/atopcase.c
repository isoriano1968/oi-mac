/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021-2023 Val Packett <val@packett.cool>
 * Copyright (c) 2023 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright 2026 isoriano1968
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * atopcase: DDI glue, ACPI and the worker thread.  The ACPI part
 * follows FreeBSD's sys/dev/atopcase/atopcase_acpi.c.
 *
 * The driver binds to the Intel LPSS SPI PCI functions (see the package's
 * driver aliases).  On attach it looks for an ACPI device with _HID
 * APP000D whose parent is this PCI function (bus/device/function against
 * the parent's _ADR); on any other machine, or on the other SPI
 * controller, attach fails and nothing is touched.
 *
 * Events: FreeBSD uses the device interrupt from _CRS, falls back to
 * polling on an interrupt storm and uses the ACPI GPE (_GPE) when there
 * is no interrupt resource; Linux uses only the GPE, which Apple's DSDT
 * wires up when the OS answers _OSI("Darwin").  illumos answers Windows
 * only, like FreeBSD.  Here: if _GPE exists, a GPE handler wakes the
 * worker; if no GPE arrives while the topcase boots, the worker polls
 * (FreeBSD's fallback: 10 ms while keys come in, longer when idle).
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/sunndi.h>
#include <sys/acpi/acpi.h>
#include <sys/acpica.h>

#include "atopcase.h"

int atopcase_debug = 0;
/* Polling intervals: while reports come in, and when idle */
int atopcase_busy_poll_ms = 8;
int atopcase_idle_poll_ms = 20;
/* 1 = never use the GPE, always poll */
int atopcase_force_poll = 0;
/* Keyboard backlight at attach, percent (0 = off) */
int atopcase_backlight = 0;

atopcase_t *atopcase_state;

static const char *atopcase_hids[] = { "APP000D", NULL };

typedef struct atopcase_find {
	ACPI_HANDLE	af_handle;
	int		af_count;
} atopcase_find_t;

static ACPI_STATUS
atopcase_find_cb(ACPI_HANDLE h, UINT32 level, void *ctx, void **rv)
{
	atopcase_find_t *af = ctx;
	int sta;

	/* No _STA, or present: take it (FreeBSD acpi_is_atopcase) */
	if (acpica_eval_int(h, "_STA", &sta) == AE_OK && (sta & 0x1) == 0)
		return (AE_OK);
	if (af->af_count++ == 0)
		af->af_handle = h;
	return (AE_OK);
}

/*
 * Is the APP000D device's parent the PCI function we attach to?
 * _ADR of a PCI device is (device << 16) | function.
 */
static boolean_t
atopcase_parent_matches(atopcase_t *sc, ACPI_HANDLE ctrl)
{
	pci_regspec_t *regs;
	uint_t nregs;
	int adr, dev, func;

	if (acpica_eval_int(ctrl, "_ADR", &adr) != AE_OK)
		return (B_FALSE);
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, sc->as_dip,
	    DDI_PROP_DONTPASS, "reg", (int **)&regs, &nregs) !=
	    DDI_PROP_SUCCESS)
		return (B_FALSE);
	dev = PCI_REG_DEV_G(regs[0].pci_phys_hi);
	func = PCI_REG_FUNC_G(regs[0].pci_phys_hi);
	ddi_prop_free(regs);

	ATOPCASE_DBG(1, "controller _ADR 0x%x, PCI dev %d func %d", adr, dev,
	    func);
	return ((uint32_t)adr == (((uint32_t)dev << 16) | (uint32_t)func));
}

static int
atopcase_acpi_find(atopcase_t *sc)
{
	atopcase_find_t af;
	ACPI_HANDLE parent;
	const char **hid;

	for (hid = atopcase_hids; *hid != NULL; hid++) {
		bzero(&af, sizeof (af));
		(void) AcpiGetDevices((char *)*hid, atopcase_find_cb, &af,
		    NULL);
		if (af.af_count == 0)
			continue;
		if (AcpiGetParent(af.af_handle, &parent) != AE_OK)
			continue;
		if (!atopcase_parent_matches(sc, parent))
			continue;
		sc->as_handle = af.af_handle;
		sc->as_ctrl_handle = parent;
		(void) strlcpy(sc->as_hid, *hid, sizeof (sc->as_hid));
		return (0);
	}
	return (ENXIO);
}

/* SpiSerialBus resource of the topcase: clock and mode */
static ACPI_STATUS
atopcase_crs_cb(ACPI_RESOURCE *res, void *ctx)
{
	atopcase_t *sc = ctx;
	ACPI_RESOURCE_SPI_SERIALBUS *spi;

	if (res->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return (AE_OK);
	spi = &res->Data.SpiSerialBus;
	if (spi->Type != ACPI_RESOURCE_SERIAL_TYPE_SPI)
		return (AE_OK);

	if (spi->ConnectionSpeed != 0)
		sc->as_spi_hz = spi->ConnectionSpeed;
	sc->as_spi_mode = 0;
	if (spi->ClockPhase == ACPI_SPI_SECOND_PHASE)
		sc->as_spi_mode |= ATOPCASE_SPI_MODE_CPHA;
	if (spi->ClockPolarity == ACPI_SPI_START_HIGH)
		sc->as_spi_mode |= ATOPCASE_SPI_MODE_CPOL;
	ATOPCASE_DBG(1, "_CRS SPI: %u Hz, %u bits, phase %u, polarity %u",
	    spi->ConnectionSpeed, spi->DataBitLength, spi->ClockPhase,
	    spi->ClockPolarity);
	return (AE_CTRL_TERMINATE);
}

static int
atopcase_acpi_comm(atopcase_t *sc, char *method, boolean_t on)
{
	ACPI_OBJECT arg;
	ACPI_OBJECT_LIST args;

	arg.Type = ACPI_TYPE_INTEGER;
	arg.Integer.Value = on ? 1 : 0;
	args.Count = 1;
	args.Pointer = &arg;
	if (AcpiEvaluateObject(sc->as_handle, method, &args, NULL) != AE_OK)
		return (ENXIO);
	drv_usecwait(100);
	return (0);
}

/* --- events --- */

static uint_t
atopcase_softint(caddr_t arg1, caddr_t arg2)
{
	atopcase_t *sc = (atopcase_t *)arg1;

	mutex_enter(&sc->as_lock);
	sc->as_gpe_pending = B_TRUE;
	cv_signal(&sc->as_cv);
	mutex_exit(&sc->as_lock);
	return (DDI_INTR_CLAIMED);
}

/*
 * ACPI GPE handler (SCI context): count, wake the worker by a soft
 * interrupt.  The GPE stays disabled until the worker has read the
 * pending packets and calls AcpiFinishGpe().
 */
static UINT32
atopcase_gpe_handler(ACPI_HANDLE gpe_dev, UINT32 gpe, void *ctx)
{
	atopcase_t *sc = ctx;

	atomic_inc_32(&sc->as_gpe_count);
	if (sc->as_softint_added)
		(void) ddi_intr_trigger_softint(sc->as_softint, NULL);
	return (0);
}

void
atopcase_request_leds(atopcase_t *sc, int leds)
{
	mutex_enter(&sc->as_lock);
	sc->as_led_state = leds;
	sc->as_led_pending = B_TRUE;
	cv_signal(&sc->as_cv);
	mutex_exit(&sc->as_lock);
}

static void
atopcase_worker(void *arg)
{
	atopcase_t *sc = arg;
	boolean_t busy = B_FALSE, gpe, led;
	clock_t ticks;
	uint8_t ledrep[2];
	int leds, n, err;

	mutex_enter(&sc->as_lock);
	while (!sc->as_stop) {
		if (!sc->as_led_pending && !sc->as_gpe_pending) {
			if (sc->as_use_gpe)
				ticks = drv_usectohz(1000000);
			else
				ticks = drv_usectohz(1000 * (busy ?
				    atopcase_busy_poll_ms :
				    atopcase_idle_poll_ms));
			(void) cv_reltimedwait(&sc->as_cv, &sc->as_lock,
			    ticks, TR_CLOCK_TICK);
		}
		if (sc->as_stop)
			break;
		led = sc->as_led_pending;
		leds = sc->as_led_state;
		sc->as_led_pending = B_FALSE;
		gpe = sc->as_gpe_pending;
		sc->as_gpe_pending = B_FALSE;
		mutex_exit(&sc->as_lock);

		if (led) {
			ledrep[0] = ATOPCASE_KBD_LED_RID;
			ledrep[1] = (uint8_t)atopcase_kbd_leds_to_hid(leds);
			(void) atopcase_set_report(sc, ATOPCASE_DEV_KBRD,
			    ATOPCASE_KBD_LED_RID, ledrep, sizeof (ledrep));
		}

		/* Read until the topcase has nothing more (bounded) */
		for (n = 0; n < 16; n++) {
			err = atopcase_receive_packet(sc);
			if (err == EAGAIN)
				break;
		}
		busy = (n > 0);

		if (gpe && sc->as_use_gpe)
			(void) AcpiFinishGpe(NULL, sc->as_gpe);

		mutex_enter(&sc->as_lock);
	}
	sc->as_thread_done = B_TRUE;
	cv_broadcast(&sc->as_cv);
	mutex_exit(&sc->as_lock);
	thread_exit();
}

/* --- attach / detach --- */

static void
atopcase_teardown(atopcase_t *sc)
{
	if (sc->as_thread != NULL) {
		mutex_enter(&sc->as_lock);
		sc->as_stop = B_TRUE;
		cv_broadcast(&sc->as_cv);
		while (!sc->as_thread_done)
			cv_wait(&sc->as_cv, &sc->as_lock);
		mutex_exit(&sc->as_lock);
		sc->as_thread = NULL;
	}
	if (sc->as_gpe_installed) {
		(void) AcpiDisableGpe(NULL, sc->as_gpe);
		(void) AcpiRemoveGpeHandler(NULL, sc->as_gpe,
		    atopcase_gpe_handler);
		sc->as_gpe_installed = B_FALSE;
	}
	if (sc->as_softint_added) {
		(void) ddi_intr_remove_softint(sc->as_softint);
		sc->as_softint_added = B_FALSE;
	}
	if (sc->as_handle != NULL)
		(void) atopcase_acpi_comm(sc, "SIEN", B_FALSE);
	if (sc->as_minor_created) {
		ddi_remove_minor_node(sc->as_dip, NULL);
		sc->as_minor_created = B_FALSE;
	}
	atopcase_spi_fini(sc);
	atopcase_kbd_fini(sc);
	atopcase_tpad_fini(sc);
	cv_destroy(&sc->as_cv);
	mutex_destroy(&sc->as_lock);
}

static int
atopcase_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	atopcase_t *sc;
	int usb_on, spi_on, gpe;
	uint32_t gpes_before;

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);
	if (atopcase_state != NULL)
		return (DDI_FAILURE);	/* one topcase per machine */

	sc = kmem_zalloc(sizeof (*sc), KM_SLEEP);
	sc->as_dip = dip;
	sc->as_instance = ddi_get_instance(dip);
	sc->as_spi_hz = ATOPCASE_SPI_DEFAULT_HZ;
	sc->as_cs_delay_us = 10;	/* FreeBSD's default */
	mutex_init(&sc->as_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&sc->as_cv, NULL, CV_DRIVER, NULL);
	atopcase_kbd_init(sc);
	atopcase_tpad_init(sc);

	if (atopcase_acpi_find(sc) != 0) {
		/* Not the topcase's controller (or not a MacBook): quietly */
		ATOPCASE_DBG(1, "no APP000D below this SPI controller");
		goto fail;
	}

	/* If the USB interface of the topcase is active, leave it to USB */
	if (acpica_eval_int(sc->as_handle, "UIST", &usb_on) == AE_OK &&
	    usb_on != 0) {
		dev_err(dip, CE_NOTE, "!topcase uses its USB interface");
		sc->as_handle = NULL;
		goto fail;
	}

	(void) AcpiWalkResources(sc->as_handle, METHOD_NAME__CRS,
	    atopcase_crs_cb, sc);

	if (atopcase_spi_init(sc) != 0) {
		sc->as_handle = NULL;
		goto fail;
	}

	/* GPE, unless polling is forced; must be ready before SIEN(1) */
	if (!atopcase_force_poll &&
	    acpica_eval_int(sc->as_handle, "_GPE", &gpe) == AE_OK) {
		sc->as_gpe = (uint32_t)gpe;
		if (ddi_intr_add_softint(dip, &sc->as_softint,
		    DDI_INTR_SOFTPRI_DEFAULT, atopcase_softint, (caddr_t)sc) ==
		    DDI_SUCCESS)
			sc->as_softint_added = B_TRUE;
		if (sc->as_softint_added &&
		    AcpiInstallGpeHandler(NULL, sc->as_gpe,
		    ACPI_GPE_LEVEL_TRIGGERED, atopcase_gpe_handler, sc) ==
		    AE_OK) {
			sc->as_gpe_installed = B_TRUE;
			if (AcpiEnableGpe(NULL, sc->as_gpe) != AE_OK) {
				(void) AcpiRemoveGpeHandler(NULL, sc->as_gpe,
				    atopcase_gpe_handler);
				sc->as_gpe_installed = B_FALSE;
			}
		}
	}
	gpes_before = sc->as_gpe_count;

	/* Restart SPI communication so that the topcase sends "booted" */
	if (acpica_eval_int(sc->as_handle, "SIST", &spi_on) != AE_OK) {
		dev_err(dip, CE_WARN, "cannot read the topcase SPI state "
		    "(SIST)");
		goto fail_hw;
	}
	if (spi_on != 0 && atopcase_acpi_comm(sc, "SIEN", B_FALSE) != 0) {
		dev_err(dip, CE_WARN, "cannot disable topcase SPI (SIEN)");
		goto fail_hw;
	}
	if (atopcase_acpi_comm(sc, "SIEN", B_TRUE) != 0) {
		dev_err(dip, CE_WARN, "cannot enable topcase SPI (SIEN)");
		goto fail_hw;
	}

	if (atopcase_proto_init(sc) != 0)
		goto fail_hw;

	/* GPE mode only if the topcase actually raised GPEs while booting */
	if (sc->as_gpe_installed && sc->as_gpe_count != gpes_before) {
		sc->as_use_gpe = B_TRUE;
		(void) AcpiFinishGpe(NULL, sc->as_gpe);
	} else if (sc->as_gpe_installed) {
		(void) AcpiDisableGpe(NULL, sc->as_gpe);
		(void) AcpiRemoveGpeHandler(NULL, sc->as_gpe,
		    atopcase_gpe_handler);
		sc->as_gpe_installed = B_FALSE;
	}

	dev_err(dip, CE_CONT, "?%s: %s %s (%04x:%04x v%x), keyboard \"%s\", "
	    "trackpad \"%s\", %s, SPI %u Hz mode %u\n", sc->as_hid,
	    sc->as_vendor, sc->as_product, sc->as_vid, sc->as_pid, sc->as_ver,
	    sc->as_kb.acc_name, sc->as_tp.acc_name,
	    sc->as_use_gpe ? "ACPI GPE" : "polled", sc->as_spi_hz,
	    sc->as_spi_mode);

	if (atopcase_backlight > 0)
		(void) atopcase_set_backlight(sc, atopcase_backlight);

	(void) ddi_prop_update_byte_array(DDI_DEV_T_NONE, dip,
	    "keyboard-hid-descriptor", sc->as_kb.acc_rdesc,
	    sc->as_kb.acc_rdesc_len);
	(void) ddi_prop_update_byte_array(DDI_DEV_T_NONE, dip,
	    "trackpad-hid-descriptor", sc->as_tp.acc_rdesc,
	    sc->as_tp.acc_rdesc_len);

	sc->as_thread = thread_create(NULL, 0, atopcase_worker, sc, 0, &p0,
	    TS_RUN, minclsyspri);

	atopcase_state = sc;
	ddi_set_driver_private(dip, sc);

	if (ddi_create_minor_node(dip, "keyboard", S_IFCHR,
	    ATOPCASE_MINOR_KBD, DDI_NT_KEYBOARD, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "cannot create the keyboard node");
		atopcase_state = NULL;
		goto fail_hw;
	}
	sc->as_minor_created = B_TRUE;

	/*
	 * The trackpad as a relative mouse (/dev/atopcase/mouse), if its
	 * descriptor has such a report.  Without it the keyboard still works.
	 */
	if (atopcase_tpad_setup(sc) &&
	    ddi_create_minor_node(dip, "mouse", S_IFCHR, ATOPCASE_MINOR_MOUSE,
	    DDI_NT_MOUSE, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "cannot create the mouse node");
		sc->as_tpad.at_parsed = B_FALSE;
	}

	ddi_report_dev(dip);
	return (DDI_SUCCESS);

fail_hw:
	atopcase_teardown(sc);
	kmem_free(sc, sizeof (*sc));
	return (DDI_FAILURE);
fail:
	atopcase_spi_fini(sc);
	atopcase_kbd_fini(sc);
	atopcase_tpad_fini(sc);
	cv_destroy(&sc->as_cv);
	mutex_destroy(&sc->as_lock);
	kmem_free(sc, sizeof (*sc));
	return (DDI_FAILURE);
}

static int
atopcase_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	atopcase_t *sc = ddi_get_driver_private(dip);

	if (cmd != DDI_DETACH || sc == NULL)
		return (DDI_FAILURE);
	if (sc->as_kbd.ak_open)
		return (DDI_FAILURE);	/* linked under conskbd */

	atopcase_state = NULL;
	atopcase_teardown(sc);
	ddi_set_driver_private(dip, NULL);
	kmem_free(sc, sizeof (*sc));
	return (DDI_SUCCESS);
}

static int
atopcase_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **result)
{
	atopcase_t *sc = atopcase_state;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (sc == NULL)
			return (DDI_FAILURE);
		*result = sc->as_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		if (sc == NULL)
			return (DDI_FAILURE);
		*result = (void *)(uintptr_t)sc->as_instance;
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static struct cb_ops atopcase_cb_ops = {
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
	.cb_str = &atopcase_str_info,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops atopcase_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = atopcase_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = atopcase_attach,
	.devo_detach = atopcase_detach,
	.devo_reset = nodev,
	.devo_cb_ops = &atopcase_cb_ops,
	.devo_bus_ops = NULL,
	.devo_power = NULL,
	.devo_quiesce = ddi_quiesce_not_needed
};

static struct modldrv atopcase_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Apple MacBook SPI keyboard",
	.drv_dev_ops = &atopcase_dev_ops
};

static struct modlinkage atopcase_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &atopcase_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&atopcase_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&atopcase_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&atopcase_modlinkage, modinfop));
}
