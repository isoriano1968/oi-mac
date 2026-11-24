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
 * abicheck: sizes and offsets of the V4L2 structs and the values of the
 * constants the usbvc change adds.  Build it on Linux (x86_64, against
 * <linux/videodev2.h>) and on illumos (-I to the gate's sys/videodev2.h)
 * and diff the two outputs: they are identical since struct v4l2_buffer
 * has the Linux layout (illumos-gate branch usbvc-v4l2, 7b95a40f).
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __linux__
#include <linux/videodev2.h>
#else
#include <sys/videodev2.h>
#endif
#define S(t) printf("sizeof %-28s %3zu\n", #t, sizeof (struct t))
#define O(t, f) printf("offsetof %s.%-20s %3zu\n", #t, #f, offsetof(struct t, f))
#define C(c) printf("%-40s 0x%08lx\n", #c, (unsigned long)(c))
int main(void) {
	S(v4l2_capability); O(v4l2_capability, device_caps);
	S(v4l2_pix_format); O(v4l2_pix_format, priv); O(v4l2_pix_format, flags);
	O(v4l2_pix_format, ycbcr_enc); O(v4l2_pix_format, quantization); O(v4l2_pix_format, xfer_func);
	S(v4l2_format); O(v4l2_format, fmt);
	S(v4l2_fmtdesc); O(v4l2_fmtdesc, pixelformat);
	S(v4l2_frmsizeenum); O(v4l2_frmsizeenum, discrete); O(v4l2_frmsizeenum, reserved);
	S(v4l2_frmivalenum); O(v4l2_frmivalenum, discrete); O(v4l2_frmivalenum, reserved);
	S(v4l2_requestbuffers); S(v4l2_buffer); O(v4l2_buffer, timestamp); O(v4l2_buffer, flags);
	O(v4l2_buffer, m); O(v4l2_buffer, length);
	S(v4l2_streamparm); S(v4l2_captureparm); S(v4l2_queryctrl); S(v4l2_control);
	S(v4l2_event); O(v4l2_event, u); O(v4l2_event, pending); O(v4l2_event, timestamp); O(v4l2_event, id);
	S(v4l2_event_ctrl); O(v4l2_event_ctrl, value); O(v4l2_event_ctrl, flags);
	S(v4l2_event_subscription);
	S(v4l2_ext_control); O(v4l2_ext_control, size); O(v4l2_ext_control, value64);
	S(v4l2_ext_controls); O(v4l2_ext_controls, request_fd); O(v4l2_ext_controls, controls);
	C(V4L2_CTRL_WHICH_DEF_VAL);
	C(V4L2_CAP_DEVICE_CAPS); C(V4L2_CAP_EXT_PIX_FORMAT); C(V4L2_PIX_FMT_MJPEG); C(V4L2_PIX_FMT_H264);
	C(V4L2_PIX_FMT_Y16); C(V4L2_PIX_FMT_Z16); C(V4L2_PIX_FMT_M420);
	C(V4L2_BUF_FLAG_ERROR); C(V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC); C(V4L2_BUF_FLAG_LAST);
	C(V4L2_CID_POWER_LINE_FREQUENCY); C(V4L2_CID_SHARPNESS); C(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
	C(V4L2_CID_COLORFX_RGB); C(V4L2_CID_LASTP1); C(V4L2_CID_CAMERA_CLASS); C(V4L2_CID_CAMERA_CLASS_BASE);
	C(V4L2_CID_EXPOSURE_AUTO); C(V4L2_CID_FOCUS_AUTO); C(V4L2_CID_ZOOM_CONTINUOUS);
	C(V4L2_CID_AUTO_EXPOSURE_BIAS); C(V4L2_CID_PAN_SPEED); C(V4L2_CID_TILT_SPEED);
	C(V4L2_CID_CAMERA_SENSOR_ROTATION); C(V4L2_CTRL_FLAG_NEXT_COMPOUND); C(V4L2_CTRL_FLAG_WRITE_ONLY);
	C(V4L2_COLORSPACE_DCI_P3); C(V4L2_XFER_FUNC_SMPTE2084); C(V4L2_YCBCR_ENC_SMPTE240M);
	C(V4L2_QUANTIZATION_LIM_RANGE); C(V4L2_EVENT_CTRL); C(V4L2_FRMIVAL_TYPE_STEPWISE);
	C(V4L2_EXPOSURE_APERTURE_PRIORITY); C(V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
	C(V4L2_FMT_FLAG_EMULATED); C(V4L2_CTRL_CLASS_CAMERA);
	/* ioctl numbers differ by design (illumos _IOWR encoding) */
	C(VIDIOC_ENUM_FRAMESIZES & 0xff); C(VIDIOC_ENUM_FRAMEINTERVALS & 0xff);
	C(VIDIOC_DQEVENT & 0xff); C(VIDIOC_SUBSCRIBE_EVENT & 0xff); C(VIDIOC_UNSUBSCRIBE_EVENT & 0xff);
	return 0;
}
