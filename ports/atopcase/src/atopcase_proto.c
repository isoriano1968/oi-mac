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
 * The topcase packet protocol, from FreeBSD sys/dev/atopcase/atopcase.c.
 *
 * Every SPI transfer is one 256-byte packet: direction, device, offset,
 * remaining, length, 246 data bytes, CRC16.  Messages (header, payload,
 * CRC16) may span two packets.  A host write is followed by a 4-byte
 * status read (ac 27 68 d5 = accepted) and then by the device's reply
 * packet.  When nothing is pending the device answers with direction
 * 0x80.
 *
 * Only the polling path of FreeBSD is ported: every function here runs
 * in the attach thread, in the worker thread or (as_polled) in kmdb,
 * never concurrently.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/byteorder.h>

#include "atopcase.h"

static const uint8_t booted[] = { 0xa0, 0x80, 0x00, 0x00 };
static const uint8_t status_ok[] = { 0xac, 0x27, 0x68, 0xd5 };

/* CRC-16/ARC (poly 0x8005 reflected, init as given): FreeBSD crc16() */
uint16_t
atopcase_crc16(uint16_t crc, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	int i;

	while (len-- > 0) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
	}
	return (crc);
}

#define	ATOPCASE_SPI_PAUSE()	drv_usecwait(100)

static atopcase_child_t *
atopcase_child(atopcase_t *sc, uint8_t device)
{
	switch (device) {
	case ATOPCASE_DEV_KBRD:
		return (&sc->as_kb);
	case ATOPCASE_DEV_TPAD:
		return (&sc->as_tp);
	default:
		return (NULL);
	}
}

static void
atopcase_copystr(char *dst, size_t dstsz, const uint8_t *payload,
    uint16_t pl_len, uint16_t len, uint16_t off)
{
	size_t n;

	len = LE_16(len);
	off = LE_16(off);
	if ((uint32_t)len + off > pl_len)
		return;
	n = MIN(len, dstsz - 1);
	bcopy(payload + off, dst, n);
	dst[n] = '\0';
}

static int
atopcase_process_message(atopcase_t *sc, uint8_t device, uint8_t *msg,
    uint16_t msg_len)
{
	atopcase_header_t *hdr = (atopcase_header_t *)msg;
	atopcase_child_t *ac;
	uint8_t *payload;
	uint16_t pl_len, crc, type;

	if (msg_len < sizeof (*hdr) + sizeof (crc))
		return (EIO);

	payload = msg + sizeof (*hdr);
	pl_len = LE_16(hdr->ah_len);
	type = LE_16(hdr->ah_type);

	if (pl_len + sizeof (*hdr) + sizeof (crc) != msg_len) {
		ATOPCASE_DBG(2, "message with length overflow");
		return (EIO);
	}

	bcopy(payload + pl_len, &crc, sizeof (crc));
	if (LE_16(crc) != atopcase_crc16(0, msg, msg_len - sizeof (crc))) {
		ATOPCASE_DBG(2, "message with failed checksum");
		return (EIO);
	}

	if ((ac = atopcase_child(sc, device)) != NULL &&
	    type == ATOPCASE_MSG_TYPE_REPORT(device)) {
		ac->acc_reports++;
		if (device == ATOPCASE_DEV_KBRD)
			atopcase_kbd_report(sc, payload, pl_len);
		else if (device == ATOPCASE_DEV_TPAD)
			atopcase_tpad_report(sc, payload, pl_len);
	} else if (device == ATOPCASE_DEV_INFO &&
	    type == ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_IFACE) &&
	    (ac = atopcase_child(sc, hdr->ah_type_arg)) != NULL &&
	    pl_len >= sizeof (atopcase_iface_info_t)) {
		atopcase_iface_info_t *ii = (atopcase_iface_info_t *)payload;

		atopcase_copystr(ac->acc_name, sizeof (ac->acc_name), payload,
		    pl_len, ii->aii_name_len, ii->aii_name_off);
		ATOPCASE_DBG(1, "interface %d: %s", ac->acc_device,
		    ac->acc_name);
	} else if (device == ATOPCASE_DEV_INFO &&
	    type == ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_DESCRIPTOR) &&
	    (ac = atopcase_child(sc, hdr->ah_type_arg)) != NULL) {
		ac->acc_rdesc_len = MIN(pl_len, sizeof (ac->acc_rdesc));
		bcopy(payload, ac->acc_rdesc, ac->acc_rdesc_len);
		ATOPCASE_DBG(1, "interface %d: HID report descriptor, %u "
		    "bytes", ac->acc_device, ac->acc_rdesc_len);
	} else if (device == ATOPCASE_DEV_INFO &&
	    type == ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_DEVICE) &&
	    hdr->ah_type_arg == ATOPCASE_INFO_DEVICE &&
	    pl_len >= sizeof (atopcase_device_info_t)) {
		atopcase_device_info_t *di = (atopcase_device_info_t *)payload;

		sc->as_vid = LE_16(di->adi_vid);
		sc->as_pid = LE_16(di->adi_pid);
		sc->as_ver = LE_16(di->adi_ver);
		atopcase_copystr(sc->as_vendor, sizeof (sc->as_vendor),
		    payload, pl_len, di->adi_vendor_len, di->adi_vendor_off);
		atopcase_copystr(sc->as_product, sizeof (sc->as_product),
		    payload, pl_len, di->adi_product_len, di->adi_product_off);
		atopcase_copystr(sc->as_serial, sizeof (sc->as_serial),
		    payload, pl_len, di->adi_serial_len, di->adi_serial_off);
	} else {
		ATOPCASE_DBG(2, "unhandled message: device 0x%x type 0x%x "
		    "len %u", device, type, pl_len);
	}

	return (0);
}

