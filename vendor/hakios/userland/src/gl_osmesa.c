/*
 * The OpenGL ES library behind src/eagl.c when that library is Mesa as tools/mk-mesa.sh builds
 * it: any of its drivers that draws into memory, reached through its off-screen interface
 * (OSMesa, given an OpenGL ES profile by tools/mesa/osmesa-es-profile.patch).
 *
 * This file is all that knows which library draws. eagl.c wants four things besides the gl*
 * functions themselves: a context of a version of OpenGL ES, that context made the calling
 * thread's, a context taken away again, and a name for the log. A library with another way to
 * its contexts gets another file like this one.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#include "hakios.gl.h"

/* GL/osmesa.h of Mesa */
#define OSMESA_RGBA                     0x1908
#define OSMESA_FORMAT                   0x22
#define OSMESA_DEPTH_BITS               0x30
#define OSMESA_STENCIL_BITS             0x31
#define OSMESA_PROFILE                  0x33
#define OSMESA_CONTEXT_MAJOR_VERSION    0x36
#define OSMESA_CONTEXT_MINOR_VERSION    0x37
#define OSMESA_ES_PROFILE               0x38
#define GL_UNSIGNED_BYTE                0x1401
#define GL_TEXTURE_2D                   0x0de1
#define GL_RENDERER                     0x1f01

typedef struct osmesa_context *OSMesaContext;
extern OSMesaContext OSMesaCreateContextAttribs(const int *attributes, OSMesaContext share);
extern unsigned char OSMesaMakeCurrent(OSMesaContext context, void *buffer, unsigned type, int width, int height);
extern void OSMesaDestroyContext(OSMesaContext context);
extern void (*OSMesaGetProcAddress(const char *name))(void);
extern const unsigned char *glGetString(unsigned name);
extern unsigned glGetError(void);

/*
 * OSMesa draws its "window" into memory handed to it. Nothing here draws into a window (the
 * window server draws into textures of its own, see eagl.c): a few pixels will do.
 */
#define WINDOW  16

struct hakios_gl {
	OSMesaContext context;
	unsigned char window[WINDOW * WINDOW * 4];
};

static pthread_once_t readback_once = PTHREAD_ONCE_INIT;
static void *(*readback_pin)(void *, uint32_t);
static void (*readback_release)(void *);
static void *(*readback_set)(void *);
static void (*readback_surface)(void *, uint32_t);

static void readback_functions(void)
{
	readback_pin = dlsym(RTLD_DEFAULT, "ios_kvm_readback_lease_create");
	readback_release = dlsym(RTLD_DEFAULT, "ios_kvm_readback_lease_destroy");
	readback_set = dlsym(RTLD_DEFAULT, "ios_kvm_readback_lease_set");
	readback_surface = dlsym(RTLD_DEFAULT, "ios_kvm_readback_lease_surface");
}

void *hakios_gl_readback_pin(void *address, uint32_t bytes)
{
	const char *enabled = getenv("IOS_KVM_DIRECT_READBACK");
	if (!enabled || strcmp(enabled, "1")) return NULL;
	pthread_once(&readback_once, readback_functions);
	return readback_pin && readback_release && readback_set ? readback_pin(address, bytes) : NULL;
}

void hakios_gl_readback_surface(void *lease, uint32_t surface)
{
    if (lease && readback_surface) readback_surface(lease, surface);
}

void hakios_gl_readback_release(void *lease)
{
	if (lease && readback_release) readback_release(lease);
}

void *hakios_gl_readback_set(void *lease)
{
	return readback_set ? readback_set(lease) : NULL;
}

struct hakios_gl *
hakios_gl_create(int version)
{
	const int attributes[] = {
		/* The dummy window is never drawn into; real FBOs supply their own attachments. */
		OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0, OSMESA_STENCIL_BITS, 0,
		OSMESA_PROFILE, OSMESA_ES_PROFILE, OSMESA_CONTEXT_MAJOR_VERSION, version, OSMESA_CONTEXT_MINOR_VERSION, 0,
		0
	};
	struct hakios_gl *gl = calloc(1, sizeof(*gl));

	if (gl != NULL && (gl->context = OSMesaCreateContextAttribs(attributes, NULL)) == NULL) {
		free(gl);
		gl = NULL;
	}
	return gl;
}

int
hakios_gl_make_current(struct hakios_gl *gl)
{
	if (gl == NULL) {
		return OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
	}
	return OSMesaMakeCurrent(gl->context, gl->window, GL_UNSIGNED_BYTE, WINDOW, WINDOW);
}

void
hakios_gl_destroy(struct hakios_gl *gl)
{
	OSMesaDestroyContext(gl->context);
	free(gl);
}

const char *
hakios_gl_library(void)
{
	return "Mesa, off-screen (OSMesa)";
}

/*
 * OES_EGL_image, with the IOSurface's ID as the image (tools/mesa/freedreno-darwin.patch: the
 * kernel's GPU driver makes a buffer of the surface's memory). The drivers that draw in memory
 * of their own with this processor are not asked: they refuse every time.
 */
int
hakios_gl_surface_texture(struct hakios_gl *gl, uint32_t surface)
{
	static void (*image_target)(unsigned target, void *image);
	static int asked, software;

	(void)gl;
	if (!asked) {
		const char *renderer = (const char *)glGetString(GL_RENDERER);

		asked = 1;
		image_target = (void (*)(unsigned, void *))OSMesaGetProcAddress("glEGLImageTargetTexture2DOES");
		/* VirGL renders on the host but cannot import a guest IOSurface ID as an EGLImage. */
		software = renderer == NULL || strstr(renderer, "softpipe") != NULL || strstr(renderer, "llvmpipe") != NULL ||
		    strstr(renderer, "virgl") != NULL;
	}
	if (image_target == NULL || software) {
		return 0;
	}
	while (glGetError() != 0) {
	}
	image_target(GL_TEXTURE_2D, (void *)(uintptr_t)surface);
	return glGetError() == 0;
}
