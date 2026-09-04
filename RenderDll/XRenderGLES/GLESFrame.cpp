////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESFrame.cpp
//  Description: Frame lifetime and framebuffer operations -- the first entry
//               points in this backend that issue real GL.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESRenderer.h"
#include "GLESContext.h"

#include <stdlib.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#endif

//////////////////////////////////////////////////////////////////////////
//! There is no SwapBuffers here, and its absence is the point.
//!
//! In a browser the drawing buffer is presented when the task that drew it
//! yields to the event loop -- normally from a requestAnimationFrame callback.
//! Calling something at the end of a frame does not present it, and there is
//! nothing to call. So BeginFrame prepares the frame and Update ends it, and
//! presentation happens because control returns to the page.
//!
//! That is why the main loop, not this file, is the next real piece of work:
//! the engine's while(!quit) never returns to the event loop, so nothing is
//! ever presented. Headless does not enter that loop at all, which is exactly
//! why it can be tested today.
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::BeginFrame()
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	// A context can be lost at any time -- a GPU reset, a backgrounded tab,
	// a driver update -- and every GL call afterwards silently does nothing.
	// Checking once a frame is cheap next to discovering it via a black
	// screen.
	if (!GLESContext_MakeCurrent())
	{
		iLog->LogWarning("XRenderGLES: WebGL context is not current; frame skipped");
		return;
	}

	glClearColor(m_fClearColor[0], m_fClearColor[1], m_fClearColor[2], m_fClearColor[3]);
	glClearDepthf(1.0f);
	glClearStencil(0);
	glDepthMask(GL_TRUE);	// glClear ignores the depth buffer while the depth
							// mask is off, which is a classic way to lose the
							// clear without any error being reported
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#endif
}

void CGLESRenderer::Update()
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	// Flush rather than Finish. Finish would stall until the GPU is idle,
	// which on the web means a synchronous wait on the compositor; the
	// browser presents when we yield regardless.
	glFlush();
#endif
}

//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::SetViewport(int x, int y, int width, int height)
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	if (width <= 0 || height <= 0)
	{
		width  = m_width;
		height = m_height;
	}

	// GL's origin is bottom-left and the engine's is top-left, but the
	// engine's viewport calls all pass y=0 with a full-height rectangle, so
	// the two agree today. When sub-viewports arrive this needs flipping --
	// left as a note rather than a speculative flip that cannot be tested.
	glViewport(x, y, width, height);
#endif
}

void CGLESRenderer::SetScissor(int x, int y, int width, int height)
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	// The engine's convention: an all-zero rectangle means "no scissor".
	if (!width || !height)
	{
		glDisable(GL_SCISSOR_TEST);
		return;
	}

	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, width, height);
#endif
}

//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::ClearDepthBuffer()
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	glDepthMask(GL_TRUE);
	glClearDepthf(1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);
#endif
}

void CGLESRenderer::ClearColorBuffer(const Vec3d vColor)
{
	m_fClearColor[0] = vColor.x;
	m_fClearColor[1] = vColor.y;
	m_fClearColor[2] = vColor.z;
	m_fClearColor[3] = 1.0f;

#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	glClearColor(m_fClearColor[0], m_fClearColor[1], m_fClearColor[2], m_fClearColor[3]);
	glClear(GL_COLOR_BUFFER_BIT);
#endif
}

//////////////////////////////////////////////////////////////////////////
//! glGetError is a synchronous round trip into JavaScript here, far more
//! expensive than the register read it is on a desktop driver, so this is
//! deliberately not called per draw. It is left available for the places that
//! ask for it explicitly.
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::CheckError(const char* comment)
{
#if defined(__EMSCRIPTEN__)
	if (!GLESContext_IsCreated())
		return;

	GLenum err = glGetError();
	if (err == GL_NO_ERROR)
		return;

	const char* szName;
	switch (err)
	{
	case GL_INVALID_ENUM:					szName = "GL_INVALID_ENUM"; break;
	case GL_INVALID_VALUE:					szName = "GL_INVALID_VALUE"; break;
	case GL_INVALID_OPERATION:				szName = "GL_INVALID_OPERATION"; break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:	szName = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
	case GL_OUT_OF_MEMORY:					szName = "GL_OUT_OF_MEMORY"; break;
	default:								szName = "GL_<unknown>"; break;
	}

	iLog->LogError("XRenderGLES: %s at %s", szName, comment ? comment : "<no context>");
#endif
}

//////////////////////////////////////////////////////////////////////////
//! Read the framebuffer back into system memory.
//!
//! glReadPixels is a hard synchronisation point: it has to wait for every
//! queued draw to finish before it can answer, and on the web the answer also
//! crosses back out of the GPU process. It is fine for a screenshot and ruinous
//! per frame, which is why nothing in the frame path calls it.
//!
//! GLES 3.0 guarantees exactly one always-supported format/type combination for
//! reads -- GL_RGBA with GL_UNSIGNED_BYTE. GL_RGB is allowed to be rejected, so
//! an RGB request is read as RGBA and narrowed here rather than being handed
//! to the driver and hoping.
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::ReadFrameBuffer(unsigned char* pRGB, int nSizeX, int nSizeY,
                                    bool bBackBuffer, bool bRGBA,
                                    int nScaledX, int nScaledY)
{
#if defined(__EMSCRIPTEN__)
	if (!pRGB || nSizeX <= 0 || nSizeY <= 0 || !GLESContext_IsCreated())
		return;

	if (bRGBA)
	{
		glReadPixels(0, 0, nSizeX, nSizeY, GL_RGBA, GL_UNSIGNED_BYTE, pRGB);
		return;
	}

	unsigned char* pTemp = (unsigned char*)malloc((size_t)nSizeX * nSizeY * 4);
	if (!pTemp)
	{
		iLog->LogError("XRenderGLES: out of memory reading back %dx%d", nSizeX, nSizeY);
		return;
	}

	glReadPixels(0, 0, nSizeX, nSizeY, GL_RGBA, GL_UNSIGNED_BYTE, pTemp);

	for (int i = 0, n = nSizeX * nSizeY; i < n; ++i)
	{
		pRGB[i * 3 + 0] = pTemp[i * 4 + 0];
		pRGB[i * 3 + 1] = pTemp[i * 4 + 1];
		pRGB[i * 3 + 2] = pTemp[i * 4 + 2];
	}

	free(pTemp);
#endif
}
