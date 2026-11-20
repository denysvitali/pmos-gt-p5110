// mutter's sequence with a real client process: the forked client allocates two
// dmabufs, passes them over a socketpair, renders into them on request and
// exits. The parent (compositor) imports them, draws the client buffer into a
// long-lived offscreen F1 (plain texture T1) and T1 onto a stage F2 each frame,
// then frees the image textures and images and deletes F1.
// Usage: mutter-xproc t|u [frames]
//   t: mutter's order (image textures and images freed, then F1 deleted)
//   u: control, F1 deleted before the client buffers are freed
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define W 256
#define H 256
#define STEP(s) do { printf("%s\n", s); fflush(stdout); } while (0)

static PFNEGLCREATEIMAGEKHRPROC ci;
static PFNEGLDESTROYIMAGEKHRPROC di;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC t2d;

static void send_fd(int s, int fd, unsigned stride)
{
	char buf[CMSG_SPACE(sizeof(int))];
	struct iovec io = { &stride, sizeof(stride) };
	struct msghdr m = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = buf, .msg_controllen = sizeof(buf) };
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(c), &fd, sizeof(int));
	if (sendmsg(s, &m, 0) < 0) { perror("sendmsg"); exit(3); }
}

static int recv_fd(int s, unsigned *stride)
{
	char buf[CMSG_SPACE(sizeof(int))];
	struct iovec io = { stride, sizeof(*stride) };
	struct msghdr m = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = buf, .msg_controllen = sizeof(buf) };
	if (recvmsg(s, &m, 0) <= 0) { perror("recvmsg"); exit(3); }
	int fd; memcpy(&fd, CMSG_DATA(CMSG_FIRSTHDR(&m)), sizeof(int));
	return fd;
}

static EGLDisplay egl_up(const char *node, EGLContext *ctx)
{
	int fd = open(node, O_RDWR | O_CLOEXEC);
	struct gbm_device *g = gbm_create_device(fd);
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	*ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, *ctx);
	ci = (void *)eglGetProcAddress("eglCreateImageKHR");
	di = (void *)eglGetProcAddress("eglDestroyImageKHR");
	t2d = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	return d;
}

static EGLImageKHR import(EGLDisplay d, int fd, unsigned stride)
{
	EGLint a[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
		       EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		       EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride, EGL_NONE };
	EGLImageKHR im = ci(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	if (im == EGL_NO_IMAGE_KHR) { printf("import failed 0x%x\n", eglGetError()); exit(2); }
	return im;
}

static GLuint image_tex(EGLImageKHR im)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	t2d(GL_TEXTURE_2D, im);
	return t;
}

static GLuint fbo_on(GLuint tex)
{
	GLuint f;
	glGenFramebuffers(1, &f);
	glBindFramebuffer(GL_FRAMEBUFFER, f);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	return f;
}

static void client(int s, int frames)
{
	EGLContext ctx;
	int rfd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gc = gbm_create_device(rfd);
	struct gbm_bo *bo[2];
	EGLDisplay d = egl_up("/dev/dri/card0", &ctx);
	GLuint fb[2];
	for (int i = 0; i < 2; i++) {
		bo[i] = gbm_bo_create(gc, W, H, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
		int fd = gbm_bo_get_fd(bo[i]);
		send_fd(s, fd, gbm_bo_get_stride(bo[i]));
		fb[i] = fbo_on(image_tex(import(d, fd, gbm_bo_get_stride(bo[i]))));
		close(fd);
	}
	glViewport(0, 0, W, H);
	for (int i = 0; i < frames; i++) {
		char c;
		if (read(s, &c, 1) != 1) break;
		glBindFramebuffer(GL_FRAMEBUFFER, fb[i & 1]);
		glClearColor((i % 10) / 10.0f, 0.5f, 0.2f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glFlush();
		if (write(s, &c, 1) != 1) break;
	}
	_exit(0); /* like a client that just quits: the kernel tears everything down */
}

int main(int argc, char **argv)
{
	char mode = argc > 1 ? argv[1][0] : 't';
	int frames = argc > 2 ? atoi(argv[2]) : 200;
	int sv[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	pid_t pid = fork();
	if (pid == 0) { close(sv[0]); client(sv[1], frames); }
	close(sv[1]);

	EGLContext ctx;
	EGLDisplay d = egl_up("/dev/dri/card0", &ctx);
	EGLImageKHR im[2];
	GLuint ti[2];
	for (int i = 0; i < 2; i++) {
		unsigned stride;
		int fd = recv_fd(sv[0], &stride);
		im[i] = import(d, fd, stride);
		close(fd);
		ti[i] = image_tex(im[i]);
	}
	GLuint t1, t2;
	glGenTextures(1, &t1); glBindTexture(GL_TEXTURE_2D, t1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glGenTextures(1, &t2); glBindTexture(GL_TEXTURE_2D, t2);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	GLuint f1 = fbo_on(t1), f2 = fbo_on(t2);
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
		char c = 'f';
		if (write(sv[0], &c, 1) != 1 || read(sv[0], &c, 1) != 1) break;
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
	if (mode == 'u') { STEP("delete f1 (first)"); glDeleteFramebuffers(1, &f1); }
	close(sv[0]);
	waitpid(pid, NULL, 0);
	STEP("client exited");
	STEP("delete image textures"); glDeleteTextures(2, ti);
	STEP("destroy images"); for (int i = 0; i < 2; i++) di(d, im[i]);
	if (mode != 'u') { STEP("delete f1"); glDeleteFramebuffers(1, &f1); }
	STEP("delete f2"); glDeleteFramebuffers(1, &f2);
	STEP("finish"); glFinish();
	printf("done mode %c\n", mode);
	return 0;
}
