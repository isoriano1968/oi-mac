/*
 * kmstest: first picture through amdgpu's own mode setting (KMS) on the Mac
 * Pro 2013's display GPU. Lists the connectors, takes the first connected
 * one and its preferred mode, draws a test pattern into a dumb buffer and
 * shows it with drmModeSetCrtc() for a few seconds; then restores the
 * CRTC's previous configuration (if it had one). No X, no EGL.
 *
 * Build: gcc -O1 -g -Wall -o kmstest kmstest.c -I/usr/include/libdrm \
 *   -L/usr/lib/xorg/amd64 -R/usr/lib/xorg/amd64 -ldrm
 * Run:   ./kmstest [/dev/dri/card1] [seconds] [list]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

static const char *
ctype(uint32_t t)
{
	switch (t) {
	case DRM_MODE_CONNECTOR_DisplayPort:	return ("DP");
	case DRM_MODE_CONNECTOR_HDMIA:		return ("HDMI-A");
	case DRM_MODE_CONNECTOR_DVII:		return ("DVI-I");
	case DRM_MODE_CONNECTOR_DVID:		return ("DVI-D");
	case DRM_MODE_CONNECTOR_VGA:		return ("VGA");
	case DRM_MODE_CONNECTOR_eDP:		return ("eDP");
	default:				return ("other");
	}
}

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/dri/card1";
	int secs = argc > 2 ? atoi(argv[2]) : 10;
	int list_only = argc > 3 && strcmp(argv[3], "list") == 0;
	drmModeRes *res;
	drmModeConnector *conn = NULL, *c;
	drmModeEncoder *enc;
	drmModeCrtc *saved = NULL;
	drmModeModeInfo mode;
	struct drm_mode_create_dumb cd = { 0 };
	struct drm_mode_map_dumb md = { 0 };
	struct drm_mode_destroy_dumb dd = { 0 };
	uint32_t fb, crtc_id = 0, *px;
	uint64_t cap = 0;
	int fd, i, j, x, y, r;

	setvbuf(stderr, NULL, _IONBF, 0);
	if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) {
		perror(path);
		return (1);
	}
	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &cap) != 0 || cap == 0) {
		fprintf(stderr, "kmstest: no dumb buffers\n");
		return (1);
	}
	if ((res = drmModeGetResources(fd)) == NULL) {
		fprintf(stderr, "kmstest: drmModeGetResources: %s\n",
		    strerror(errno));
		return (1);
	}
	fprintf(stderr, "kmstest: %d connectors, %d encoders, %d crtcs, "
	    "fb %dx%d..%dx%d\n", res->count_connectors, res->count_encoders,
	    res->count_crtcs, res->min_width, res->min_height,
	    res->max_width, res->max_height);
	for (i = 0; i < res->count_connectors; i++) {
		c = drmModeGetConnector(fd, res->connectors[i]);
		if (c == NULL)
			continue;
		fprintf(stderr, "kmstest: connector %u %s-%u: %s, %d modes",
		    c->connector_id, ctype(c->connector_type),
		    c->connector_type_id,
		    c->connection == DRM_MODE_CONNECTED ? "connected" :
		    c->connection == DRM_MODE_DISCONNECTED ? "disconnected" :
		    "unknown", c->count_modes);
		for (j = 0; j < c->count_modes && j < 3; j++)
			fprintf(stderr, "%s %s@%u", j == 0 ? ":" : ",",
			    c->modes[j].name, c->modes[j].vrefresh);
		fprintf(stderr, "\n");
		if (conn == NULL && c->connection == DRM_MODE_CONNECTED &&
		    c->count_modes > 0)
			conn = c;
		else
			drmModeFreeConnector(c);
	}
	if (list_only || conn == NULL) {
		if (conn == NULL)
			fprintf(stderr, "kmstest: no connected connector\n");
		return (conn == NULL);
	}
	mode = conn->modes[0];
	for (i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			mode = conn->modes[i];
			break;
		}
	/* a CRTC the connector's encoders can drive */
	for (i = 0; i < conn->count_encoders && crtc_id == 0; i++) {
		if ((enc = drmModeGetEncoder(fd, conn->encoders[i])) == NULL)
			continue;
		for (j = 0; j < res->count_crtcs; j++)
			if (enc->possible_crtcs & (1u << j)) {
				crtc_id = res->crtcs[j];
				break;
			}
		drmModeFreeEncoder(enc);
	}
	if (crtc_id == 0) {
		fprintf(stderr, "kmstest: no CRTC for the connector\n");
		return (1);
	}
	fprintf(stderr, "kmstest: using connector %u, crtc %u, mode %s@%u "
	    "(%ux%u)\n", conn->connector_id, crtc_id, mode.name, mode.vrefresh,
	    mode.hdisplay, mode.vdisplay);

	cd.width = mode.hdisplay;
	cd.height = mode.vdisplay;
	cd.bpp = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) {
		perror("CREATE_DUMB");
		return (1);
	}
	if (drmModeAddFB(fd, cd.width, cd.height, 24, 32, cd.pitch,
	    cd.handle, &fb) != 0) {
		perror("drmModeAddFB");
		return (1);
	}
	md.handle = cd.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) {
		perror("MAP_DUMB");
		return (1);
	}
	px = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    md.offset);
	if (px == MAP_FAILED) {
		perror("mmap");
		return (1);
	}
	/* colour bars on top, a gradient below, a white frame */
	for (y = 0; y < (int)cd.height; y++) {
		uint32_t *row = (uint32_t *)((char *)px + y * cd.pitch);

		for (x = 0; x < (int)cd.width; x++) {
			static const uint32_t bars[8] = {
				0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
				0xff00ff, 0xff0000, 0x0000ff, 0x000000
			};
			uint32_t v;

			if (y < (int)cd.height / 2)
				v = bars[x * 8 / cd.width];
			else
				v = ((x * 255 / cd.width) << 16) |
				    ((y * 255 / cd.height) << 8) | 0x40;
			if (x < 4 || y < 4 || x >= (int)cd.width - 4 ||
			    y >= (int)cd.height - 4)
				v = 0xffffff;
			row[x] = v;
		}
	}
	/* read back: does the mapping hold what was written, everywhere? */
	{
		size_t n = (size_t)cd.pitch * cd.height / 4, k, bad = 0,
		    first = (size_t)-1;
		volatile uint32_t *v = px;

		for (y = 0; y < (int)cd.height; y++)
			for (x = 0; x < (int)cd.width; x++) {
				k = (size_t)y * cd.pitch / 4 + x;
				if (v[k] == 0xdeadbeef)
					continue;
			}
		/* write a known word into every 4 KiB page, read it back */
		for (k = 0; k < n; k += 1024)
			v[k] = 0x5a5a0000u | (uint32_t)(k >> 10);
		for (k = 0; k < n; k += 1024)
			if (v[k] != (0x5a5a0000u | (uint32_t)(k >> 10))) {
				if (first == (size_t)-1)
					first = k * 4;
				bad++;
			}
		fprintf(stderr, "kmstest: buffer %llu bytes (pitch %u, handle "
		    "%u, map offset 0x%llx): %zu of %zu pages read back "
		    "wrong, first at byte %zu (%.1f MiB)\n",
		    (unsigned long long)cd.size, cd.pitch, cd.handle,
		    (unsigned long long)md.offset, bad, n / 1024,
		    first == (size_t)-1 ? 0 : first,
		    first == (size_t)-1 ? 0.0 : first / 1048576.0);
		/* redraw the pattern words the probe overwrote */
		for (k = 0; k < n; k += 1024) {
			y = (int)(k * 4 / cd.pitch);
			x = (int)((k * 4 % cd.pitch) / 4);
			v[k] = y < (int)cd.height / 2 ? 0xffffff :
			    ((x * 255 / cd.width) << 16) |
			    ((y * 255 / cd.height) << 8) | 0x40;
		}
	}
	saved = drmModeGetCrtc(fd, crtc_id);
	r = drmModeSetCrtc(fd, crtc_id, fb, 0, 0, &conn->connector_id, 1,
	    &mode);
	fprintf(stderr, "kmstest: drmModeSetCrtc = %d%s%s\n", r,
	    r != 0 ? ": " : "", r != 0 ? strerror(errno) : "");
	if (r == 0) {
		fprintf(stderr, "kmstest: phase 1, the pattern, %d s\n", secs);
		sleep(secs);
		/*
		 * Phase 2: redraw the whole buffer through the same mapping
		 * after the mode set (which pins the buffer for scanout and
		 * may move it): a 64-pixel checkerboard. If the screen keeps
		 * phase 1's picture, the mapping no longer points where the
		 * display reads.
		 */
		fprintf(stderr, "kmstest: phase 2, checkerboard, %d s\n", secs);
		for (y = 0; y < (int)cd.height; y++) {
			uint32_t *row = (uint32_t *)((char *)px + y * cd.pitch);

			for (x = 0; x < (int)cd.width; x++)
				row[x] = ((x / 64 + y / 64) & 1) ? 0xffffff :
				    0x2040c0;
		}
		sleep(secs);
	}
	if (saved != NULL && saved->mode_valid)
		(void) drmModeSetCrtc(fd, saved->crtc_id, saved->buffer_id,
		    saved->x, saved->y, &conn->connector_id, 1, &saved->mode);
	munmap(px, cd.size);
	drmModeRmFB(fd, fb);
	dd.handle = cd.handle;
	(void) drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
	close(fd);
	fprintf(stderr, "kmstest: done\n");
	return (r != 0);
}
