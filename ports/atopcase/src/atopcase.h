/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021-2023 Val Packett <val@packett.cool>
 * Copyright (c) 2023 Vladimir Kondratyev <wulf@FreeBSD.org>
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
 * atopcase: the Apple MacBook "topcase" (internal keyboard and trackpad)
 * on the Intel LPSS SPI controller of the 2015-2017 MacBooks and
 * MacBook Pros (ACPI device APP000D).
 *
 * Port of FreeBSD's atopcase(4) (sys/dev/atopcase, Val Packett and
 * Vladimir Kondratyev) and the register model of intelspi(4)
 * (sys/dev/intel/spi.c, Oleksandr Tymoshenko).  FreeBSD has a SPI bus
 * and a HID bus; illumos has neither, so this one module drives the SPI
 * controller itself, speaks the topcase protocol and offers a STREAMS
 * keyboard (DDI_NT_KEYBOARD, kbtrans with the USB keymap), which
 * consconfig links into the console keyboard like kb8042.
 */

#ifndef _ATOPCASE_H
#define	_ATOPCASE_H

#include <sys/types.h>
#include <sys/ksynch.h>
#include <sys/sunddi.h>
#include <sys/stream.h>
#include <sys/consdev.h>
#include <sys/kbd.h>		/* struct keyboard, before kbtrans.h */
#include <sys/kbio.h>
#include <sys/kbtrans.h>
#include <sys/acpi/acpi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- topcase protocol (FreeBSD atopcase_reg.h) --- */

#define	ATOPCASE_PACKET_SIZE	256
#define	ATOPCASE_DATA_SIZE	246
#define	ATOPCASE_PKT_PER_MSG	2
#define	ATOPCASE_MSG_SIZE	(ATOPCASE_DATA_SIZE * ATOPCASE_PKT_PER_MSG)

/* Read == device-initiated, Write == host-initiated or reply to that */
#define	ATOPCASE_DIR_READ		0x20
#define	ATOPCASE_DIR_WRITE		0x40
#define	ATOPCASE_DIR_NOTHING		0x80

#define	ATOPCASE_DEV_MGMT		0x00
#define	ATOPCASE_DEV_KBRD		0x01
#define	ATOPCASE_DEV_TPAD		0x02
#define	ATOPCASE_DEV_INFO		0xD0

#define	ATOPCASE_BKL_REPORT_ID		0xB0

#define	ATOPCASE_INFO_DEVICE		0x01
#define	ATOPCASE_INFO_IFACE		0x02
#define	ATOPCASE_INFO_DESCRIPTOR	0x10

#define	ATOPCASE_MSG_TYPE_SET_REPORT(dev, rid)	(((rid) << 8) | 0x50 | (dev))
#define	ATOPCASE_MSG_TYPE_REPORT(dev)		(((dev) << 8) | 0x10)
#define	ATOPCASE_MSG_TYPE_INFO(inf)		(((inf) << 8) | 0x20)

#pragma pack(1)
typedef struct atopcase_header {
	uint16_t	ah_type;
	uint8_t		ah_type_arg;	/* "device" for INFO_DESCRIPTOR */
	uint8_t		ah_seq_no;
	uint16_t	ah_resp_len;
	uint16_t	ah_len;
} atopcase_header_t;

typedef struct atopcase_packet {
	uint8_t		ap_direction;
	uint8_t		ap_device;
	uint16_t	ap_offset;
	uint16_t	ap_remaining;
	uint16_t	ap_length;
	uint8_t		ap_data[ATOPCASE_DATA_SIZE];
	uint16_t	ap_checksum;
} atopcase_packet_t;

typedef struct atopcase_device_info {
	uint16_t	adi_unknown[2];
	uint16_t	adi_num_devs;
	uint16_t	adi_vid;
	uint16_t	adi_pid;
	uint16_t	adi_ver;
	uint16_t	adi_vendor_off;
	uint16_t	adi_vendor_len;
	uint16_t	adi_product_off;
	uint16_t	adi_product_len;
	uint16_t	adi_serial_off;
	uint16_t	adi_serial_len;
} atopcase_device_info_t;

typedef struct atopcase_iface_info {
	uint8_t		aii_unknown0;
	uint8_t		aii_iface_num;
	uint8_t		aii_unknown1[3];
	uint8_t		aii_country_code;
	uint16_t	aii_max_input_report_len;
	uint16_t	aii_max_output_report_len;
	uint16_t	aii_max_control_report_len;
	uint16_t	aii_name_off;
	uint16_t	aii_name_len;
} atopcase_iface_info_t;

typedef struct atopcase_bl_payload {
	uint8_t		abl_report_id;
	uint8_t		abl_device;
	uint16_t	abl_level;
	uint16_t	abl_status;
} atopcase_bl_payload_t;
#pragma pack()

/*
 * Keyboard input report as the topcase sends it (the payload of a
 * REPORT(KBRD) message; same layout as Linux applespi's
 * keyboard_protocol and the HID descriptor FreeBSD hands to hkbd):
 * report id, modifier bits (USB boot layout), reserved, six key usages,
 * Fn state.
 */
