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
 * v4l2test: exercise a V4L2 capture device (usbvc(4D)) the way Chromium's
 * media/capture/video/linux code does.
 *
 *   v4l2test [-d /dev/video0]
 *	list: QUERYCAP, ENUM_FMT, ENUM_FRAMESIZES, ENUM_FRAMEINTERVALS,
 *	G_FMT, G_PARM, QUERYCTRL
 *   v4l2test [-d dev] -n frames [-f MJPG|YUYV|NV12] [-s WxH] [-r fps]
 *	    [-o file] [-b buffers] [-2]
 *	stream: S_FMT, S_PARM, REQBUFS, QUERYBUF + mmap, QBUF, STREAMON,
 *	then poll(2) + DQBUF + QBUF for each frame, STREAMOFF.
 *	-2 opens the device a second time every 100 frames (QUERYCAP), as
 *	Chromium enumerates devices during a call.  The frames
 *	go to the file one after the other (MJPEG: "ffplay -f mjpeg file";
 *	YUYV: "ffplay -f rawvideo -pixel_format yuyv422 -video_size WxH").
 *
 * Exit status 0 when every step worked and n frames came in.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/videodev2.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define	MAXBUF		32
#define	POLL_MS		3000	/* Chromium's kCaptureTimeoutMs */

static int fd = -1;
static const char *reopen_dev;	/* -2: open again while streaming */

static int
xioctl(int req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && errno == EINTR);

	return (r);
}

static const char *
fcc(uint32_t f, char *buf)
{
	int i;

	for (i = 0; i < 4; i++) {
		char c = (f >> (8 * i)) & 0xff;

		buf[i] = (c >= ' ' && c < 127) ? c : '.';
	}
	buf[4] = '\0';

	return (buf);
}

static uint32_t
str2fcc(const char *s)
{
	char b[4] = { ' ', ' ', ' ', ' ' };
	size_t n = strlen(s);

	(void) memcpy(b, s, n > 4 ? 4 : n);

	return (v4l2_fourcc(b[0], b[1], b[2], b[3]));
}

static double
fract_fps(const struct v4l2_fract *f)
{
	return (f->numerator ? (double)f->denominator / f->numerator : 0.0);
}

static double
now(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);

	return (ts.tv_sec + ts.tv_nsec / 1e9);
}

static void
list_intervals(uint32_t pixfmt, uint32_t w, uint32_t h)
{
	struct v4l2_frmivalenum fi;
	uint32_t i;

	(void) printf("        rates:");
	for (i = 0; ; i++) {
		bzero(&fi, sizeof (fi));
		fi.index = i;
		fi.pixel_format = pixfmt;
		fi.width = w;
		fi.height = h;
		if (xioctl(VIDIOC_ENUM_FRAMEINTERVALS, &fi) != 0) {
			if (i == 0)
				(void) printf(" ENUM_FRAMEINTERVALS: %s",
				    strerror(errno));
			break;
		}
		if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
			(void) printf(" %.2f (%u/%u)", fract_fps(&fi.discrete),
			    fi.discrete.numerator, fi.discrete.denominator);
		} else {
			(void) printf(" %s %.2f..%.2f fps, step %u/%u s",
			    fi.type == V4L2_FRMIVAL_TYPE_CONTINUOUS ?
			    "continuous" : "stepwise",
			    fract_fps(&fi.stepwise.max),
			    fract_fps(&fi.stepwise.min),
			    fi.stepwise.step.numerator,
			    fi.stepwise.step.denominator);
			break;
		}
	}
	(void) printf("\n");
}

