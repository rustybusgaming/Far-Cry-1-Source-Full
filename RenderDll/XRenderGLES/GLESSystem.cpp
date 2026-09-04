////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESSystem.cpp
//  Description: The WebGL2 backend's system and context layer, plus the
//               module entry point.
//
//  See GLESRenderer.h for why this file is the one that has to exist: the
//  XRenderGLES target compiles every XRenderNULL source except NULL_System.cpp,
//  so these are exactly the methods that have no other definition and the link
//  will not complete without them.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESRenderer.h"
#include "GLESContext.h"

#include <time.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#endif

//////////////////////////////////////////////////////////////////////////

CGLESRenderer::CGLESRenderer()
	: m_nCanvasWidth(0)
	, m_nCanvasHeight(0)
	, m_bContextCreated(false)
	, m_b2DMode(false)
	, m_nDynVAO(0)
	, m_nDynVBO(0)
	, m_nDynIBO(0)
	, m_nDynVBOCapacity(0)
	, m_nDynIBOCapacity(0)
	, m_pDynPool(0)
	, m_nDynPoolVerts(0)
	, m_nDynPoolUsed(0)
{
	m_fClearColor[0] = m_fClearColor[1] = m_fClearColor[2] = 0.0f;
	m_fClearColor[3] = 1.0f;

	memset(m_matMVP, 0, sizeof(m_matMVP));
	m_matMVP[0] = m_matMVP[5] = m_matMVP[10] = m_matMVP[15] = 1.0f;
}

CGLESRenderer::~CGLESRenderer()
{
}

//////////////////////////////////////////////////////////////////////////
//! The engine's Init() signature is shaped by Win32: an HINSTANCE, an HWND, a
//! device context and a GL rendering context, any of which a Windows caller
//! might already have created. None of them exists here, and all are ignored.
//!
//! What the browser gives us instead is a canvas, named by CSS selector. The
//! canvas is fixed as "#canvas" because that is the element Emscripten's own
//! runtime creates and tracks; making it configurable is worth doing when
//! there is a reason to host more than one engine view on a page.
//////////////////////////////////////////////////////////////////////////
WIN_HWND CGLESRenderer::Init(int x, int y, int width, int height,
                             unsigned int cbpp, int zbpp, int sbits, bool fullscreen,
                             WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc,
                             WIN_HGLRC hGLrc, bool bReInit)
{
	m_nCanvasWidth  = width;
	m_nCanvasHeight = height;

	if (!GLESContext_Create("#canvas", width, height))
	{
		iLog->LogError("XRenderGLES: no WebGL2 context; renderer cannot start");
		return (WIN_HWND)0;
	}
	m_bContextCreated = true;

	const SGLESContextInfo* pInfo = GLESContext_GetInfo();
	if (pInfo)
	{
		iLog->Log("XRenderGLES: %s / %s", pInfo->szRenderer ? pInfo->szRenderer : "?",
		          pInfo->szVersion ? pInfo->szVersion : "?");

		// CRenderer publishes these to the rest of the engine; the texture
		// manager and shader system size their allocations from them, so they
		// have to be the real device limits rather than guesses.
		m_MaxTextureSize = pInfo->nMaxTextureSize;
		m_MaxTextureMemory = 0;		// WebGL does not expose VRAM, and no
									// portable query for it exists
	}

	m_width  = width;
	m_height = height;

	SetViewport(0, 0, width, height);
	PS2SetDefaultState();
	SetPolygonMode(R_SOLID_MODE);
	SetGamma(CV_r_gamma + m_fDeltaGamma);

	if (bReInit)
	{
		iLog->Log("Reload textures\n");
		RefreshResources(0);
	}

	iLog->Log("Init Shaders\n");

	gRenDev->m_cEF.mfInit();
	EF_PipelineInit();

	// The engine only ever tests this against NULL. There is no window.
	return (WIN_HWND)this;
}

//////////////////////////////////////////////////////////////////////////

void CGLESRenderer::ShutDown(bool bReInit)
{
	FreeResources(FRR_ALL);
	EF_PipelineShutdown();
	CName::mfExitSubsystem();

	// Before the context goes: the GL objects are only meaningful while it
	// lives, and deleting them afterwards would be operating on a dead context.
	ReleaseDrawResources();

	if (m_bContextCreated)
	{
		GLESContext_Destroy();
		m_bContextCreated = false;
	}
}

//////////////////////////////////////////////////////////////////////////
//! One canvas, one context. The engine's multi-context API exists for the
//! editor, which hosts several viewports in one process; there is no such
//! thing here yet, so the calls succeed and refer to the single context
//! rather than pretending to manage a set.
//////////////////////////////////////////////////////////////////////////
bool CGLESRenderer::SetCurrentContext(WIN_HWND hWnd)	{ return GLESContext_MakeCurrent() != 0; }
bool CGLESRenderer::CreateContext(WIN_HWND hWnd, bool bAllowFSAA)	{ return m_bContextCreated; }
bool CGLESRenderer::DeleteContext(WIN_HWND hWnd)		{ return true; }

