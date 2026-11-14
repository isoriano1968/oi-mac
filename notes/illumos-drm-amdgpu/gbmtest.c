/* gbmtest: can GBM create a scanout front buffer on the display GPU? */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <gbm.h>

static void
try(struct gbm_device *g, int w, int h, unsigned fl, const char *what)
{
	struct gbm_bo *bo;

	errno = 0;
	bo = gbm_bo_create(g, w, h, GBM_FORMAT_XRGB8888, fl);
	printf("%-28s %4dx%-4d: %s (errno %d %s)", what, w, h, bo ? "ok" : "FAILED",
	    errno, strerror(errno));
	if (bo) {
		printf(" stride %u modifier 0x%llx", gbm_bo_get_stride(bo),
		    (unsigned long long)gbm_bo_get_modifier(bo));
		gbm_bo_destroy(bo);
	}
	printf("\n");
}

int
main(int argc, char **argv)
{
	const char *p = argc > 1 ? argv[1] : "/dev/dri/card0";
	int fd = open(p, O_RDWR | O_CLOEXEC);
	struct gbm_device *g;

	if (fd < 0) { perror(p); return 1; }
	g = gbm_create_device(fd);
	if (!g) { printf("gbm_create_device failed\n"); return 1; }
	printf("backend %s\n", gbm_device_get_backend_name(g));
	try(g, 3840, 2160, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING, "scanout|rendering");
	try(g, 3840, 2160, GBM_BO_USE_RENDERING, "rendering");
	try(g, 3840, 2160, GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR, "scanout|linear");
	try(g, 1920, 1080, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING, "scanout|rendering");
	try(g, 64, 64, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE, "cursor|write");
	gbm_device_destroy(g);
	close(fd);
	return 0;
}
