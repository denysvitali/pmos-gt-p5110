// Which texture upload paths produce a sampleable texture on the SGX blob?
// Each case uploads a 4x4 texture whose texels are (r,g,b,a) = (200, 40, 10, 255),
// draws it into a 16x16 RGBA FBO with a plain sampling shader and reads the
// centre pixel back.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <string.h>

static PFNGLTEXSTORAGE2DEXTPROC texstorage;

static void draw_and_read(const char *name, GLuint tex, GLenum uploaderr)
{
	GLuint t, fb;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
	glViewport(0, 0, 16, 16);
	glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);   /* blue = not drawn */
	glBindTexture(GL_TEXTURE_2D, tex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[4] = { 0 };
	glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-44s upload err %#06x  sampled rgba %3u %3u %3u %3u\n", name, uploaderr, px[0], px[1], px[2], px[3]);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fb); glDeleteTextures(1, &t);
}

static GLuint newtex(void)
{
	GLuint t;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

int main(void)
{
	struct gbm_device *g = gbm_create_device(open("/dev/dri/card0", 02));
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, g, NULL);
	eglInitialize(d, NULL, NULL); eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
	texstorage = (void *)eglGetProcAddress("glTexStorage2DEXT");

	const char *vs = "attribute vec2 p; varying vec2 uv; void main(){uv=p*0.5+0.5; gl_Position=vec4(p,0.,1.);}";
	const char *fs = "precision mediump float; varying vec2 uv; uniform sampler2D s; void main(){gl_FragColor=texture2D(s,uv);}";
	GLuint pr = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glAttachShader(pr, v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glAttachShader(pr, f);
	glBindAttribLocation(pr, 0, "p"); glLinkProgram(pr); glUseProgram(pr);
	static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);

	unsigned char rgba[64], bgra[64], one[16];
	for (int i = 0; i < 16; i++) {
		rgba[i*4+0] = 200; rgba[i*4+1] = 40; rgba[i*4+2] = 10; rgba[i*4+3] = 255;
		bgra[i*4+0] = 10;  bgra[i*4+1] = 40; bgra[i*4+2] = 200; bgra[i*4+3] = 255;
		one[i] = 200;
	}
	GLuint t; GLenum e;

	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	e = glGetError(); draw_and_read("A RGBA/RGBA TexImage", t, e);
	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, 4, 4, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
	e = glGetError(); draw_and_read("B BGRA/BGRA TexImage (EXT semantics)", t, e);
	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
	e = glGetError(); draw_and_read("C RGBA/BGRA TexImage (IMG semantics)", t, e);
	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, 4, 4, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, NULL);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
	e = glGetError(); draw_and_read("B2 BGRA TexImage(NULL) + TexSubImage", t, e);
	if (texstorage) {
		t = newtex(); texstorage(GL_TEXTURE_2D, 1, GL_BGRA8_EXT, 4, 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
		e = glGetError(); draw_and_read("D TexStorage(BGRA8) + TexSubImage BGRA", t, e);
		t = newtex(); texstorage(GL_TEXTURE_2D, 1, GL_RGBA8_OES, 4, 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		e = glGetError(); draw_and_read("E TexStorage(RGBA8) + TexSubImage RGBA", t, e);
	} else {
		printf("no glTexStorage2DEXT entry point\n");
	}
	t = newtex(); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 4, 4, 0, GL_ALPHA, GL_UNSIGNED_BYTE, one);
	e = glGetError(); draw_and_read("F ALPHA (expect a=200)", t, e);
	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_RED_EXT, 4, 4, 0, GL_RED_EXT, GL_UNSIGNED_BYTE, one);
	e = glGetError(); draw_and_read("G RED_EXT (expect r=200)", t, e);
	t = newtex(); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	e = glGetError(); draw_and_read("H RGBA, mipmap filter, no mipmaps", t, e);
	/* NPOT cases (5x3) */
	unsigned char np[5 * 3 * 4];
	for (int i = 0; i < 15; i++) { np[i*4] = 200; np[i*4+1] = 40; np[i*4+2] = 10; np[i*4+3] = 255; }
	struct { const char *n; GLint wrap, minf; int mip; } cs[] = {
		{ "I NPOT clamp linear", GL_CLAMP_TO_EDGE, GL_LINEAR, 0 },
		{ "J NPOT repeat linear", GL_REPEAT, GL_LINEAR, 0 },
		{ "K NPOT clamp, GenerateMipmap, trilinear", GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, 1 },
		{ "L NPOT clamp, GenerateMipmap, linear", GL_CLAMP_TO_EDGE, GL_LINEAR, 1 },
	};
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		GLuint tt; glGenTextures(1, &tt); glBindTexture(GL_TEXTURE_2D, tt);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 5, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, np);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, cs[i].wrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, cs[i].wrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, cs[i].minf);
		if (cs[i].mip) glGenerateMipmap(GL_TEXTURE_2D);
		e = glGetError(); draw_and_read(cs[i].n, tt, e);
	}
	return 0;
}
