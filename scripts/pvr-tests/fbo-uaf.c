// Does deleting an FBO whose colour attachment was an EGLImage (dmabuf) texture,
// after the texture and image are gone, crash the PVR blob (+0x16e6c)?
// Usage: fbo-uaf a|b|c|d|e|f|g|h|i|j|k|m
//   a: render into FBO(tex<-image), delete tex, destroy image, free bo, delete FBO
//   b: control: delete FBO first, then tex, image, bo
//   c: like a, but the image and bo stay alive
//   d: like a, but nothing is rendered into the FBO (attach only)
//   e: like a, but glReadPixels from the FBO instead of drawing (cogl download)
//   f: like a, but the texture is detached from the FBO before it is deleted
//   g: like a, but the texture is deleted while the FBO is bound (implicit detach)
//   h: like f, but a plain glTexImage2D texture (no EGLImage)
//   i: like f, but the image and bo stay alive
//   j: FBO on a plain texture; draw sampling the EGLImage texture into it, then
//      delete the image texture, destroy image and bo, delete the FBO
//   k: like j, with glFinish before the deletes
//   m: like j, but the image and bo stay alive
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

int main(int argc, char **argv)
{
	char mode = argc > 1 ? argv[1][0] : 'a';
	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	struct gbm_device *g = gbm_create_device(fd);
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	PFNEGLCREATEIMAGEKHRPROC ci = (void *)eglGetProcAddress("eglCreateImageKHR");
	PFNEGLDESTROYIMAGEKHRPROC di = (void *)eglGetProcAddress("eglDestroyImageKHR");
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC t2d = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	// The "client" buffer, from the render node as a Wayland client would.
	int rfd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gc = gbm_create_device(rfd);
	struct gbm_bo *bo = gbm_bo_create(gc, W, H, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	int bfd = gbm_bo_get_fd(bo);
	EGLint a[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
		       EGL_DMA_BUF_PLANE0_FD_EXT, bfd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gbm_bo_get_stride(bo), EGL_NONE };
	EGLImageKHR im = ci(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	close(bfd);
	if (im == EGL_NO_IMAGE_KHR) { printf("image import failed 0x%x\n", eglGetError()); return 2; }

	GLuint tex, fbo;
	if (mode == 'j' || mode == 'k' || mode == 'm') {
		GLuint dst;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		t2d(GL_TEXTURE_2D, im);
		glGenTextures(1, &dst);
		glBindTexture(GL_TEXTURE_2D, dst);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
		printf("fbo status 0x%x (complete=0x8cd5)\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
		const char *vs = "attribute vec2 p; varying vec2 uv; void main(){uv=p*0.5+0.5; gl_Position=vec4(p,0.,1.);}";
		const char *fs = "precision mediump float; varying vec2 uv; uniform sampler2D s; void main(){gl_FragColor=texture2D(s,uv);}";
		GLuint pr = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glAttachShader(pr, v);
		glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glAttachShader(pr, f);
		glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
		static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
		glViewport(0, 0, W, H);
		glBindTexture(GL_TEXTURE_2D, tex);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		STEP("drew image texture into fbo");
		if (mode == 'k') { glFinish(); STEP("finished"); }
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		STEP("delete image tex"); glDeleteTextures(1, &tex);
		if (mode != 'm') {
			STEP("destroy image"); di(d, im);
			STEP("destroy bo"); gbm_bo_destroy(bo);
		}
		STEP("delete fbo"); glDeleteFramebuffers(1, &fbo);
		STEP("delete dst"); glDeleteTextures(1, &dst);
		STEP("finish"); glFinish();
		printf("done mode %c\n", mode);
		return 0;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	if (mode == 'h')
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	else
		t2d(GL_TEXTURE_2D, im);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	printf("fbo status 0x%x (complete=0x8cd5)\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
	if (mode == 'e') {
		static unsigned char px[W * H * 4];
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
		STEP("read pixels from fbo");
	} else if (mode != 'd') {
		glViewport(0, 0, W, H);
		glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		STEP("cleared fbo");
	}
	// Leave the FBO unbound (mutter's offscreen is not current when deleted).
	if (mode != 'g')
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (mode == 'b') { STEP("delete fbo (first)"); glDeleteFramebuffers(1, &fbo); }
	if (mode == 'f' || mode == 'h' || mode == 'i') {
		STEP("detach tex from fbo");
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	STEP("delete tex"); glDeleteTextures(1, &tex);
	if (mode == 'g')
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (mode != 'c' && mode != 'i') {
		STEP("destroy image"); di(d, im);
		STEP("destroy bo"); gbm_bo_destroy(bo);
	}
	if (mode != 'b') { STEP("delete fbo"); glDeleteFramebuffers(1, &fbo); }
	STEP("finish"); glFinish();
	printf("done mode %c\n", mode);
	return 0;
}
