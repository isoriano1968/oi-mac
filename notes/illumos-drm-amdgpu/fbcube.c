/*
 * fbcube: a spinning cube from the Mac Pro 2013's compute GPU (amdgpu +
 * radeonsi), shown in a corner of the console framebuffer (/dev/fb on the
 * display GPU) without X. Each frame is drawn offscreen (EGL surfaceless,
 * GLES2, FBO with depth), read back with glReadPixels and copied into the
 * mmap'd console framebuffer: an overlay over the text console, not real
 * scanout (that comes with amdgpu on the display GPU).
 *
 * Build: gcc -O2 -g -Wall -DEGL_NO_X11 -o fbcube fbcube.c \
 *   -L/usr/lib/mesa/amd64 -R/usr/lib/mesa/amd64 -lEGL -lGLESv2 -lm
 * Run:   sudo ./fbcube [seconds] [size] [/dev/fb] [fps, 0 = unpaced]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/fbio.h>
#include <sys/visual_io.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static void
step(const char *s)
{
	fprintf(stderr, "fbcube: %s\n", s);
}

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec / 1e9);
}

static GLuint
shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof (log), NULL, log);
		fprintf(stderr, "fbcube: shader: %s\n", log);
		exit(1);
	}
	return (s);
}

/* m = a * b, column-major 4x4 */
static void
mul(float *m, const float *a, const float *b)
{
	float r[16];
	int i, j, k;

	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++) {
			r[j * 4 + i] = 0;
			for (k = 0; k < 4; k++)
				r[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
		}
	memcpy(m, r, sizeof (r));
}

