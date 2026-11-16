/* LD_PRELOAD shim: make DKIOCGMEDIAINFO(EXT) report 4096-byte logical sectors */
#include <sys/types.h>
#include <sys/dkio.h>
#include <dlfcn.h>
#include <stdarg.h>
int
ioctl(int fd, int req, ...)
{
	static int (*real)(int, int, ...);
	va_list ap; void *arg; int rv;
	va_start(ap, req); arg = va_arg(ap, void *); va_end(ap);
	if (real == NULL) real = (int (*)(int, int, ...))dlsym(RTLD_NEXT, "ioctl");
	rv = real(fd, req, arg);
	if (rv == 0 && req == DKIOCGMEDIAINFO) {
		struct dk_minfo *m = arg;
		m->dki_capacity = m->dki_capacity * m->dki_lbsize / 4096;
		m->dki_lbsize = 4096;
	} else if (rv == 0 && req == DKIOCGMEDIAINFOEXT) {
		struct dk_minfo_ext *m = arg;
		m->dki_capacity = m->dki_capacity * m->dki_lbsize / 4096;
		m->dki_lbsize = 4096; m->dki_pbsize = 4096;
	}
	return (rv);
}
