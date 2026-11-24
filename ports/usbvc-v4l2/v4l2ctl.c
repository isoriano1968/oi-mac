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
 * v4l2ctl: list, set and read back the controls of a V4L2 device
 * (usbvc(4D)), a small stand-in for Linux's v4l2-ctl.
 *
 *   v4l2ctl [-d /dev/video0]
 *	list every control (VIDIOC_QUERYCTRL with V4L2_CTRL_FLAG_NEXT_CTRL):
 *	ID, name as GStreamer's extra-controls spells it, type, range,
 *	default, current value (VIDIOC_G_CTRL, i.e. GET_CUR from the
 *	camera), and the items of menu controls (VIDIOC_QUERYMENU).
 *   v4l2ctl [-d dev] name=value ...
 *	VIDIOC_S_CTRL for each, in the order given, each followed by a
 *	VIDIOC_G_CTRL read-back; then the list as above.
 *
 * Exit status 0 when every S_CTRL and G_CTRL worked.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/videodev2.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd;

/* GStreamer's (and v4l2-ctl's) name: lower case, runs of others -> '_' */
static void
normalise(const char *in, char *out, size_t len)
{
	size_t i = 0;
	const char *p;

	for (p = in; *p != '\0' && i + 2 < len; p++) {
		if (isalnum((unsigned char)*p)) {
			if (i > 0 && p > in && !isalnum((unsigned char)p[-1]))
				out[i++] = '_';
			out[i++] = tolower((unsigned char)*p);
		}
	}
	out[i] = '\0';
}

static const char *
type_name(uint32_t type)
{
	switch (type) {
	case V4L2_CTRL_TYPE_INTEGER:
		return ("int");
	case V4L2_CTRL_TYPE_BOOLEAN:
		return ("bool");
	case V4L2_CTRL_TYPE_MENU:
		return ("menu");
	default:
		return ("other");
	}
}

/* Find a control by its normalised name; 0 if there is none */
static uint32_t
find_ctrl(const char *name, struct v4l2_queryctrl *out)
{
	struct v4l2_queryctrl qc;
	char n[64];

	(void) memset(&qc, 0, sizeof (qc));
	qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
		normalise((const char *)qc.name, n, sizeof (n));
		if (strcmp(n, name) == 0) {
			*out = qc;
			return (qc.id);
		}
		qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}

	return (0);
}

static int
list(void)
{
	struct v4l2_queryctrl qc;
	struct v4l2_querymenu qm;
	struct v4l2_control c;
	char n[64];
	int count = 0, rc = 0;

	(void) memset(&qc, 0, sizeof (qc));
	qc.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0) {
		count++;
		normalise((const char *)qc.name, n, sizeof (n));
		c.id = qc.id;
		c.value = 0;
		(void) printf("0x%08x %-28s %-4s min=%d max=%d step=%d "
		    "default=%d", qc.id, n, type_name(qc.type), qc.minimum,
		    qc.maximum, qc.step, qc.default_value);
		if (ioctl(fd, VIDIOC_G_CTRL, &c) == 0) {
			(void) printf(" value=%d\n", c.value);
		} else {
			(void) printf(" value=? (G_CTRL: %s)\n",
			    strerror(errno));
			rc = 1;
		}
		if (qc.type == V4L2_CTRL_TYPE_MENU) {
			int i;

			for (i = qc.minimum; i <= qc.maximum; i++) {
				(void) memset(&qm, 0, sizeof (qm));
				qm.id = qc.id;
				qm.index = i;
				if (ioctl(fd, VIDIOC_QUERYMENU, &qm) == 0)
					(void) printf("%12s%d: %s\n", "", i,
					    (const char *)qm.name);
			}
		}
		qc.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
	if (count == 0) {
		(void) fprintf(stderr, "no controls (QUERYCTRL with "
		    "NEXT_CTRL: %s)\n", strerror(errno));
		return (1);
	}

	return (rc);
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/video0";
	int i, rc = 0;

	if (argc > 2 && strcmp(argv[1], "-d") == 0) {
		dev = argv[2];
		argc -= 2;
		argv += 2;
	}
	if ((fd = open(dev, O_RDWR)) < 0) {
		perror(dev);
		return (2);
	}

	for (i = 1; i < argc; i++) {
		struct v4l2_queryctrl qc;
		struct v4l2_control c;
		char *eq = strchr(argv[i], '=');

		if (eq == NULL) {
			(void) fprintf(stderr, "usage: v4l2ctl [-d dev] "
			    "[name=value ...]\n");
			return (2);
		}
		*eq = '\0';
		if (find_ctrl(argv[i], &qc) == 0) {
			(void) fprintf(stderr, "%s: no such control\n",
			    argv[i]);
			rc = 1;
			continue;
		}
		c.id = qc.id;
		c.value = atoi(eq + 1);
		if (ioctl(fd, VIDIOC_S_CTRL, &c) != 0) {
			(void) printf("set %s=%d: %s\n", argv[i],
			    atoi(eq + 1), strerror(errno));
			rc = 1;
			continue;
		}
		c.value = 0;
		if (ioctl(fd, VIDIOC_G_CTRL, &c) != 0) {
			(void) printf("set %s=%d: ok, read back: %s\n",
			    argv[i], atoi(eq + 1), strerror(errno));
			rc = 1;
			continue;
		}
		(void) printf("set %s=%d: ok, read back %d\n", argv[i],
		    atoi(eq + 1), c.value);
	}
	if (list() != 0)
		rc = 1;
	(void) close(fd);

	return (rc);
}