static int
list_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fd_;
	struct v4l2_frmsizeenum fs;
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_queryctrl qc;
	char b[5];
	uint32_t id, i, j;

	bzero(&cap, sizeof (cap));
	if (xioctl(VIDIOC_QUERYCAP, &cap) != 0) {
		perror("VIDIOC_QUERYCAP");
		return (1);
	}
	(void) printf("driver %.16s, card \"%.32s\", bus %.32s, version %u\n",
	    cap.driver, cap.card, cap.bus_info, cap.version);
	(void) printf("capabilities 0x%08x, device_caps 0x%08x%s\n",
	    cap.capabilities, cap.device_caps,
	    (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? "" :
	    " (V4L2_CAP_DEVICE_CAPS not set)");

	for (i = 0; ; i++) {
		bzero(&fd_, sizeof (fd_));
		fd_.index = i;
		fd_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (xioctl(VIDIOC_ENUM_FMT, &fd_) != 0)
			break;
		(void) printf("format %u: %s \"%.32s\"%s\n", fd_.index,
		    fcc(fd_.pixelformat, b), fd_.description,
		    (fd_.flags & V4L2_FMT_FLAG_COMPRESSED) ?
		    " (compressed)" : "");
		for (j = 0; ; j++) {
			bzero(&fs, sizeof (fs));
			fs.index = j;
			fs.pixel_format = fd_.pixelformat;
			if (xioctl(VIDIOC_ENUM_FRAMESIZES, &fs) != 0) {
				if (j == 0)
					(void) printf("    ENUM_FRAMESIZES: "
					    "%s\n", strerror(errno));
				break;
			}
			if (fs.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
				(void) printf("    sizes %ux%u..%ux%u\n",
				    fs.stepwise.min_width,
				    fs.stepwise.min_height,
				    fs.stepwise.max_width,
				    fs.stepwise.max_height);
				break;
			}
			(void) printf("    %ux%u\n", fs.discrete.width,
			    fs.discrete.height);
			list_intervals(fd_.pixelformat, fs.discrete.width,
			    fs.discrete.height);
		}
	}
	if (i == 0)
		(void) printf("ENUM_FMT: no formats (%s)\n", strerror(errno));

	bzero(&fmt, sizeof (fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(VIDIOC_G_FMT, &fmt) == 0) {
		(void) printf("current: %s %ux%u, bytesperline %u, "
		    "sizeimage %u, colorspace %u\n",
		    fcc(fmt.fmt.pix.pixelformat, b), fmt.fmt.pix.width,
		    fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
		    fmt.fmt.pix.sizeimage, fmt.fmt.pix.colorspace);
	} else {
		perror("VIDIOC_G_FMT");
	}
	bzero(&parm, sizeof (parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(VIDIOC_G_PARM, &parm) == 0) {
		(void) printf("G_PARM: capability 0x%x, timeperframe %u/%u "
		    "(%.2f fps)\n", parm.parm.capture.capability,
		    parm.parm.capture.timeperframe.numerator,
		    parm.parm.capture.timeperframe.denominator,
		    fract_fps(&parm.parm.capture.timeperframe));
	} else {
		perror("VIDIOC_G_PARM");
	}

	/* user class controls, as Chromium walks them */
	for (id = V4L2_CID_BASE; id < V4L2_CID_LASTP1; id++) {
		bzero(&qc, sizeof (qc));
		qc.id = id;
		if (xioctl(VIDIOC_QUERYCTRL, &qc) != 0)
			continue;
		(void) printf("control 0x%x \"%.32s\": %d..%d step %d, "
		    "default %d\n", qc.id, qc.name, qc.minimum, qc.maximum,
		    qc.step, qc.default_value);
	}
	bzero(&qc, sizeof (qc));
	qc.id = V4L2_CID_USER_CLASS | V4L2_CTRL_FLAG_NEXT_CTRL;
	(void) printf("QUERYCTRL with V4L2_CTRL_FLAG_NEXT_CTRL: %s\n",
	    xioctl(VIDIOC_QUERYCTRL, &qc) == 0 ? "supported" :
	    strerror(errno));

	return (0);
}

/*
 * A frame whose last bytes are not FFD9: where is the last EOI marker,
 * and what comes after it (padding, a second frame, cut data)?
 */
static void
eoi_report(const uint8_t *p, uint32_t n, uint32_t flags)
{
	uint32_t i, last = 0, nsoi = 0, neoi = 0, k;

	for (i = 0; i + 1 < n; i++) {
		if (p[i] == 0xff && p[i + 1] == 0xd9) {
			last = i + 2;
			neoi++;
		}
		if (p[i] == 0xff && p[i + 1] == 0xd8)
			nsoi++;
	}
	(void) printf("%s: bytesused %u, flags 0x%x, SOI x%u, EOI x%u, "
	    "last EOI ends at %u (%u bytes after it)\n  tail:",
	    neoi ? "data after EOI" : "no EOI", n, flags, nsoi, neoi, last,
	    last ? n - last : 0);
	for (k = n > 24 ? n - 24 : 0; k < n; k++)
		(void) printf(" %02x", p[k]);
	if (last != 0 && last < n) {
		(void) printf("\n  after EOI:");
		for (k = last; k < n && k < last + 24; k++)
			(void) printf(" %02x", p[k]);
	}
	(void) printf("\n");
}

static int
stream(int nframes, uint32_t pixfmt, uint32_t w, uint32_t h, int fps,
    const char *out, uint32_t nbuf)
{
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	struct pollfd pfd;
	void *map[MAXBUF];
	size_t maplen[MAXBUF];
	char b[5];
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int got = 0, timeouts = 0, errs = 0, badjpeg = 0, rc = 1;
	int badeoi = 0, tailjunk = 0, reopen_ok = 0, reopen_fail = 0, reopen_errno = 0;
	uint32_t i, minsz = UINT32_MAX, maxsz = 0, lastseq = 0, gaps = 0;
	uint64_t total = 0;
	double t0, t1;
	FILE *of = NULL;

	bzero(map, sizeof (map));
	bzero(&fmt, sizeof (fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(VIDIOC_G_FMT, &fmt) != 0) {
		perror("VIDIOC_G_FMT");
		return (1);
	}
	if (pixfmt != 0)
		fmt.fmt.pix.pixelformat = pixfmt;
	if (w != 0) {
		fmt.fmt.pix.width = w;
		fmt.fmt.pix.height = h;
	}
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	{
		struct v4l2_format tf = fmt;

		/* as GStreamer: TRY_FMT first (EINVAL/ENOTTY are reported) */
		if (xioctl(VIDIOC_TRY_FMT, &tf) != 0) {
			perror("VIDIOC_TRY_FMT");
		} else {
			(void) printf("TRY_FMT: %s %ux%u, bytesperline %u, "
			    "sizeimage %u\n", fcc(tf.fmt.pix.pixelformat, b),
			    tf.fmt.pix.width, tf.fmt.pix.height,
			    tf.fmt.pix.bytesperline, tf.fmt.pix.sizeimage);
		}
	}
	if (xioctl(VIDIOC_S_FMT, &fmt) != 0) {
		perror("VIDIOC_S_FMT");
		return (1);
	}
	(void) printf("S_FMT: %s %ux%u, bytesperline %u, sizeimage %u\n",
	    fcc(fmt.fmt.pix.pixelformat, b), fmt.fmt.pix.width,
	    fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
	    fmt.fmt.pix.sizeimage);
	pixfmt = fmt.fmt.pix.pixelformat;

	if (fps > 0) {
		bzero(&parm, sizeof (parm));
		parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		parm.parm.capture.timeperframe.numerator = 1;
		parm.parm.capture.timeperframe.denominator = fps;
		if (xioctl(VIDIOC_S_PARM, &parm) != 0)
			perror("VIDIOC_S_PARM");
		else
			(void) printf("S_PARM: %u/%u (%.2f fps)\n",
			    parm.parm.capture.timeperframe.numerator,
			    parm.parm.capture.timeperframe.denominator,
			    fract_fps(&parm.parm.capture.timeperframe));
	}

	bzero(&req, sizeof (req));
	req.count = nbuf;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (xioctl(VIDIOC_REQBUFS, &req) != 0) {
		perror("VIDIOC_REQBUFS");
		return (1);
	}
	if (req.count == 0 || req.count > MAXBUF) {
		(void) fprintf(stderr, "REQBUFS gave %u buffers\n", req.count);
		return (1);
	}
	for (i = 0; i < req.count; i++) {
		bzero(&buf, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (xioctl(VIDIOC_QUERYBUF, &buf) != 0) {
			perror("VIDIOC_QUERYBUF");
			goto done;
		}
		maplen[i] = buf.length;
		map[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, buf.m.offset);
		if (map[i] == MAP_FAILED) {
			map[i] = NULL;
			perror("mmap");
			goto done;
		}
		if (xioctl(VIDIOC_QBUF, &buf) != 0) {
			perror("VIDIOC_QBUF");
			goto done;
		}
	}
	(void) printf("%u buffers of %zu bytes mapped\n", req.count,
	    maplen[0]);

	if (out != NULL && (of = fopen(out, "w")) == NULL) {
		perror(out);
		goto done;
	}
	if (xioctl(VIDIOC_STREAMON, &type) != 0) {
		perror("VIDIOC_STREAMON");
		goto done;
	}

	t0 = t1 = now();
	while (got < nframes && timeouts < 3) {
		int r;

		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		r = poll(&pfd, 1, POLL_MS);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}
		if (r == 0) {
			(void) fprintf(stderr, "poll: no frame in %d ms\n",
			    POLL_MS);
			timeouts++;
			continue;
		}
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			(void) fprintf(stderr, "poll: revents 0x%x\n",
			    pfd.revents);
			break;
		}
		timeouts = 0;
		bzero(&buf, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (xioctl(VIDIOC_DQBUF, &buf) != 0) {
			perror("VIDIOC_DQBUF");
			break;
		}
		if (got == 0)
			t1 = now();
		if (buf.flags & V4L2_BUF_FLAG_ERROR)
			errs++;
		if (got > 0 && buf.sequence != lastseq + 1)
			gaps++;
		lastseq = buf.sequence;
		if (buf.bytesused < minsz)
			minsz = buf.bytesused;
		if (buf.bytesused > maxsz)
			maxsz = buf.bytesused;
		total += buf.bytesused;
		if (pixfmt == V4L2_PIX_FMT_MJPEG && buf.bytesused >= 2) {
			uint8_t *p = map[buf.index];

			uint32_t e = buf.bytesused;

			if (p[0] != 0xff || p[1] != 0xd8)
				badjpeg++;
			/* EOI marker; some cameras pad the frame with zeros */
			while (e > 2 && p[e - 1] == 0)
				e--;
			if (e < 4 || p[e - 2] != 0xff || p[e - 1] != 0xd9) {
				uint32_t k;

				/* decoders stop at the EOI: data after it is padding */
				for (k = e - 1; k > 0; k--)
					if (p[k - 1] == 0xff && p[k] == 0xd9)
						break;
				if (k == 0) {
					badeoi++;
					if (badeoi <= 3)
						eoi_report(p, buf.bytesused,
						    buf.flags);
				} else if (++tailjunk <= 2) {
					eoi_report(p, buf.bytesused, buf.flags);
				}
			}
		}
		if (reopen_dev != NULL && got % 100 == 50) {
			/* as Chromium's device enumeration during a call */
			struct v4l2_capability qc;
			int fd2 = open(reopen_dev, O_RDONLY);

			if (fd2 == -1) {
				reopen_fail++;
				reopen_errno = errno;
			} else {
				bzero(&qc, sizeof (qc));
				if (ioctl(fd2, VIDIOC_QUERYCAP, &qc) == 0)
					reopen_ok++;
				else {
					reopen_fail++;
					reopen_errno = errno;
				}
				(void) close(fd2);
			}
		}
		if (of != NULL &&
		    fwrite(map[buf.index], 1, buf.bytesused, of) !=
		    buf.bytesused) {
			perror("fwrite");
			break;
		}
		got++;
		if (xioctl(VIDIOC_QBUF, &buf) != 0) {
			perror("VIDIOC_QBUF");
			break;
		}
	}
	t1 = now() - t1;
	if (xioctl(VIDIOC_STREAMOFF, &type) != 0)
		perror("VIDIOC_STREAMOFF");

	(void) printf("%d frames in %.2f s after the first one (%.2f fps), "
	    "%.2f s to the first frame\n", got, t1,
	    got > 1 ? (got - 1) / t1 : 0.0, now() - t0 - t1);
	if (got > 0)
		(void) printf("bytesused %u..%u, mean %llu; error flag %d, "
		    "sequence gaps %u\n", minsz, maxsz,
		    (unsigned long long)(total / got), errs, gaps);
	if (pixfmt == V4L2_PIX_FMT_MJPEG)
		(void) printf("frames without a JPEG SOI marker: %d, "
		    "without an EOI marker: %d, with data after the EOI: %d\n",
		    badjpeg, badeoi, tailjunk);
	if (reopen_dev != NULL)
		(void) printf("second open while streaming: %d ok, %d failed%s%s\n",
		    reopen_ok, reopen_fail, reopen_fail ? ": " : "",
		    reopen_fail ? strerror(reopen_errno) : "");
	if (got == nframes)
		rc = 0;
done:
	for (i = 0; i < MAXBUF; i++) {
		if (map[i] != NULL)
			(void) munmap(map[i], maplen[i]);
	}
	if (of != NULL)
		(void) fclose(of);

	return (rc);
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: v4l2test [-d device] [-n frames "
	    "[-f fourcc] [-s WxH] [-r fps] [-o file] [-b buffers] [-2]]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/video0", *out = NULL;
	int c, nframes = 0, fps = 0, rc;
	uint32_t pixfmt = 0, w = 0, h = 0, nbuf = 4;

	while ((c = getopt(argc, argv, "d:n:f:s:r:o:b:2")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		case 'n':
			nframes = atoi(optarg);
			break;
		case 'f':
			pixfmt = str2fcc(optarg);
			break;
		case 's':
			if (sscanf(optarg, "%ux%u", &w, &h) != 2)
				usage();
			break;
		case 'r':
			fps = atoi(optarg);
			break;
		case 'o':
			out = optarg;
			break;
		case 'b':
			nbuf = atoi(optarg);
			break;
		case '2':
			reopen_dev = "";
			break;
		default:
			usage();
		}
	}

	if (reopen_dev != NULL)
		reopen_dev = dev;
	if ((fd = open(dev, O_RDWR | O_NONBLOCK)) == -1) {
		perror(dev);
		return (1);
	}
	rc = nframes > 0 ? stream(nframes, pixfmt, w, h, fps, out, nbuf) :
	    list_device();
	(void) close(fd);

	return (rc);
}