/*
 * Read one packet.  Returns 0 for a processed packet (or a fragment of a
 * message), EAGAIN when the device had nothing to send, EIO for a bad
 * packet or SPI error.
 */
int
atopcase_receive_packet(atopcase_t *sc)
{
	atopcase_packet_t pkt;
	uint16_t length, remaining, offset, msg_len;
	uint8_t *msg;
	int err;

	bzero(&sc->as_junk, sizeof (sc->as_junk));
	err = atopcase_spi_xfer(sc, &sc->as_junk, &pkt, sizeof (pkt), B_FALSE);
	ATOPCASE_SPI_PAUSE();
	if (err != 0)
		return (err);

	if (LE_16(pkt.ap_checksum) !=
	    atopcase_crc16(0, &pkt, sizeof (pkt) - 2)) {
		sc->as_bad_packets++;
		ATOPCASE_DBG(3, "packet with failed checksum: %02x %02x %02x "
		    "%02x", ((uint8_t *)&pkt)[0], ((uint8_t *)&pkt)[1],
		    ((uint8_t *)&pkt)[2], ((uint8_t *)&pkt)[3]);
		return (EIO);
	}

	if (pkt.ap_direction == ATOPCASE_DIR_NOTHING)
		return (EAGAIN);

	if (pkt.ap_direction != ATOPCASE_DIR_READ &&
	    pkt.ap_direction != ATOPCASE_DIR_WRITE) {
		ATOPCASE_DBG(2, "unknown direction 0x%x", pkt.ap_direction);
		return (EIO);
	}

	length = LE_16(pkt.ap_length);
	remaining = LE_16(pkt.ap_remaining);
	offset = LE_16(pkt.ap_offset);

	if (length > sizeof (pkt.ap_data)) {
		ATOPCASE_DBG(2, "packet with length overflow: %u", length);
		return (EIO);
	}

	if (pkt.ap_direction == ATOPCASE_DIR_READ &&
	    pkt.ap_device == ATOPCASE_DEV_INFO &&
	    length == sizeof (booted) &&
	    bcmp(pkt.ap_data, booted, length) == 0) {
		ATOPCASE_DBG(1, "boot packet");
		sc->as_booted = B_TRUE;
		return (0);
	}

	/* multi-packet messages */
	if (remaining != 0 || offset != 0) {
		if (offset != sc->as_msg_len) {
			ATOPCASE_DBG(2, "unexpected offset %u, expected %u",
			    offset, sc->as_msg_len);
			sc->as_msg_len = 0;
			return (EIO);
		}
		if ((size_t)remaining + length + offset >
		    sizeof (sc->as_msg)) {
			ATOPCASE_DBG(2, "message too long: %lu",
			    (ulong_t)remaining + length + offset);
			sc->as_msg_len = 0;
			return (EIO);
		}
		bcopy(pkt.ap_data, sc->as_msg + offset, length);
		sc->as_msg_len += length;
		if (remaining != 0)
			return (0);
		msg = sc->as_msg;
		msg_len = sc->as_msg_len;
	} else {
		msg = pkt.ap_data;
		msg_len = length;
	}
	sc->as_msg_len = 0;

	return (atopcase_process_message(sc, pkt.ap_device, msg, msg_len));
}

