#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint; typedef int GLsizei;
typedef void (*_glapi_proc)(void);
typedef enum { PVRDRI_API_GLES1 = 2, PVRDRI_API_GLES2 = 3 } PVRDRIAPIType;
typedef struct { int dummy; } PVRDRIContext;
/* wrap_block.c: the workaround block cut out of the patched mesa_context.c */
#include "wrap_block.c"
/* fake GL */
static GLint bound_tex, bound_fbo; static char log_[512];
static void f_getint(GLenum e, GLint *v) { *v = e == 0x8069 ? bound_tex : e == 0x8CA6 ? bound_fbo : 0; }
static void f_img(GLenum t, void *i) { (void)t; (void)i; }
static void f_fbtex(GLenum a, GLenum b, GLenum c, GLuint t, GLint l) { (void)a;(void)b;(void)c;(void)t;(void)l; }
static void f_deltex(GLsizei n, const GLuint *t) { for (int i = 0; i < n; i++) sprintf(log_ + strlen(log_), "T%u ", t[i]); }
static void f_delfbo(GLsizei n, const GLuint *f) { for (int i = 0; i < n; i++) sprintf(log_ + strlen(log_), "F%u ", f[i]); }
#define CHECK(want) do { printf("%-40s %s [%s]\n", #want, strcmp(log_, want) ? "FAIL" : "ok", log_); fails += !!strcmp(log_, want); log_[0] = 0; } while (0)
int main(void) {
	int fails = 0; PVRDRIContext c1, c2;
	real_GetIntegerv = f_getint;
	void (*img)(GLenum, void *) = (void *)pvrdri_wrap_func(PVRDRI_API_GLES2, NULL, "glEGLImageTargetTexture2DOES", (_glapi_proc)f_img);
	void (*fbtex)(GLenum, GLenum, GLenum, GLuint, GLint) = (void *)pvrdri_wrap_func(PVRDRI_API_GLES2, NULL, "glFramebufferTexture2D", (_glapi_proc)f_fbtex);
	void (*deltex)(GLsizei, const GLuint *) = (void *)pvrdri_wrap_func(PVRDRI_API_GLES2, NULL, "glDeleteTextures", (_glapi_proc)f_deltex);
	void (*delfbo)(GLsizei, const GLuint *) = (void *)pvrdri_wrap_func(PVRDRI_API_GLES2, NULL, "glDeleteFramebuffers", (_glapi_proc)f_delfbo);
	if ((void *)pvrdri_wrap_func(PVRDRI_API_GLES1, NULL, "glDeleteTextures", (_glapi_proc)f_deltex) != (void *)f_deltex) { puts("GLES1 wrapped: FAIL"); fails++; }
	gpvCurCtx = &c1;
	GLuint t5 = 5, t6 = 6, t7 = 7, f9 = 9, f10 = 10;
	/* image tex 5 attached to fbo 9; plain tex 6 attached too */
	bound_tex = 5; img(0x0DE1, NULL); bound_fbo = 9; fbtex(0x8D40, 0x8CE0, 0x0DE1, 5, 0); fbtex(0x8D40, 0x8CE0, 0x0DE1, 6, 0);
	GLuint both[] = { 5, 6 }; deltex(2, both); CHECK("T6 ");          /* 5 deferred, 6 deleted */
	delfbo(1, &f10); CHECK("F10 ");                                   /* unrelated fbo: 5 stays */
	delfbo(1, &f9); CHECK("F9 T5 ");                                  /* fbo first, then the texture */
	/* image tex 7 never attached: deleted immediately */
	bound_tex = 7; img(0x0DE1, NULL); deltex(1, &t7); CHECK("T7 ");
	delfbo(1, &f9); CHECK("F9 ");                                     /* nothing left to free */
	/* per-context isolation: ctx2 deletes name 5 while ctx1 still has it attached */
	bound_tex = 5; img(0x0DE1, NULL); bound_fbo = 9; fbtex(0x8D40, 0x8CE0, 0x0DE1, 5, 0);
	gpvCurCtx = &c2; deltex(1, &t5); CHECK("T5 ");                    /* other ctx: not deferred */
	gpvCurCtx = &c1; deltex(1, &t5); CHECK("");                       /* ctx1: deferred */
	pvrdri_forget_context(&c1); delfbo(1, &f9); CHECK("F9 ");         /* forgotten: no stale delete */
	/* detach (texture 0) is not recorded, earlier attachment still defers */
	bound_tex = 6; img(0x0DE1, NULL); bound_fbo = 10; fbtex(0x8D40, 0x8CE0, 0x0DE1, 6, 0); fbtex(0x8D40, 0x8CE0, 0x0DE1, 0, 0);
	deltex(1, &t6); CHECK(""); delfbo(1, &f10); CHECK("F10 T6 ");
	/* many textures (> stack buffer) */
	GLuint many[40]; for (int i = 0; i < 40; i++) many[i] = 100 + i; deltex(40, many);
	printf("40 plain deletes: %s\n", strlen(log_) > 150 ? "ok" : "FAIL"); fails += strlen(log_) <= 150; log_[0] = 0;
	printf("%s\n", fails ? "FAILURES" : "all ok"); return fails;
}
