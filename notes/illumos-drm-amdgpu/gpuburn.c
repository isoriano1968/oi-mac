/*
 * gpuburn: a sustained shader load on the Mac Pro 2013's compute GPU, to
 * see whether power management (si_dpm) raises the engine and memory
 * clocks. EGL surfaceless + GLES2: a fullscreen quad with an expensive
 * fragment shader into a 2048x2048 FBO, glFinish every frame, no readback.
 * Prints frames per second and the shader work per second.
 *
 * Build: gcc -O2 -g -Wall -DEGL_NO_X11 -o gpuburn gpuburn.c \
 *   -L/usr/lib/mesa/amd64 -R/usr/lib/mesa/amd64 -lEGL -lGLESv2
 * Run:   ./gpuburn [seconds] [iterations per pixel] [render node]
 * Without a render node, EGL's surfaceless platform picks the first GPU
 * (renderD128); with one (e.g. /dev/dri/renderD129) gpuburn takes that
 * device through EGL_EXT_device_enumeration and EGL_PLATFORM_DEVICE_EXT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define	S	2048

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec / 1e9);
}

int
main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 20, t0, t, last = 0;
	int iters = argc > 2 ? atoi(argv[2]) : 64, frames = 0, lf = 0;
	const char *node = argc > 3 ? argv[3] : NULL;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd;
	EGLDisplay dpy = EGL_NO_DISPLAY;
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
	    "attribute vec2 pos; varying vec2 p;\n"
	    "void main() { p = pos; gl_Position = vec4(pos, 0.0, 1.0); }\n";
	char fs[1024];
	static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint fbo, rb, prog, s1, s2;
	GLint ok, ut;
	char log[512];

	snprintf(fs, sizeof (fs),
	    "precision highp float; varying vec2 p; uniform float t;\n"
	    "void main() {\n"
	    "  vec2 z = p * 1.5 + vec2(sin(t), cos(t)) * 0.1; float a = 0.0;\n"
	    "  for (int i = 0; i < %d; i++) {\n"
	    "    z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + p * 0.7;\n"
	    "    a += sin(z.x) * cos(z.y);\n"
	    "    z = clamp(z, -2.0, 2.0);\n"
	    "  }\n"
	    "  gl_FragColor = vec4(fract(a), fract(a * 0.5), 0.5, 1.0);\n"
	    "}\n", iters);
	setvbuf(stderr, NULL, _IONBF, 0);
	gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
	    eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (gpd != NULL && node != NULL) {
		PFNEGLQUERYDEVICESEXTPROC qd = (PFNEGLQUERYDEVICESEXTPROC)
		    eglGetProcAddress("eglQueryDevicesEXT");
		PFNEGLQUERYDEVICESTRINGEXTPROC qs =
		    (PFNEGLQUERYDEVICESTRINGEXTPROC)
		    eglGetProcAddress("eglQueryDeviceStringEXT");
		EGLDeviceEXT devs[16];
		EGLint nd = 0, i;

		if (qd == NULL || qs == NULL || !qd(16, devs, &nd)) {
			fprintf(stderr, "gpuburn: no EGL device enumeration\n");
			return (1);
		}
		for (i = 0; i < nd; i++) {
			const char *r = qs(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT);

			if (r != NULL && strcmp(r, node) == 0) {
				dpy = gpd(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
				break;
			}
		}
		if (i == nd) {
			fprintf(stderr, "gpuburn: no EGL device for %s\n", node);
			return (1);
		}
	} else if (gpd != NULL) {
		dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
		    NULL);
	}
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
		fprintf(stderr, "gpuburn: eglInitialize failed\n");
		return (1);
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1 ||
	    (ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr)) ==
	    EGL_NO_CONTEXT ||
	    !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "gpuburn: context failed 0x%x\n", eglGetError());
		return (1);
	}
	fprintf(stderr, "gpuburn: %s, %dx%d, %d iterations per pixel\n",
	    (const char *)glGetString(GL_RENDERER), S, S, iters);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, S, S);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_RENDERBUFFER, rb);
	prog = glCreateProgram();
	s1 = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(s1, 1, &vs, NULL);
	glCompileShader(s1);
	s2 = glCreateShader(GL_FRAGMENT_SHADER);
	{
		const char *src = fs;

		glShaderSource(s2, 1, &src, NULL);
	}
	glCompileShader(s2);
	glGetShaderiv(s2, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s2, sizeof (log), NULL, log);
		fprintf(stderr, "gpuburn: shader: %s\n", log);
		return (1);
	}
	glAttachShader(prog, s1);
	glAttachShader(prog, s2);
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	ut = glGetUniformLocation(prog, "t");
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
	glEnableVertexAttribArray(0);
	glViewport(0, 0, S, S);
	t0 = now();
	while ((t = now() - t0) < secs) {
		glUniform1f(ut, (float)t);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glFinish();
		frames++;
		if (t - last >= 2.0) {
			double f = (frames - lf) / (t - last);

			fprintf(stderr, "gpuburn: t=%.0f s  %.1f fps  "
			    "%.1f G pixel-iterations/s\n", t, f,
			    f * S * S * iters / 1e9);
			last = t;
			lf = frames;
		}
	}
	fprintf(stderr, "gpuburn: %d frames in %.1f s = %.1f fps, GL error "
	    "0x%x\n", frames, t, frames / t, glGetError());
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	return (0);
}