static int
atopcase_receive_status(atopcase_t *sc)
{
	uint8_t dummy[4] = { 0 };
	uint8_t status[4] = { 0 };
	int err;

	err = atopcase_spi_xfer(sc, dummy, status, sizeof (status), B_FALSE);
	ATOPCASE_SPI_PAUSE();
	if (err != 0)
		return (err);

	if (bcmp(status, status_ok, sizeof (status_ok)) != 0) {
		ATOPCASE_DBG(1, "write not accepted: %02x %02x %02x %02x",
		    status[0], status[1], status[2], status[3]);
		return (EIO);
	}
	return (0);
}

/*
 * Write a packet, read the status, then read packets until the reply
 * (or anything else) arrives; replies are processed like any packet.
 */
static int
atopcase_send(atopcase_t *sc, atopcase_packet_t *pkt)
{
	int err, retries;

	err = atopcase_spi_xfer(sc, pkt, &sc->as_junk, sizeof (*pkt), B_TRUE);
	ATOPCASE_SPI_PAUSE();
	if (err != 0)
		return (err);

	if ((err = atopcase_receive_status(sc)) != 0)
		return (err);

	/* Backlight setting may take a while (FreeBSD: 20 x 1 ms) */
	retries = 20;
	while ((err = atopcase_receive_packet(sc)) == EAGAIN &&
	    --retries != 0)
		drv_usecwait(1000);
	if (err != 0)
		ATOPCASE_DBG(1, "write ack read failed: %d", err);
	return (err);
}

static void
atopcase_create_message(atopcase_t *sc, atopcase_packet_t *pkt,
    uint8_t device, uint16_t type, uint8_t type_arg, const void *payload,
    uint8_t len, uint16_t resp_len)
{
	atopcase_header_t *hdr = (atopcase_header_t *)pkt->ap_data;
	uint16_t msg_crc, pkt_len;

	ASSERT(len <= ATOPCASE_DATA_SIZE - sizeof (*hdr) - 2);

	bzero(pkt, sizeof (*pkt));
	pkt->ap_direction = ATOPCASE_DIR_WRITE;
	pkt->ap_device = device;
	pkt_len = sizeof (*hdr) + len + 2;
	pkt->ap_length = LE_16(pkt_len);

	hdr->ah_type = LE_16(type);
	hdr->ah_type_arg = type_arg;
	hdr->ah_seq_no = sc->as_seq_no++;
	hdr->ah_resp_len = LE_16((resp_len == 0) ? len : resp_len);
	hdr->ah_len = LE_16(len);

	if (len > 0)
		bcopy(payload, pkt->ap_data + sizeof (*hdr), len);
	msg_crc = LE_16(atopcase_crc16(0, pkt->ap_data, pkt_len - 2));
	bcopy(&msg_crc, pkt->ap_data + sizeof (*hdr) + len, 2);
	pkt->ap_checksum = LE_16(atopcase_crc16(0, pkt, sizeof (*pkt) - 2));
}

