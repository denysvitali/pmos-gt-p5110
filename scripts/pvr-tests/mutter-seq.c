// mutter's sequence from the PVR_FBO_DEFER_DEBUG log: a long-lived offscreen FBO
// F1 on a plain texture T1, client EGLImage textures drawn into F1 every frame,
// T1 drawn onto a "stage" F2. At client exit the image textures and images are
// freed, F1 is deleted last.
// Usage: mutter-seq p|q|r|s [frames]
//   p: free image textures, images, bos, then delete F1 (mutter's order)
//   q: control, delete F1 first
//   r: like p, but a second (client) context renders into each dmabuf before
//      the compositor samples it (pending GPU writes on the client buffers)
//   s: like r, delete F1 first (control)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define W 256
#define H 256
#define STEP(s) do { printf("%s\n", s); fflush(stdout); } while (0)

static PFNEGLCREATEIMAGEKHRPROC ci;
static PFNEGLDESTROYIMAGEKHRPROC di;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC t2d;

static GLuint plain_fbo(GLuint *tex)
{
	GLuint f;
	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glGenFramebuffers(1, &f);
	glBindFramebuffer(GL_FRAMEBUFFER, f);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
	return f;
}

int main(int argc, char **argv)
{
	char mode = argc > 1 ? argv[1][0] : 'p';
	int frames = argc > 2 ? atoi(argv[2]) : 200;
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	struct gbm_device *g = gbm_create_device(fd);
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	ci = (void *)eglGetProcAddress("eglCreateImageKHR");
	di = (void *)eglGetProcAddress("eglDestroyImageKHR");
	t2d = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	int rfd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gc = gbm_create_device(rfd);
	struct gbm_bo *bo[2];
	EGLImageKHR im[2];
	GLuint ti[2];
	for (int i = 0; i < 2; i++) {
		bo[i] = gbm_bo_create(gc, W, H, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
		int bfd = gbm_bo_get_fd(bo[i]);
		EGLint a[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
			       EGL_DMA_BUF_PLANE0_FD_EXT, bfd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
			       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gbm_bo_get_stride(bo[i]), EGL_NONE };
		im[i] = ci(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
		close(bfd);
		if (im[i] == EGL_NO_IMAGE_KHR) { printf("import failed 0x%x\n", eglGetError()); return 2; }
		glGenTextures(1, &ti[i]);
		glBindTexture(GL_TEXTURE_2D, ti[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		t2d(GL_TEXTURE_2D, im[i]);
	}

	int client = mode == 'r' || mode == 's';
	EGLContext cctx = EGL_NO_CONTEXT;
	GLuint cfb[2], ctex[2];
	if (client) {
		cctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, cctx);
		for (int i = 0; i < 2; i++) {
			glGenTextures(1, &ctex[i]);
			glBindTexture(GL_TEXTURE_2D, ctex[i]);
			t2d(GL_TEXTURE_2D, im[i]);
			glGenFramebuffers(1, &cfb[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, cfb[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctex[i], 0);
		}
		glViewport(0, 0, W, H);
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	}

	GLuint t1, t2;
	GLuint f1 = plain_fbo(&t1), f2 = plain_fbo(&t2);
	const char *vs = "attribute vec2 p; varying vec2 uv; void main(){uv=p*0.5+0.5; gl_Position=vec4(p,0.,1.);}";
	const char *fs = "precision mediump float; varying vec2 uv; uniform sampler2D s; void main(){gl_FragColor=texture2D(s,uv);}";
	GLuint pr = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glAttachShader(pr, v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glAttachShader(pr, f);
	glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
	static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
	glViewport(0, 0, W, H);

	for (int i = 0; i < frames; i++) {
		if (client) {
			eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, cctx);
			glBindFramebuffer(GL_FRAMEBUFFER, cfb[i & 1]);
			glClearColor((i % 10) / 10.0f, 0.5f, 0.2f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glFlush();
			eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, f1);
		glBindTexture(GL_TEXTURE_2D, ti[i & 1]);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glBindFramebuffer(GL_FRAMEBUFFER, f2);
		glBindTexture(GL_TEXTURE_2D, t1);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glFlush();
	}
	printf("drew %d frames\n", frames);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (mode == 'q' || mode == 's') { STEP("delete f1 (first)"); glDeleteFramebuffers(1, &f1); }
	STEP("delete image textures"); glDeleteTextures(2, ti);
	if (client) {
		STEP("client exits");
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, cctx);
		glDeleteFramebuffers(2, cfb);
		glDeleteTextures(2, ctex);
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
		eglDestroyContext(d, cctx);
	}
	STEP("destroy images and bos");
	for (int i = 0; i < 2; i++) { di(d, im[i]); gbm_bo_destroy(bo[i]); }
	if (mode != 'q' && mode != 's') { STEP("delete f1"); glDeleteFramebuffers(1, &f1); }
	STEP("delete f2"); glDeleteFramebuffers(1, &f2);
	STEP("finish"); glFinish();
	printf("done mode %c\n", mode);
	return 0;
}
