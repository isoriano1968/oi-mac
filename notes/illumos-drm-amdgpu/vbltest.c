/* vbltest: drmWaitVBlank on every CRTC pipe of a KMS device */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
int
main(int argc, char **argv)
{
	int fd = open(argc > 1 ? argv[1] : "/dev/dri/card0", O_RDWR), i;
	drmModeRes *r;

	if (fd < 0 || (r = drmModeGetResources(fd)) == NULL) {
		perror("open/resources");
		return 1;
	}
	for (i = 0; i < r->count_crtcs; i++) {
		drmModeCrtc *c = drmModeGetCrtc(fd, r->crtcs[i]);
		drmVBlank v;
		int ret;

		memset(&v, 0, sizeof (v));
		v.request.type = DRM_VBLANK_RELATIVE |
		    ((i << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK);
		v.request.sequence = 1;
		errno = 0;
		ret = drmWaitVBlank(fd, &v);
		printf("pipe %d crtc %u %s: wait_vblank = %d (%s) seq %u\n", i,
		    r->crtcs[i], c && c->mode_valid ? "ACTIVE" : "off", ret,
		    strerror(errno), v.reply.sequence);
		drmModeFreeCrtc(c);
	}
	return 0;
}