#define	ATOPCASE_KBD_RID		0
#define	ATOPCASE_KBD_MODS		1
#define	ATOPCASE_KBD_KEYS		3
#define	ATOPCASE_KBD_NKEYS		6
#define	ATOPCASE_KBD_FN			9
#define	ATOPCASE_KBD_REPORT_LEN		10

/* Caps lock LED: SET_REPORT(KBRD, id 1) with { 0x01, HID LED bits } */
#define	ATOPCASE_KBD_LED_RID		0x01
#define	ATOPCASE_HID_LED_NUM		0x01
#define	ATOPCASE_HID_LED_CAPS		0x02

/* --- Intel LPSS SPI (FreeBSD intelspi, Sunrise Point family) --- */

#define	LPSS_PRIV_SPT			0x200
#define	LPSS_PRIV_CLK			0x00
#define	 LPSS_PRIV_CLK_EN		(1U << 0)
#define	LPSS_PRIV_RESETS		0x04
#define	 LPSS_PRIV_RESETS_FUNC		0x3
#define	 LPSS_PRIV_RESETS_IDMA		(1U << 2)
#define	LPSS_PRIV_CS_CTRL_SPT		0x24
#define	 SPI_CS_CTRL_CS_MASK		(3)
#define	 SPI_CS_CTRL_SW_MODE		(1 << 0)
#define	 SPI_CS_CTRL_CS_HIGH		(1 << 1)

#define	SSP_SSCR0			0x00
#define	 SSCR0_SCR(n)			((((n) - 1) & 0xfff) << 8)
#define	 SSCR0_SSE			(1 << 7)
#define	 SSCR0_FRF_SPI			(0 << 4)
#define	 SSCR0_DSS(n)			(((n) - 1) << 0)
#define	SSP_SSCR1			0x04
#define	 SSCR1_TINTE			(1 << 19)
#define	 SSCR1_RFT(n)			(((n) - 1) << 10)
#define	 SSCR1_TFT(n)			(((n) - 1) << 6)
#define	 SSCR1_SPI_SPH			(1 << 4)
#define	 SSCR1_SPI_SPO			(1 << 3)
#define	 SSCR1_TIE			(1 << 1)
#define	 SSCR1_RIE			(1 << 0)
#define	SSP_SSSR			0x08
#define	 SSSR_TFL_MASK			(0xf << 8)
#define	 SSSR_RNE			(1 << 3)
#define	 SSSR_TNF			(1 << 2)
#define	 SSSR_BSY			(1 << 4)
#define	SSP_SSDR			0x10

/* SSP input clock as FreeBSD intelspi assumes it */
#define	ATOPCASE_SSP_CLOCK		50000000U
#define	ATOPCASE_SPI_DEFAULT_HZ		8000000U	/* Apple: 125 ns */
#define	ATOPCASE_SPI_MODE_CPHA		0x1
#define	ATOPCASE_SPI_MODE_CPOL		0x2
/* Keep at most this many bytes in flight (the PXA2xx SSP FIFO is 16) */
#define	ATOPCASE_SPI_FIFO		16

/* --- driver state --- */

typedef struct atopcase_child {
	uint8_t		acc_device;
	char		acc_name[80];
	uint8_t		acc_rdesc[ATOPCASE_MSG_SIZE];
	uint16_t	acc_rdesc_len;
	uint64_t	acc_reports;
} atopcase_child_t;

typedef struct atopcase_kbd {
	kmutex_t		ak_lock;
	boolean_t		ak_open;
	queue_t			*ak_rq;
	struct kbtrans		*ak_kbtrans;
	struct cons_polledio	ak_polledio;
	int			ak_vkbd_type;	/* KB_USB or KB_PC */
	int			ak_layout;
	uint8_t			ak_last_mods;
	uint8_t			ak_last_raw[ATOPCASE_KBD_NKEYS];
	uint8_t			ak_last_emit[ATOPCASE_KBD_NKEYS];
	/* polled (kmdb) key buffer */
	int			ak_pbuf_key[32];
	enum keystate		ak_pbuf_state[32];
	uint_t			ak_pbuf_head;
	uint_t			ak_pbuf_tail;
	boolean_t		ak_in_poll;
} atopcase_kbd_t;

/* --- trackpad (atopcase_tpad.c) --- */

#define	ATOPCASE_TP_NBUTTONS		3

/* Minor numbers: the keyboard and the trackpad as a mouse */
#define	ATOPCASE_MINOR_KBD		0
#define	ATOPCASE_MINOR_MOUSE		1

/* One field of the trackpad's HID input report */
typedef struct atopcase_tpad_field {
	boolean_t	tf_valid;
	boolean_t	tf_rel;		/* relative (deltas) */
	boolean_t	tf_signed;	/* logical minimum < 0 */
	uint8_t		tf_rid;		/* report id, 0 if none */
	uint8_t		tf_size;	/* bits, at most 32 */
	uint16_t	tf_pos;		/* bit offset after the report id */
} atopcase_tpad_field_t;