int
atopcase_request_desc(atopcase_t *sc, uint16_t type, uint8_t device)
{
	atopcase_create_message(sc, &sc->as_buf, ATOPCASE_DEV_INFO, type,
	    device, NULL, 0, 0x200);
	return (atopcase_send(sc, &sc->as_buf));
}

int
atopcase_set_report(atopcase_t *sc, uint8_t device, uint8_t id,
    const void *buf, uint8_t len)
{
	if (len >= ATOPCASE_DATA_SIZE - sizeof (atopcase_header_t) - 2)
		return (EINVAL);
	atopcase_create_message(sc, &sc->as_buf, device,
	    ATOPCASE_MSG_TYPE_SET_REPORT(device, id), 0, buf, len, 0);
	return (atopcase_send(sc, &sc->as_buf));
}

/* Keyboard backlight, percent 0-100 (FreeBSD: hardware range 32-255) */
int
atopcase_set_backlight(atopcase_t *sc, uint_t percent)
{
	atopcase_bl_payload_t bl;

	if (percent > 100)
		percent = 100;
	bzero(&bl, sizeof (bl));
	bl.abl_report_id = ATOPCASE_BKL_REPORT_ID;
	bl.abl_device = ATOPCASE_DEV_KBRD;
	bl.abl_level = LE_16((percent == 0) ? 0 : (32 + (223 * percent / 100)));
	bl.abl_status = LE_16((percent > 0) ? 0x01F4 : 0x1);
	return (atopcase_set_report(sc, ATOPCASE_DEV_KBRD,
	    ATOPCASE_BKL_REPORT_ID, &bl, sizeof (bl)));
}

/*
 * After SIEN(1): wait for the boot packet, then fetch the device info and
 * the keyboard and trackpad interface names and HID descriptors.
 */
int
atopcase_proto_init(atopcase_t *sc)
{
	int i, err;

	sc->as_kb.acc_device = ATOPCASE_DEV_KBRD;
	sc->as_tp.acc_device = ATOPCASE_DEV_TPAD;

	for (i = 0; i < 50 && !sc->as_booted; i++) {
		err = atopcase_receive_packet(sc);
		if (err == EAGAIN || err == EIO)
			delay(drv_usectohz(10000));
	}
	if (!sc->as_booted)
		dev_err(sc->as_dip, CE_NOTE, "!no boot packet from the "
		    "topcase, trying to talk to it anyway");

	/* The management device may send one message after power on */
	drv_usecwait(2000);
	for (i = 0; i < 5 && atopcase_receive_packet(sc) != EAGAIN; i++)
		;

	err = atopcase_request_desc(sc,
	    ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_DEVICE),
	    ATOPCASE_INFO_DEVICE);
	if (err != 0) {
		dev_err(sc->as_dip, CE_WARN, "no device descriptor from the "
		    "topcase (error %d)", err);
		return (err);
	}

	err = atopcase_request_desc(sc,
	    ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_IFACE), ATOPCASE_DEV_KBRD);
	if (err == 0)
		err = atopcase_request_desc(sc,
		    ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_DESCRIPTOR),
		    ATOPCASE_DEV_KBRD);
	if (err != 0) {
		dev_err(sc->as_dip, CE_WARN, "no keyboard descriptor from the "
		    "topcase (error %d)", err);
		return (err);
	}

	/* Trackpad: names and descriptor only, for the log (not used yet) */
	if (atopcase_request_desc(sc,
	    ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_IFACE),
	    ATOPCASE_DEV_TPAD) == 0) {
		(void) atopcase_request_desc(sc,
		    ATOPCASE_MSG_TYPE_INFO(ATOPCASE_INFO_DESCRIPTOR),
		    ATOPCASE_DEV_TPAD);
	}

	return (0);
}
