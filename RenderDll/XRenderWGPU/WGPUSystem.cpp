////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUSystem.cpp
//  Description: The WebGPU backend's system layer and module entry point.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "WGPURenderer.h"
#include "WGPUContext.h"

#include <time.h>

//////////////////////////////////////////////////////////////////////////

CWGPURenderer::CWGPURenderer()
	: m_nCanvasWidth(0)
	, m_nCanvasHeight(0)
	, m_bDeviceReady(false)
{
	m_fClearColor[0] = m_fClearColor[1] = m_fClearColor[2] = 0.0f;
	m_fClearColor[3] = 1.0f;
}

CWGPURenderer::~CWGPURenderer()
{
}

//////////////////////////////////////////////////////////////////////////
//! Init does NOT acquire the device -- it collects one the host already
//! obtained. See WGPUContext.h: device acquisition is asynchronous and this
//! function is not, so the ordering is inverted and the host waits before the
//! engine is created at all.
//!
//! Reaching here without a ready device is therefore a host bug rather than a
//! device problem, and it is reported as one.
//////////////////////////////////////////////////////////////////////////
WIN_HWND CWGPURenderer::Init(int x, int y, int width, int height,
                             unsigned int cbpp, int zbpp, int sbits, bool fullscreen,
                             WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc,
                             WIN_HGLRC hGLrc, bool bReInit)
{
	m_nCanvasWidth  = width;
	m_nCanvasHeight = height;

	if (!WGPUContext_IsReady())
	{
		iLog->LogError("XRenderWGPU: no WebGPU device at Init. The host must call "
		               "WGPUContext_BeginInit() and wait for WGPUContext_IsReady() "
		               "before CreateSystemInterface()");
		return (WIN_HWND)0;
	}

	m_bDeviceReady = true;

	const SWGPUContextInfo* pInfo = WGPUContext_GetInfo();
	if (pInfo)
	{
		iLog->Log("XRenderWGPU: %s", pInfo->szAdapter ? pInfo->szAdapter : "?");

		m_MaxTextureSize   = (int)pInfo->nMaxTextureDimension2D;
		m_MaxTextureMemory = 0;		// WebGPU exposes no VRAM figure, as WebGL does not
	}

	m_width  = width;
	m_height = height;

	WGPUContext_Resize(width, height);
	SetViewport(0, 0, width, height);

	if (bReInit)
	{
		iLog->Log("Reload textures\n");
		RefreshResources(0);
	}

	iLog->Log("Init Shaders\n");

	gRenDev->m_cEF.mfInit();
	EF_PipelineInit();

	return (WIN_HWND)this;
}

void CWGPURenderer::ShutDown(bool bReInit)
{
	FreeResources(FRR_ALL);
	EF_PipelineShutdown();
	CName::mfExitSubsystem();

	// The host owns the device's lifetime, since the host created it. Tearing
	// it down here would break a re-init that reuses it.
	m_bDeviceReady = false;
}

//////////////////////////////////////////////////////////////////////////
//! WebGPU has no "current context" -- a device is not bound to a thread and
//! commands carry their own device. These exist because IRenderer requires
//! them.
//////////////////////////////////////////////////////////////////////////
bool CWGPURenderer::SetCurrentContext(WIN_HWND hWnd)	{ return m_bDeviceReady; }
bool CWGPURenderer::CreateContext(WIN_HWND hWnd, bool bAllowFSAA)	{ return m_bDeviceReady; }
bool CWGPURenderer::DeleteContext(WIN_HWND hWnd)		{ return true; }
void CWGPURenderer::MakeCurrent()						{}

void CWGPURenderer::ShareResources(IRenderer* renderer)
{
	// Resources belong to a device, and there is one device.
}

void CWGPURenderer::RefreshResources(int nFlags)
{
	if (nFlags & FRO_TEXTURES)
		m_TexMan->ReloadAll(nFlags);
	if (nFlags & (FRO_SHADERS | FRO_SHADERTEXTURES))
		gRenDev->m_cEF.mfReloadAllShaders(nFlags);
}

//! No display-mode list in a browser; the page decides the canvas size.
int CWGPURenderer::EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset)
{
	return 0;
}

bool CWGPURenderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth,
                                     int nNewRefreshHZ, bool bFullScreen)
{
	if (!m_bDeviceReady || nNewWidth <= 0 || nNewHeight <= 0)
		return false;

	// A resize is a surface reconfigure in WebGPU -- there is no separate swap
	// chain object to recreate.
	WGPUContext_Resize(nNewWidth, nNewHeight);

	m_nCanvasWidth  = nNewWidth;
	m_nCanvasHeight = nNewHeight;
	m_width  = nNewWidth;
	m_height = nNewHeight;

	SetViewport(0, 0, nNewWidth, nNewHeight);
	return true;
}

//! Gamma is a display ramp and a page cannot touch it; see the GLES backend.
bool CWGPURenderer::SetGammaDelta(const float fGamma)
{
	m_fDeltaGamma = fGamma;
	return true;
}

void CWGPURenderer::SetGamma(float fGamma) {}

//! There is no default device state to establish: in WebGPU every piece of
//! what GL calls state lives in a pipeline object instead, decided when the
//! pipeline is created rather than set beforehand.
void CWGPURenderer::PS2SetDefaultState() {}

//////////////////////////////////////////////////////////////////////////
// Module globals and entry point. Same arrangement as XRenderGLES: this
// target compiles NULL_System.cpp with CRY_BACKEND_OWNS_MODULE so its module tail is
// removed, and supplies these instead.
//////////////////////////////////////////////////////////////////////////

ILog            *iLog;
IConsole        *iConsole;
ITimer          *iTimer;
ISystem         *iSystem;
int             *pTest_int;
IPhysicalWorld  *pIPhysicalWorld;

#if !defined(_CRY_STATIC_MODULES)
ISystem* GetISystem()
{
	return iSystem;
}
#endif

void *gGet_D3DDevice()      { return NULL; }
void *gGet_glReadPixels()   { return NULL; }

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
	CRenderer* rd = (CRenderer*)(new CWGPURenderer());
#ifdef DEBUGALLOC
#define new DEBUG_CLIENTBLOCK
#endif

	srand(clock());

	return rd;
}
