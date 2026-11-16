/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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
 * Intel LPSS SPI controller (PXA2xx SSP core plus the LPSS private
 * registers), Sunrise Point family: the 100/200-series PCH of the
 * Skylake/Kaby Lake MacBooks.  Register model and init sequence from
 * FreeBSD's intelspi(4) (sys/dev/intel/spi.c).
 *
 * Unlike FreeBSD, transfers are always done by polling the FIFOs: the
 * topcase messages are 256 bytes at about 8 MHz (some 300 us), and
 * polling also works for kmdb.  The controller's own interrupt is not
 * used.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/pci_cap.h>

#include "atopcase.h"

#define	SPI_RD(sc, off)		\
	ddi_get32((sc)->as_regh, (uint32_t *)((sc)->as_regs + (off)))
#define	SPI_WR(sc, off, v)	\
	ddi_put32((sc)->as_regh, (uint32_t *)((sc)->as_regs + (off)), (v))

#define	CS_CTRL		(LPSS_PRIV_SPT + LPSS_PRIV_CS_CTRL_SPT)

/* Upper bound for one transfer, in polling rounds of about 1 us */
static uint_t atopcase_spi_spin = 200000;

static void
atopcase_spi_set_cs(atopcase_t *sc, boolean_t high)
{
	uint32_t reg;

	reg = SPI_RD(sc, CS_CTRL);
	reg &= ~SPI_CS_CTRL_CS_MASK;
	reg |= SPI_CS_CTRL_SW_MODE;
	if (high)
		reg |= SPI_CS_CTRL_CS_HIGH;
	SPI_WR(sc, CS_CTRL, reg);
}

static void
atopcase_spi_hw_init(atopcase_t *sc)
{
	uint32_t reg, div;

	/* Take the controller out of reset (FreeBSD: Sunrise Point only) */
	SPI_WR(sc, LPSS_PRIV_SPT + LPSS_PRIV_RESETS,
	    LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA);

	SPI_WR(sc, SSP_SSCR0, 0);

	/* Chip select under software control, de-asserted */
	reg = SPI_RD(sc, CS_CTRL);
	reg &= ~SPI_CS_CTRL_CS_MASK;
	reg |= SPI_CS_CTRL_SW_MODE | SPI_CS_CTRL_CS_HIGH;
	SPI_WR(sc, CS_CTRL, reg);

	/* FIFO thresholds; no interrupts (we poll); clock mode */
	reg = SSCR1_TFT(2) | SSCR1_RFT(2);
	if (sc->as_spi_mode & ATOPCASE_SPI_MODE_CPHA)
		reg |= SSCR1_SPI_SPH;
	if (sc->as_spi_mode & ATOPCASE_SPI_MODE_CPOL)
		reg |= SSCR1_SPI_SPO;
	SPI_WR(sc, SSP_SSCR1, reg);

	div = (ATOPCASE_SSP_CLOCK + sc->as_spi_hz - 1) / sc->as_spi_hz;
	if (div < 1)
		div = 1;
	if (div > 0x1000)
		div = 0x1000;
	SPI_WR(sc, SSP_SSCR0, SSCR0_SCR(div) | SSCR0_FRF_SPI |
	    SSCR0_DSS(8) | SSCR0_SSE);
}

/*
 * Make sure the function is in D0 (Apple's firmware uses the keyboard,
 * so it normally is), then map BAR 0 and initialise the SSP.
 */
