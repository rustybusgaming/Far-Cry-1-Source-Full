////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESContext.cpp
//  Description: WebGL2 context creation. See GLESContext.h for the design.
//
////////////////////////////////////////////////////////////////////////////

#include "GLESContext.h"

#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>

//! GL_EXT_texture_filter_anisotropic is an extension even in GLES 3.0, so its
//! enum is not in gl3.h.
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	g_hContext = 0;
static SGLESContextInfo					g_info;
static int								g_bInfoValid = 0;

//////////////////////////////////////////////////////////////////////////
//! Emscripten's extension check takes the bare name and also tries the
//! vendor-prefixed spellings, which still matter for the compressed-texture
//! extensions on older builds.
//////////////////////////////////////////////////////////////////////////
static int HasExtension(const char* szName)
{
	return emscripten_webgl_enable_extension(g_hContext, szName) ? 1 : 0;
}

static void QueryInfo(void)
{
	memset(&g_info, 0, sizeof(g_info));

	glGetIntegerv(GL_MAX_TEXTURE_SIZE,            &g_info.nMaxTextureSize);
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,   &g_info.nMaxCubeTextureSize);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,     &g_info.nMaxTextureUnits);
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,          &g_info.nMaxVertexAttribs);
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,       &g_info.nMaxColorAttachments);
	glGetIntegerv(GL_MAX_SAMPLES,                 &g_info.nMaxSamples);

	// Enabling an extension in WebGL is what makes its enums and entry points
	// live, so this both queries and switches them on.
	g_info.bHasAnisotropic = HasExtension("EXT_texture_filter_anisotropic");
	g_info.bHasS3TC        = HasExtension("WEBGL_compressed_texture_s3tc");

	g_info.szVendor   = (const char*)glGetString(GL_VENDOR);
	g_info.szRenderer = (const char*)glGetString(GL_RENDERER);
	g_info.szVersion  = (const char*)glGetString(GL_VERSION);

	g_bInfoValid = 1;
}

//////////////////////////////////////////////////////////////////////////

int GLESContext_Create(const char* szCanvasSelector, int nWidth, int nHeight)
{
	if (g_hContext)
	{
		printf("[gles] context already created\n");
		return 1;
	}

	if (!szCanvasSelector)
		szCanvasSelector = "#canvas";

	EmscriptenWebGLContextAttributes attr;
	emscripten_webgl_init_context_attributes(&attr);

	// majorVersion 2 IS the request for WebGL2. Emscripten will not silently
	// fall back to WebGL1: if the browser cannot provide 2, creation fails,
	// which is what we want -- a WebGL1 context could not run this renderer
	// and failing early beats failing at the first shader compile.
	attr.majorVersion = 2;
	attr.minorVersion = 0;

	attr.alpha                        = EM_FALSE;	// the engine owns every pixel;
													// an alpha channel just costs
													// compositing work
	attr.depth                        = EM_TRUE;
	attr.stencil                      = EM_TRUE;	// stencil shadow volumes
	attr.antialias                    = EM_FALSE;	// the engine does its own AA
	attr.preserveDrawingBuffer        = EM_FALSE;
	attr.premultipliedAlpha           = EM_TRUE;
	attr.failIfMajorPerformanceCaveat = EM_FALSE;	// software GL is slow but
													// still better than nothing
	attr.enableExtensionsByDefault    = EM_TRUE;

	g_hContext = emscripten_webgl_create_context(szCanvasSelector, &attr);
	if (!g_hContext)
	{
		printf("[gles] failed to create a WebGL2 context on '%s'\n", szCanvasSelector);
		return 0;
	}

	if (emscripten_webgl_make_context_current(g_hContext) != EMSCRIPTEN_RESULT_SUCCESS)
	{
		printf("[gles] created a context on '%s' but could not make it current\n",
		       szCanvasSelector);
		emscripten_webgl_destroy_context(g_hContext);
		g_hContext = 0;
		return 0;
	}

	GLESContext_Resize(nWidth, nHeight);
	QueryInfo();

	printf("[gles] WebGL2 context on '%s', %dx%d\n", szCanvasSelector, nWidth, nHeight);
	printf("[gles]   vendor   : %s\n", g_info.szVendor   ? g_info.szVendor   : "?");
	printf("[gles]   renderer : %s\n", g_info.szRenderer ? g_info.szRenderer : "?");
	printf("[gles]   version  : %s\n", g_info.szVersion  ? g_info.szVersion  : "?");
	printf("[gles]   max tex %d, cube %d, units %d, attribs %d, MRT %d, samples %d\n",
	       g_info.nMaxTextureSize, g_info.nMaxCubeTextureSize, g_info.nMaxTextureUnits,
	       g_info.nMaxVertexAttribs, g_info.nMaxColorAttachments, g_info.nMaxSamples);
	printf("[gles]   anisotropic %s, S3TC %s\n",
	       g_info.bHasAnisotropic ? "yes" : "no",
	       g_info.bHasS3TC ? "yes" : "no");

	// Worth saying out loud: without S3TC every DXT texture in the game's paks
	// has to be decompressed on load, which costs both time and a great deal
	// of memory on a 32-bit heap.
	if (!g_info.bHasS3TC)
		printf("[gles]   WARNING: no S3TC; DXT textures will need CPU decompression\n");

	return 1;
}

void GLESContext_Destroy(void)
{
	if (!g_hContext)
		return;

	emscripten_webgl_destroy_context(g_hContext);
	g_hContext = 0;
	g_bInfoValid = 0;
}

int GLESContext_MakeCurrent(void)
{
	if (!g_hContext)
		return 0;
	return emscripten_webgl_make_context_current(g_hContext) == EMSCRIPTEN_RESULT_SUCCESS;
}

void GLESContext_Resize(int nWidth, int nHeight)
{
	if (!g_hContext || nWidth <= 0 || nHeight <= 0)
		return;

	// This sets the drawing buffer, which is independent of the canvas's CSS
	// size. The engine's notion of resolution is the drawing buffer.
	emscripten_set_canvas_element_size("#canvas", nWidth, nHeight);
	glViewport(0, 0, nWidth, nHeight);
}

const SGLESContextInfo* GLESContext_GetInfo(void)
{
	return g_bInfoValid ? &g_info : 0;
}

int GLESContext_IsCreated(void)
{
	return g_hContext != 0;
}

#else //__EMSCRIPTEN__

//////////////////////////////////////////////////////////////////////////
// Native builds have no context here.
//
// This is not a stub standing in for work not done -- it is the honest state
// of the target. There is no GL implementation in this container (no
// /usr/include/GL, no EGL), and more importantly the backend's reason to
// exist is WebGL2. A native GLES path would need EGL plus a surfaceless
// platform extension, and would only be worth adding as a debugging aid.
//
// The functions exist so the renderer above compiles for both targets and
// reports a clean failure rather than not linking.
//////////////////////////////////////////////////////////////////////////

int GLESContext_Create(const char* szCanvasSelector, int nWidth, int nHeight)
{
	(void)szCanvasSelector; (void)nWidth; (void)nHeight;
	printf("[gles] no GL context available on this target (built without Emscripten)\n");
	return 0;
}

void GLESContext_Destroy(void) {}
int  GLESContext_MakeCurrent(void) { return 0; }
void GLESContext_Resize(int nWidth, int nHeight) { (void)nWidth; (void)nHeight; }
const SGLESContextInfo* GLESContext_GetInfo(void) { return 0; }
int  GLESContext_IsCreated(void) { return 0; }

#endif //__EMSCRIPTEN__