int
main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 30;
	int S = argc > 2 ? atoi(argv[2]) : 400;
	const char *fbdev = argc > 3 ? argv[3] : "/dev/fb";
	int fps = argc > 4 ? atoi(argv[4]) : 60;
	struct vis_identifier vid;
	struct fbgattr ga;
	int fd, W, H, bpp, pitch, x0, y0, frames = 0, x, y;
	size_t fbsize;
	unsigned char *fb, *px;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint maj, min, n;
	static const EGLint cattr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, 0, EGL_NONE
	};
	static const EGLint xattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};
	static const char *vs =
	    "uniform mat4 mvp; uniform mat4 rot;\n"
	    "attribute vec3 pos; attribute vec3 col; attribute vec3 nrm;\n"
	    "varying vec3 c;\n"
	    "void main() {\n"
	    "  vec3 n = normalize((rot * vec4(nrm, 0.0)).xyz);\n"
	    "  float l = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.6, 1.0))), 0.0);\n"
	    "  c = col * l; gl_Position = mvp * vec4(pos, 1.0); }\n";
	static const char *fs =
	    "precision mediump float; varying vec3 c;\n"
	    "void main() { gl_FragColor = vec4(c, 1.0); }\n";
	/* 6 faces x 2 triangles x 3 vertices: pos, colour, normal */
	static GLfloat v[36 * 9];
	static const float face[6][3] = {
		{ 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
		{ -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }
	};
	static const float colour[6][3] = {
		{ 0.95f, 0.25f, 0.20f }, { 0.20f, 0.75f, 0.30f },
		{ 0.20f, 0.45f, 0.95f }, { 0.95f, 0.80f, 0.20f },
		{ 0.75f, 0.30f, 0.90f }, { 0.20f, 0.85f, 0.90f }
	};
	static const float quad[6][2] = {
		{ -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 }
	};
	GLuint fbo, rbc, rbd, prog;
	GLint umvp, urot;
	double t0, t;
	int f, i;

	setvbuf(stderr, NULL, _IONBF, 0);
	/* the cube's vertices */
	for (f = 0; f < 6; f++) {
		const float *N = face[f];
		float U[3], V[3];

		/* two axes across the face */
		U[0] = N[1] != 0 || N[2] != 0 ? 1 : 0;
		U[1] = 0;
		U[2] = N[0] != 0 ? 1 : 0;
		V[0] = N[1] * U[2] - N[2] * U[1];
		V[1] = N[2] * U[0] - N[0] * U[2];
		V[2] = N[0] * U[1] - N[1] * U[0];
		for (i = 0; i < 6; i++) {
			GLfloat *p = &v[(f * 6 + i) * 9];
			int k;

			for (k = 0; k < 3; k++) {
				p[k] = N[k] + quad[i][0] * U[k] + quad[i][1] * V[k];
				p[3 + k] = colour[f][k];
				p[6 + k] = N[k];
			}
		}
	}

	step("open the console framebuffer");
	if ((fd = open(fbdev, O_RDWR)) < 0) {
		perror(fbdev);
		return (1);
	}
	if (ioctl(fd, VIS_GETIDENTIFIER, &vid) == 0)
		fprintf(stderr, "fbcube: %s is %s\n", fbdev, vid.name);
	if (ioctl(fd, FBIOGATTR, &ga) != 0) {
		perror("FBIOGATTR");
		return (1);
	}
	W = ga.fbtype.fb_width;
	H = ga.fbtype.fb_height;
	bpp = ga.fbtype.fb_depth;
	fbsize = ga.fbtype.fb_size;
	/*
	 * FBIOGATTR has no line length; fb_size is rounded up (1600x900x32:
	 * 5763072 = 5760000 + 3072), so the pitch is the width's, as the
	 * firmware framebuffer's (vgatext) is.
	 */
	pitch = W * (bpp / 8);
	fprintf(stderr, "fbcube: %dx%d, %d bits, %zu bytes, pitch %d\n",
	    W, H, bpp, fbsize, pitch);
	if (bpp != 32 || (size_t)pitch * H > fbsize || S > W || S > H) {
		step("unsupported framebuffer layout");
		return (1);
	}
	fb = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap");
		return (1);
	}
	x0 = W - S - 16;
	y0 = 16;

	step("EGL surfaceless");
	gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
	    eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = gpd != NULL ? gpd(EGL_PLATFORM_SURFACELESS_MESA,
	    EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
		step("eglInitialize failed");
		return (1);
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1 ||
	    (ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr)) ==
	    EGL_NO_CONTEXT ||
	    !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "fbcube: context failed 0x%x\n", eglGetError());
		return (1);
	}
	fprintf(stderr, "fbcube: GL_RENDERER %s\n",
	    (const char *)glGetString(GL_RENDERER));

	glGenRenderbuffers(1, &rbc);
	glBindRenderbuffer(GL_RENDERBUFFER, rbc);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, S, S);
	glGenRenderbuffers(1, &rbd);
	glBindRenderbuffer(GL_RENDERBUFFER, rbd);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, S, S);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_RENDERBUFFER, rbc);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	    GL_RENDERBUFFER, rbd);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE) {
		step("framebuffer incomplete");
		return (1);
	}
	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glBindAttribLocation(prog, 2, "nrm");
	glLinkProgram(prog);
	glUseProgram(prog);
	umvp = glGetUniformLocation(prog, "mvp");
	urot = glGetUniformLocation(prog, "rot");
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat), v);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat),
	    v + 3);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof (GLfloat),
	    v + 6);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, S, S);
	px = malloc((size_t)S * S * 4);

	step("spinning");
	t0 = now();
	while ((t = now() - t0) < secs) {
		float a = (float)t * 1.1f, b = (float)t * 0.7f;
		float ry[16] = { cosf(a), 0, -sinf(a), 0, 0, 1, 0, 0,
		    sinf(a), 0, cosf(a), 0, 0, 0, 0, 1 };
		float rx[16] = { 1, 0, 0, 0, 0, cosf(b), sinf(b), 0,
		    0, -sinf(b), cosf(b), 0, 0, 0, 0, 1 };
		float tr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
		    0, 0, -5.0f, 1 };
		/* perspective: fov 45 degrees, aspect 1, near 1, far 20 */
		float fl = 1.0f / tanf(22.5f * 3.14159265f / 180.0f);
		float pr[16] = { fl, 0, 0, 0, 0, fl, 0, 0,
		    0, 0, -21.0f / 19.0f, -1, 0, 0, -40.0f / 19.0f, 0 };
		float rot[16], mvp[16];

		mul(rot, ry, rx);
		mul(mvp, tr, rot);
		mul(mvp, pr, mvp);
		glUniformMatrix4fv(umvp, 1, GL_FALSE, mvp);
		glUniformMatrix4fv(urot, 1, GL_FALSE, rot);
		glClearColor(0.06f, 0.07f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 36);
		glReadPixels(0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px);
		/* RGBA bottom-up -> the console's XRGB (B, G, R, X bytes) */
		for (y = 0; y < S; y++) {
			unsigned char *src = px + (size_t)(S - 1 - y) * S * 4;
			unsigned char *dst = fb + (size_t)(y0 + y) * pitch +
			    (size_t)x0 * 4;

			for (x = 0; x < S; x++) {
				dst[x * 4] = src[x * 4 + 2];
				dst[x * 4 + 1] = src[x * 4 + 1];
				dst[x * 4 + 2] = src[x * 4];
				dst[x * 4 + 3] = 0;
			}
		}
		frames++;
		/*
		 * No vertical blank to wait for on the firmware framebuffer:
		 * pace to about 60 frames a second, so each frame stays on the
		 * screen for a refresh (fewer tears than 1000+ fps).
		 */
		if (fps > 0) {
			double next = frames / (double)fps;
			double wait = next - (now() - t0);

			if (wait > 0)
				(void) usleep((useconds_t)(wait * 1e6));
		}
		if (frames % 120 == 0)
			fprintf(stderr, "fbcube: %d frames, %.1f fps\n",
			    frames, frames / t);
	}
	fprintf(stderr, "fbcube: %d frames in %.1f s = %.1f fps\n", frames, t,
	    frames / t);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	munmap(fb, fbsize);
	close(fd);
	step("done");
	return (0);
}
