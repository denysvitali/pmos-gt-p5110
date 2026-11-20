// Do VBO vertex paths work on the SGX blob? Draw a full-FBO quad (red) with
// vertices from: client array, glBufferData, glBufferSubData, glMapBufferOES.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <string.h>
static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
static void run(const char *name, int mode)
{
	GLuint t, fb, vbo = 0;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
	glViewport(0, 0, 16, 16); glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
	if (mode == 0) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q);
	} else {
		glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
		if (mode == 1) glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
		if (mode == 2) { glBufferData(GL_ARRAY_BUFFER, sizeof q, NULL, GL_DYNAMIC_DRAW); glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof q, q); }
		if (mode == 4) { /* cogl: map, bind 0, write, rebind, unmap */
			PFNGLMAPBUFFEROESPROC map = (void *)eglGetProcAddress("glMapBufferOES");
			PFNGLUNMAPBUFFEROESPROC unmap = (void *)eglGetProcAddress("glUnmapBufferOES");
			glBufferData(GL_ARRAY_BUFFER, sizeof q, NULL, GL_DYNAMIC_DRAW);
			void *p = map(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			memcpy(p, q, sizeof q);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			printf("  unmap -> %d\n", unmap(GL_ARRAY_BUFFER));
		}
		if (mode == 3) {
			PFNGLMAPBUFFEROESPROC map = (void *)eglGetProcAddress("glMapBufferOES");
			PFNGLUNMAPBUFFEROESPROC unmap = (void *)eglGetProcAddress("glUnmapBufferOES");
			glBufferData(GL_ARRAY_BUFFER, sizeof q, NULL, GL_DYNAMIC_DRAW);
			void *p = map ? map(GL_ARRAY_BUFFER, GL_WRITE_ONLY_OES) : NULL;
			if (p) { memcpy(p, q, sizeof q); unmap(GL_ARRAY_BUFFER); }
			printf("  map entry %p mapped %p\n", (void *)map, p);
		}
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	}
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[4];
	glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-28s err %#06x  rgba %3u %3u %3u %3u (255 0 0 = drawn, 0 0 255 = not)\n", name, glGetError(), px[0], px[1], px[2], px[3]);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (vbo) glDeleteBuffers(1, &vbo);
	glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fb); glDeleteTextures(1, &t);
}
int main(void)
{
	struct gbm_device *g = gbm_create_device(open("/dev/dri/card0", 02));
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL); eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
	printf("mapbuffer ext: %s\n", strstr((const char *)glGetString(GL_EXTENSIONS), "GL_OES_mapbuffer") ? "yes" : "no");
	const char *vs = "attribute vec2 p; void main(){gl_Position=vec4(p,0.,1.);}";
	const char *fs = "precision mediump float; void main(){gl_FragColor=vec4(1.,0.,0.,1.);}";
	GLuint pr = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glAttachShader(pr, v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glAttachShader(pr, f);
	glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
	run("client array", 0); run("VBO BufferData", 1); run("VBO BufferSubData", 2); run("VBO MapBufferOES", 3);
	run("VBO Map, bind 0, write, rebind", 4);
	return 0;
}