int
atopcase_spi_init(atopcase_t *sc)
{
	static ddi_device_acc_attr_t attr = {
		DDI_DEVICE_ATTR_V0,
		DDI_STRUCTURE_LE_ACC,
		DDI_STRICTORDER_ACC,
	};
	ddi_acc_handle_t cfg;
	uint16_t pmcap, pmcsr, cmd;

	if (pci_config_setup(sc->as_dip, &cfg) != DDI_SUCCESS) {
		dev_err(sc->as_dip, CE_WARN, "cannot map PCI config space");
		return (ENXIO);
	}
	if (PCI_CAP_LOCATE(cfg, PCI_CAP_ID_PM, &pmcap) == DDI_SUCCESS) {
		pmcsr = PCI_CAP_GET16(cfg, 0, pmcap, PCI_PMCSR);
		if ((pmcsr & PCI_PMCSR_STATE_MASK) != PCI_PMCSR_D0) {
			ATOPCASE_DBG(1, "SPI controller in D%d, setting D0",
			    pmcsr & PCI_PMCSR_STATE_MASK);
			PCI_CAP_PUT16(cfg, 0, pmcap, PCI_PMCSR,
			    pmcsr & ~PCI_PMCSR_STATE_MASK);
			delay(drv_usectohz(10000));
		}
	}
	cmd = pci_config_get16(cfg, PCI_CONF_COMM);
	if ((cmd & PCI_COMM_MAE) == 0)
		pci_config_put16(cfg, PCI_CONF_COMM, cmd | PCI_COMM_MAE);
	pci_config_teardown(&cfg);

	/* reg[0] is config space, reg[1] is BAR 0 */
	if (ddi_regs_map_setup(sc->as_dip, 1, &sc->as_regs, 0, 0, &attr,
	    &sc->as_regh) != DDI_SUCCESS) {
		dev_err(sc->as_dip, CE_WARN, "cannot map the SPI registers");
		return (ENXIO);
	}

	ATOPCASE_DBG(1, "LPSS clock 0x%x, resets 0x%x, cs 0x%x",
	    SPI_RD(sc, LPSS_PRIV_SPT + LPSS_PRIV_CLK),
	    SPI_RD(sc, LPSS_PRIV_SPT + LPSS_PRIV_RESETS),
	    SPI_RD(sc, CS_CTRL));
	if ((SPI_RD(sc, LPSS_PRIV_SPT + LPSS_PRIV_CLK) & LPSS_PRIV_CLK_EN) ==
	    0) {
		dev_err(sc->as_dip, CE_NOTE, "!LPSS clock gate is off "
		    "(firmware did not enable it)");
	}

	mutex_init(&sc->as_spi_lock, NULL, MUTEX_DRIVER, NULL);
	atopcase_spi_hw_init(sc);
	return (0);
}

void
atopcase_spi_fini(atopcase_t *sc)
{
	if (sc->as_regh != NULL) {
		SPI_WR(sc, SSP_SSCR0, 0);
		atopcase_spi_set_cs(sc, B_TRUE);
		ddi_regs_map_free(&sc->as_regh);
		sc->as_regh = NULL;
		mutex_destroy(&sc->as_spi_lock);
	}
}

/*
 * Full-duplex transfer of len bytes: tx may be NULL (send zeros), rx may
 * be NULL (discard).  Chip select stays asserted afterwards if keep_cs is
 * set (the topcase write is followed by a status read under the same CS,
 * as in FreeBSD's SPI_FLAG_KEEP_CS).
 */
int
atopcase_spi_xfer(atopcase_t *sc, const void *txv, void *rxv, size_t len,
    boolean_t keep_cs)
{
	const uint8_t *tx = txv;
	uint8_t *rx = rxv;
	size_t w = 0, r = 0;
	uint_t spin = atopcase_spi_spin;
	uint32_t sssr, b;
	int err = 0;

	if (!sc->as_polled)
		mutex_enter(&sc->as_spi_lock);

	/* Drop anything stale in the receive FIFO */
	while (SPI_RD(sc, SSP_SSSR) & SSSR_RNE)
		(void) SPI_RD(sc, SSP_SSDR);

	atopcase_spi_set_cs(sc, B_FALSE);
	drv_usecwait(sc->as_cs_delay_us);

	while (r < len) {
		sssr = SPI_RD(sc, SSP_SSSR);
		while (w < len && (w - r) < ATOPCASE_SPI_FIFO &&
		    (sssr & SSSR_TNF)) {
			SPI_WR(sc, SSP_SSDR, tx != NULL ? tx[w] : 0);
			w++;
			sssr = SPI_RD(sc, SSP_SSSR);
		}
		while (r < w && (sssr & SSSR_RNE)) {
			b = SPI_RD(sc, SSP_SSDR) & 0xff;
			if (rx != NULL)
				rx[r] = (uint8_t)b;
			r++;
			sssr = SPI_RD(sc, SSP_SSSR);
		}
		if (r < len) {
			if (spin-- == 0) {
				err = EIO;
				break;
			}
			drv_usecwait(1);
		}
	}

	if (!keep_cs || err != 0)
		atopcase_spi_set_cs(sc, B_TRUE);

	if (!sc->as_polled)
		mutex_exit(&sc->as_spi_lock);

	if (err != 0) {
		dev_err(sc->as_dip, CE_WARN, "!SPI transfer timeout "
		    "(%lu of %lu bytes, sssr 0x%x)", (ulong_t)r, (ulong_t)len,
		    SPI_RD(sc, SSP_SSSR));
		atopcase_spi_hw_init(sc);
	}
	return (err);
}
