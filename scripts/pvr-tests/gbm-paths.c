// Which GBM rendering path writes into the bo that would be scanned out?
// Usage: gbm-paths s|b|m|x|f|n|w
//   s: mutter's path: gbm_surface + EGL window surface, eglSwapBuffers,
//      gbm_surface_lock_front_buffer()
//   b: wlroots' path: gbm_bo -> EGLImage -> texture -> FBO
//   m: like s, but gbm_surface_create_with_modifiers2(LINEAR) as mutter 48 does
//   x: like s, plus glDiscardFramebufferEXT(depth, stencil) before each swap, as cogl does
//   f: like s, but each frame first renders an offscreen FBO, then binds 0 (as mutter does)
//   n: like s, but the context is created with EGL_NO_CONFIG_KHR (as cogl does)
//   w: mutter's order: surface A, current; surface B, current; destroy A; render B
// Clears to red (and green on the second frame for s), then reads the bo's
// memory through its dma-buf and prints what is there.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef W
#define W 256
#endif
#ifndef H
#define H 256
#endif

static void dump(struct gbm_bo *bo, const char *what)
{
	int fd = gbm_bo_get_fd(bo);
	unsigned stride = gbm_bo_get_stride(bo);
	size_t len = (size_t)stride * H;
	uint32_t *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) { printf("%s: mmap of dma-buf failed\n", what); close(fd); return; }
	struct dma_buf_sync s = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &s);
	unsigned nz = 0, n = 0;
	for (unsigned y = 0; y < H; y += 7)
		for (unsigned x = 0; x < W; x += 7, n++)
			if (p[y * (stride / 4) + x] & 0x00ffffff) nz++;
	printf("%s: stride %u, first pixel %08x, centre %08x, non-zero %u/%u\n", what, stride,
	       p[0], p[(H / 2) * (stride / 4) + W / 2], nz, n);
	s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	ioctl(fd, DMA_BUF_IOCTL_SYNC, &s);
	munmap(p, len);
	close(fd);
}

int main(int argc, char **argv)
{
	char mode = argc > 1 ? argv[1][0] : 's';
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	struct gbm_device *g = gbm_create_device(fd);
	printf("gbm backend: %s\n", gbm_device_get_backend_name(g));
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	if (mode == 's' || mode == 'm' || mode == 'x' || mode == 'f' || mode == 'n' || mode == 'w') {
		EGLint ca2[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
				 EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
		EGLConfig cfgs[64], cfg = NULL;
		EGLint n = 0;
		eglChooseConfig(d, ca2, cfgs, 64, &n);
		for (int i = 0; i < n; i++) {
			EGLint vid;
			eglGetConfigAttrib(d, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid);
			if (vid == GBM_FORMAT_XRGB8888) { cfg = cfgs[i]; break; }
		}
		printf("configs %d, XRGB8888 config %s\n", n, cfg ? "found" : "MISSING");
		if (!cfg) return 2;
		uint64_t mods[] = { DRM_FORMAT_MOD_LINEAR };
		struct gbm_surface *gs = mode == 'm'
			? gbm_surface_create_with_modifiers2(g, W, H, GBM_FORMAT_XRGB8888, mods, 1,
							     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)
			: gbm_surface_create(g, W, H, GBM_FORMAT_XRGB8888,
					     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
		printf("gbm surface %p\n", (void *)gs);
		if (!gs) return 4;
		EGLSurface es = eglCreatePlatformWindowSurface(d, cfg, gs, NULL);
		EGLContext c = eglCreateContext(d, (mode == 'n' || mode == 'w') ? EGL_NO_CONFIG_KHR : cfg, EGL_NO_CONTEXT, ca);
		EGLBoolean mc = eglMakeCurrent(d, es, es, c);
		if (mode == 'w') {
			struct gbm_surface *gs2 = gbm_surface_create(g, W, H, GBM_FORMAT_XRGB8888,
								     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
			EGLSurface es2 = eglCreatePlatformWindowSurface(d, cfg, gs2, NULL);
			printf("surface B %p make-current %d\n", (void *)es2, eglMakeCurrent(d, es2, es2, c));
			printf("destroy A %d\n", eglDestroySurface(d, es));
			gbm_surface_destroy(gs);
			es = es2; gs = gs2;
		}
		printf("surface %p context %p make-current %d (err %#x)\n", (void *)es, (void *)c, mc, eglGetError());
		GLuint otex = 0, ofb = 0;
		if (mode == 'f') {
			glGenTextures(1, &otex); glBindTexture(GL_TEXTURE_2D, otex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glGenFramebuffers(1, &ofb);
		}
		for (int f = 0; f < 2; f++) {
			if (mode == 'f') {
				glBindFramebuffer(GL_FRAMEBUFFER, ofb);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, otex, 0);
				glViewport(0, 0, 128, 64);
				glClearColor(0, 0, 1, 1);
				glClear(GL_COLOR_BUFFER_BIT);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				printf("offscreen FBO %u drawn, bound 0 again\n", ofb);
			}
			glViewport(0, 0, W, H);
			glClearColor(f ? 0.0f : 1.0f, f ? 1.0f : 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			EGLint age = -1;
			eglQuerySurface(d, es, 0x313D /* EGL_BUFFER_AGE_EXT */, &age);
			if (mode == 'x') {
				PFNGLDISCARDFRAMEBUFFEREXTPROC disc = (void *)eglGetProcAddress("glDiscardFramebufferEXT");
				static const GLenum att[] = { GL_DEPTH_EXT, GL_STENCIL_EXT };
				if (disc) disc(GL_FRAMEBUFFER, 2, att);
				printf("discard depth+stencil: %s\n", disc ? "called" : "no entry point");
			}
			printf("buffer age %d; ", age);
			printf("frame %d swap %d (gl err %#x)\n", f, eglSwapBuffers(d, es), glGetError());
			struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs);
			if (!bo) { printf("lock_front_buffer failed\n"); return 3; }
			dump(bo, f ? "surface front (green expected)" : "surface front (red expected)");
			gbm_surface_release_buffer(gs, bo);
		}
		if (getenv("PAUSE")) sleep(4);
		return 0;
	}

	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
	PFNEGLCREATEIMAGEKHRPROC ci = (void *)eglGetProcAddress("eglCreateImageKHR");
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC t2d = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	struct gbm_bo *bo = gbm_bo_create(g, W, H, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	int bfd = gbm_bo_get_fd(bo);
	EGLint a[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
		       EGL_DMA_BUF_PLANE0_FD_EXT, bfd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gbm_bo_get_stride(bo), EGL_NONE };
	EGLImageKHR im = ci(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	close(bfd);
	GLuint t, fb;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); t2d(GL_TEXTURE_2D, im);
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
	printf("image %p fbo status %#x\n", im, glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, W, H);
	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	dump(bo, "bo via FBO (red expected)");
	return 0;
}