typedef struct atopcase_tpad_layout {
	boolean_t		tl_uses_rid;
	uint8_t			tl_rid;		/* the X/Y report */
	uint16_t		tl_bits;	/* its length in bits */
	atopcase_tpad_field_t	tl_x, tl_y, tl_wheel;
	atopcase_tpad_field_t	tl_btn[ATOPCASE_TP_NBUTTONS];
} atopcase_tpad_layout_t;

typedef struct atopcase_tpad {
	kmutex_t		at_lock;
	boolean_t		at_parsed;	/* the mouse node exists */
	boolean_t		at_open;
	queue_t			*at_rq;
	atopcase_tpad_layout_t	at_layout;
	uint_t			at_nbuttons;
	uint_t			at_buttons;	/* last reported state */
	int			at_format;	/* VUID_NATIVE/FIRM_EVENT */
	ushort_t		at_vuidaddr;
	boolean_t		at_wheel_enabled;
} atopcase_tpad_t;

typedef struct atopcase {
	dev_info_t		*as_dip;
	int			as_instance;

	/* LPSS SPI controller */
	ddi_acc_handle_t	as_regh;
	caddr_t			as_regs;
	kmutex_t		as_spi_lock;
	uint32_t		as_spi_hz;
	uint32_t		as_spi_mode;
	uint32_t		as_cs_delay_us;
	boolean_t		as_polled;	/* kmdb: no locks, no sleeps */

	/* ACPI */
	ACPI_HANDLE		as_handle;	/* APP000D */
	ACPI_HANDLE		as_ctrl_handle;	/* its SPI controller */
	char			as_hid[16];
	uint32_t		as_gpe;
	boolean_t		as_gpe_installed;
	boolean_t		as_use_gpe;
	volatile uint32_t	as_gpe_count;
	ddi_softint_handle_t	as_softint;
	boolean_t		as_softint_added;

	/* worker thread: all SPI I/O after attach goes through it */
	kmutex_t		as_lock;
	kcondvar_t		as_cv;
	kthread_t		*as_thread;
	boolean_t		as_stop;
	boolean_t		as_thread_done;
	boolean_t		as_gpe_pending;
	boolean_t		as_led_pending;
	int			as_led_state;	/* kbtrans LED_* bits */

	/* protocol */
	boolean_t		as_booted;
	uint8_t			as_seq_no;
	uint8_t			as_msg[ATOPCASE_MSG_SIZE];
	uint16_t		as_msg_len;
	atopcase_packet_t	as_buf;
	atopcase_packet_t	as_junk;
	uint16_t		as_vid;
	uint16_t		as_pid;
	uint16_t		as_ver;
	char			as_vendor[80];
	char			as_product[80];
	char			as_serial[80];
	atopcase_child_t	as_kb;
	atopcase_child_t	as_tp;
	uint64_t		as_bad_packets;

	atopcase_kbd_t		as_kbd;
	atopcase_tpad_t		as_tpad;
	boolean_t		as_minor_created;
} atopcase_t;

extern int atopcase_debug;

/* atopcase_spi.c */
extern int atopcase_spi_init(atopcase_t *);
extern void atopcase_spi_fini(atopcase_t *);
extern int atopcase_spi_xfer(atopcase_t *, const void *, void *, size_t,
    boolean_t);

/* atopcase_proto.c */
extern uint16_t atopcase_crc16(uint16_t, const void *, size_t);
extern int atopcase_receive_packet(atopcase_t *);
extern int atopcase_request_desc(atopcase_t *, uint16_t, uint8_t);
extern int atopcase_set_report(atopcase_t *, uint8_t, uint8_t,
    const void *, uint8_t);
extern int atopcase_set_backlight(atopcase_t *, uint_t);
extern int atopcase_proto_init(atopcase_t *);

/* atopcase_kbd.c */
extern struct streamtab atopcase_str_info;
extern void atopcase_kbd_init(atopcase_t *);
extern void atopcase_kbd_fini(atopcase_t *);
extern void atopcase_kbd_report(atopcase_t *, const uint8_t *, uint16_t);
extern int atopcase_kbd_leds_to_hid(int);

/* atopcase_tpad.c */
extern void atopcase_tpad_init(atopcase_t *);
extern boolean_t atopcase_tpad_setup(atopcase_t *);
extern void atopcase_tpad_fini(atopcase_t *);
extern void atopcase_tpad_report(atopcase_t *, const uint8_t *, uint16_t);
extern int atopcase_tpad_open(atopcase_t *, queue_t *, int, cred_t *);
extern int atopcase_tpad_close(queue_t *);
extern int atopcase_tpad_wsrv(queue_t *);

/* atopcase.c */
extern atopcase_t *atopcase_state;
extern void atopcase_request_leds(atopcase_t *, int);

/* Debug output goes to the system log only ("!") */
#define	ATOPCASE_DBG(lvl, ...)	do {					\
	if (atopcase_debug >= (lvl))					\
		cmn_err(CE_CONT, "!atopcase: " __VA_ARGS__);		\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* _ATOPCASE_H */
