////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_glescontext.cpp
//  Description: Browser test for the WebGL2 context the renderer creates.
//
//  This is the one part of the renderer that cannot be tested anywhere else.
//  Every other backend entry point is engine code that happens to call GL and
//  can be reasoned about from the source; context creation is a negotiation
//  with the browser, and whether it succeeds depends on the page, the GPU, the
//  driver blocklist and the browser's own policy. The only way to know is to
//  ask a real browser.
//
//  It links GLESContext.cpp itself -- the same file the renderer uses, not a
//  copy -- and then does what CGLESRenderer::BeginFrame and ClearColorBuffer
//  do, so a regression in either shows up here.
//
//  Results are written to the page title and to a global the harness reads,
//  because printf from wasm reaches the console but not the test runner.
//
////////////////////////////////////////////////////////////////////////////

#include "GLESContext.h"

#include <emscripten/emscripten.h>
#include <GLES3/gl3.h>

#include <stdio.h>
#include <string.h>

static int g_nFailures = 0;

#define CHECK(cond, msg) \
	do { \
		if (!(cond)) { \
			printf("FAIL: %s\n", msg); \
			++g_nFailures; \
		} else { \
			printf("ok  : %s\n", msg); \
		} \
	} while (0)

int main()
{
	//////////////////////////////////////////////////////////////////////
	// Creation.
	//////////////////////////////////////////////////////////////////////
	CHECK(!GLESContext_IsCreated(), "no context before creation");

	const int nWidth  = 320;
	const int nHeight = 240;

	CHECK(GLESContext_Create("#canvas", nWidth, nHeight) != 0, "WebGL2 context created");
	CHECK(GLESContext_IsCreated() != 0, "context reports itself created");
	CHECK(GLESContext_MakeCurrent() != 0, "context can be made current");

	//////////////////////////////////////////////////////////////////////
	// Capabilities. These are the numbers CGLESRenderer::Init copies into
	// CRenderer for the texture manager to size itself from, so a zero here
	// would quietly mis-size allocations rather than fail.
	//////////////////////////////////////////////////////////////////////
	const SGLESContextInfo* pInfo = GLESContext_GetInfo();
	CHECK(pInfo != 0, "capabilities available");
	if (pInfo)
	{
		CHECK(pInfo->nMaxTextureSize >= 2048, "max texture size >= 2048");
		CHECK(pInfo->nMaxCubeTextureSize >= 2048, "max cube map size >= 2048");
		CHECK(pInfo->nMaxTextureUnits >= 8, "at least 8 texture units");
		CHECK(pInfo->nMaxVertexAttribs >= 16, "at least 16 vertex attributes");

		// This is the one that proves it is WebGL2 rather than WebGL1 with a
		// cooperative extension: WebGL1 has exactly one colour attachment.
		CHECK(pInfo->nMaxColorAttachments >= 4, "MRT available (WebGL2, not WebGL1)");

		printf("info: %s / %s\n",
		       pInfo->szRenderer ? pInfo->szRenderer : "?",
		       pInfo->szVersion  ? pInfo->szVersion  : "?");

		// GLES 3.0 is what WebGL2 reports; assert the string rather than trust
		// the attribute request.
		CHECK(pInfo->szVersion && strstr(pInfo->szVersion, "OpenGL ES 3.0") != 0,
		      "context reports OpenGL ES 3.0");
	}

	//////////////////////////////////////////////////////////////////////
	// A clear, read back. This is exactly what the renderer does in
	// BeginFrame/ClearColorBuffer, and reading the pixel back is the only
	// way to know the clear reached the drawing buffer rather than being
	// dropped -- a wrong depth mask or a lost context loses it silently.
	//////////////////////////////////////////////////////////////////////
	glViewport(0, 0, nWidth, nHeight);

	// A colour with a distinct value in every channel, so a channel-order
	// mistake cannot pass.
	glClearColor(0.25f, 0.50f, 0.75f, 1.0f);
	glClearDepthf(1.0f);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	CHECK(glGetError() == GL_NO_ERROR, "clear produced no GL error");

	unsigned char px[4] = { 0, 0, 0, 0 };
	glReadPixels(nWidth / 2, nHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(glGetError() == GL_NO_ERROR, "readback produced no GL error");

	printf("info: centre pixel = %d,%d,%d,%d\n", px[0], px[1], px[2], px[3]);

	// 0.25/0.50/0.75 in 8-bit are 64/128/191. Allow one step for the
	// float-to-unorm rounding the spec permits.
	CHECK(px[0] >= 63 && px[0] <= 65, "red channel cleared to 0.25");
	CHECK(px[1] >= 127 && px[1] <= 129, "green channel cleared to 0.50");
	CHECK(px[2] >= 190 && px[2] <= 192, "blue channel cleared to 0.75");

	//////////////////////////////////////////////////////////////////////
	// Resize. ChangeResolution goes through this, and the drawing buffer
	// must actually change size -- the canvas CSS size is independent.
	//////////////////////////////////////////////////////////////////////
	GLESContext_Resize(640, 480);
	GLint dims[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_VIEWPORT, dims);
	CHECK(dims[2] == 640 && dims[3] == 480, "resize updated the viewport");

	//////////////////////////////////////////////////////////////////////
	// Teardown, and that it is safe twice -- ShutDown can be reached by more
	// than one path when startup fails partway.
	//////////////////////////////////////////////////////////////////////
	GLESContext_Destroy();
	CHECK(!GLESContext_IsCreated(), "context destroyed");
	GLESContext_Destroy();
	CHECK(!GLESContext_IsCreated(), "destroying twice is safe");
	CHECK(GLESContext_GetInfo() == 0, "capabilities gone with the context");

	//////////////////////////////////////////////////////////////////////
	// Publish the verdict where the harness can see it.
	//////////////////////////////////////////////////////////////////////
	if (g_nFailures)
		printf("\n%d failure(s)\n", g_nFailures);
	else
		printf("\nglescontext: all tests passed\n");

	EM_ASM({
		window.__cryTestFailures = $0;
		window.__cryTestDone = true;
		document.title = $0 ? ('FAIL ' + $0) : 'PASS';
	}, g_nFailures);

	return g_nFailures ? 1 : 0;
}
