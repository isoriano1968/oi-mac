/*
 * egltri: first triangle on the Mac Pro 2013's compute GPU (amdgpu bring-up).
 * EGL surfaceless (no window, no X): Mesa picks the render node, radeonsi
 * draws one RGB triangle into a 256x256 framebuffer object, glReadPixels
 * reads it back, and the picture goes to a PPM file. Every step is printed
 * unbuffered first, so a streamed run shows where it stopped.
 *
 * Build: gcc -O1 -g -Wall -DEGL_NO_X11 -o egltri egltri.c \
 *   -L/usr/lib/mesa/amd64 -R/usr/lib/mesa/amd64 -lEGL -lGLESv2
 * Run:   EGL_LOG_LEVEL=debug ./egltri [out.ppm] [clear]
 *   "clear": only clear to a colour and read back (no shaders, no draw).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define	W	256
#define	H	256

static void
step(const char *s)
{
	fprintf(stderr, "egltri: %s\n", s);
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
		fprintf(stderr, "egltri: shader: %s\n", log);
		exit(1);
	}
	return (s);
}

int
main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "tri.ppm";
	int clear_only = argc > 2 && strcmp(argv[2], "clear") == 0;
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
	    "attribute vec2 pos; attribute vec3 col; varying vec3 c;\n"
	    "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
	static const char *fs =
	    "precision mediump float; varying vec3 c;\n"
	    "void main() { gl_FragColor = vec4(c, 1.0); }\n";
	static const GLfloat v[] = {
		0.0f, 0.8f, 1.0f, 0.0f, 0.0f,
		-0.8f, -0.7f, 0.0f, 1.0f, 0.0f,
		0.8f, -0.7f, 0.0f, 0.0f, 1.0f,
	};
	GLuint fbo, rb, prog;
	unsigned char *px;
	FILE *f;
	int x, y;

	setvbuf(stderr, NULL, _IONBF, 0);
	step("eglGetPlatformDisplayEXT(SURFACELESS_MESA)");
	gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
	    eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (gpd == NULL) {
		step("no eglGetPlatformDisplayEXT");
		return (1);
	}
	dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	step("eglInitialize");
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
		fprintf(stderr, "egltri: eglInitialize failed 0x%x\n",
		    eglGetError());
		return (1);
	}
	fprintf(stderr, "egltri: EGL %d.%d %s\n", maj, min,
	    eglQueryString(dpy, EGL_VENDOR));
	eglBindAPI(EGL_OPENGL_ES_API);
	step("eglChooseConfig");
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1) {
		step("no config");
		return (1);
	}
	step("eglCreateContext");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr);
	if (ctx == EGL_NO_CONTEXT ||
	    !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "egltri: context failed 0x%x\n", eglGetError());
		return (1);
	}
	fprintf(stderr, "egltri: GL_RENDERER %s\n",
	    (const char *)glGetString(GL_RENDERER));
	fprintf(stderr, "egltri: GL_VERSION %s\n",
	    (const char *)glGetString(GL_VERSION));

	step("framebuffer object");
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, W, H);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
	    GL_FRAMEBUFFER_COMPLETE) {
		step("framebuffer incomplete");
		return (1);
	}
	glViewport(0, 0, W, H);
	step("clear");
	glClearColor(0.10f, 0.12f, 0.20f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!clear_only) {
		step("shaders");
		prog = glCreateProgram();
		glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
		glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
		glBindAttribLocation(prog, 0, "pos");
		glBindAttribLocation(prog, 1, "col");
		glLinkProgram(prog);
		glUseProgram(prog);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
		    5 * sizeof (GLfloat), v);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
		    5 * sizeof (GLfloat), v + 2);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		step("draw");
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
	step("glFinish");
	glFinish();
	step("glReadPixels");
	px = malloc(W * H * 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	fprintf(stderr, "egltri: GL error 0x%x; centre pixel %u %u %u, "
	    "corner %u %u %u\n", glGetError(),
	    px[(H / 2 * W + W / 2) * 4], px[(H / 2 * W + W / 2) * 4 + 1],
	    px[(H / 2 * W + W / 2) * 4 + 2], px[0], px[1], px[2]);
	f = fopen(out, "wb");
	if (f == NULL) {
		perror(out);
		return (1);
	}
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (y = H - 1; y >= 0; y--)
		for (x = 0; x < W; x++)
			fwrite(&px[(y * W + x) * 4], 1, 3, f);
	fclose(f);
	fprintf(stderr, "egltri: wrote %s\n", out);
	step("teardown");
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	step("done");
	return (0);
}