//////////////////////////////////////////////////////////////////////////
//! Emscripten's WebGL contexts are per-thread, so this is a real operation
//! rather than a formality once anything renders off the main thread.
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::MakeCurrent()
{
	GLESContext_MakeCurrent();
}

void CGLESRenderer::ShareResources(IRenderer* renderer)
{
	// Sharing textures and buffers between contexts has no WebGL equivalent
	// at all -- there is no shared-context concept in the API. Nothing in the
	// game path calls this; the editor does.
}

void CGLESRenderer::RefreshResources(int nFlags)
{
	if (nFlags & FRO_TEXTURES)
		m_TexMan->ReloadAll(nFlags);
	if (nFlags & (FRO_SHADERS | FRO_SHADERTEXTURES))
		gRenDev->m_cEF.mfReloadAllShaders(nFlags);
}

//////////////////////////////////////////////////////////////////////////
//! There is no display-mode list in a browser: the page decides how large the
//! canvas is, and the engine cannot enumerate or choose. Returning none is
//! accurate, and the resolution menu correctly shows nothing to pick.
//////////////////////////////////////////////////////////////////////////
int CGLESRenderer::EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset)
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////
//! Resolution IS changeable, unlike the mode list -- the drawing buffer is
//! independent of the canvas's CSS size, so this resizes the buffer the engine
//! renders into and leaves the page's layout alone. Colour depth and refresh
//! rate have no meaning here and are ignored; fullscreen is the browser's
//! Fullscreen API and belongs to the page, not to the engine.
//////////////////////////////////////////////////////////////////////////
bool CGLESRenderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth,
                                     int nNewRefreshHZ, bool bFullScreen)
{
	if (!m_bContextCreated || nNewWidth <= 0 || nNewHeight <= 0)
		return false;

	GLESContext_Resize(nNewWidth, nNewHeight);

	m_nCanvasWidth  = nNewWidth;
	m_nCanvasHeight = nNewHeight;
	m_width  = nNewWidth;
	m_height = nNewHeight;

	SetViewport(0, 0, nNewWidth, nNewHeight);
	return true;
}

//////////////////////////////////////////////////////////////////////////
//! Gamma is a display-hardware ramp, and a web page cannot touch it -- for
//! good reason, since it is a whole-screen effect. The engine's gamma slider
//! therefore has nothing to drive here; when it matters it becomes a term in
//! the post-process shader rather than a device call.
//!
//! SetGammaDelta still records the value, because CRenderer reads m_fDeltaGamma
//! back and the console variable should not appear to be ignored.
//////////////////////////////////////////////////////////////////////////
bool CGLESRenderer::SetGammaDelta(const float fGamma)
{
	m_fDeltaGamma = fGamma;
	return true;
}

void CGLESRenderer::SetGamma(float fGamma)
{
}

//////////////////////////////////////////////////////////////////////////
//! Named for the PlayStation 2 backend that first needed it; every backend
//! implements it as "put the device in the state the engine assumes at the
//! start of a frame".
//////////////////////////////////////////////////////////////////////////
void CGLESRenderer::PS2SetDefaultState()
{
#if defined(__EMSCRIPTEN__)
	if (!m_bContextCreated)
		return;

	glDisable(GL_BLEND);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glClearColor(m_fClearColor[0], m_fClearColor[1], m_fClearColor[2], m_fClearColor[3]);
	glClearDepthf(1.0f);
	glClearStencil(0);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Module globals.
//
// These live in NULL_System.cpp for the null backend, and since this target
// does not compile that file they have to be here. They are the engine
// interfaces the renderer talks back through, filled in by
// PackageRenderConstructor below.
//////////////////////////////////////////////////////////////////////////

ILog            *iLog;
IConsole        *iConsole;
ITimer          *iTimer;
ISystem         *iSystem;
int             *pTest_int;
IPhysicalWorld  *pIPhysicalWorld;

#if !defined(_CRY_STATIC_MODULES)
// One link unit, one GetISystem() -- see CryCommon/StaticModules.h. In the
// single-link-unit build CrySystem owns the definition.
ISystem* GetISystem()
{
	return iSystem;
}
#endif // !_CRY_STATIC_MODULES

//! Escape hatches the engine uses to reach the underlying device on Windows.
//! There is no D3D device and no readable GL entry point to hand out here.
void *gGet_D3DDevice()      { return NULL; }
void *gGet_glReadPixels()   { return NULL; }

//////////////////////////////////////////////////////////////////////////

extern "C" DLL_EXPORT IRenderer* PackageRenderConstructor(int argc, char* argv[], SCryRenderInterface* sp);
DLL_EXPORT IRenderer* PackageRenderConstructor(int argc, char* argv[], SCryRenderInterface* sp)
{
	gbRgb = false;

	iConsole        = sp->ipConsole;
	iLog            = sp->ipLog;
	iTimer          = sp->ipTimer;
	iSystem         = sp->ipSystem;
	pTest_int       = sp->ipTest_int;
	pIPhysicalWorld = sp->pIPhysicalWorld;

#ifdef DEBUGALLOC
#undef new
#endif
	CRenderer* rd = (CRenderer*)(new CGLESRenderer());
#ifdef DEBUGALLOC
#define new DEBUG_CLIENTBLOCK
#endif

	srand(clock());

	return rd;
}
