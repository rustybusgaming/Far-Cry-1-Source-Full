////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUContext.cpp
//  Description: WebGPU device acquisition. See WGPUContext.h for why this is
//               driven by the host rather than by the renderer.
//
////////////////////////////////////////////////////////////////////////////

#include "WGPUContext.h"

#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>

static WGPUInstance			g_instance = 0;
static WGPUAdapter			g_adapter  = 0;
static WGPUDevice			g_device   = 0;
static WGPUQueue			g_queue    = 0;
static WGPUSurface			g_surface  = 0;
static WGPUTextureFormat	g_format   = WGPUTextureFormat_Undefined;

static EWGPUContextState	g_state = eWGPUContext_Idle;
static SWGPUContextInfo		g_info;
static char					g_szAdapter[256];
static char					g_szCanvas[128];
static int					g_nWidth  = 0;
static int					g_nHeight = 0;

//////////////////////////////////////////////////////////////////////////
//! WGPUStringView is a pointer and a length, and is NOT guaranteed to be
//! null-terminated -- printing it with %s would run off the end. Dawn's error
//! messages arrive this way, and they are the only diagnostic there is when a
//! device request fails, so they are worth copying correctly.
//////////////////////////////////////////////////////////////////////////
static void CopyStringView(WGPUStringView sv, char* szOut, size_t nOutSize)
{
	if (!szOut || nOutSize == 0)
		return;

	if (!sv.data || sv.length == 0)
	{
		szOut[0] = 0;
		return;
	}

	size_t n = sv.length;
	if (n >= nOutSize)
		n = nOutSize - 1;

	memcpy(szOut, sv.data, n);
	szOut[n] = 0;
}

//////////////////////////////////////////////////////////////////////////

static void OnDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                         WGPUStringView message, void* ud1, void* ud2)
{
	char szMsg[512];
	CopyStringView(message, szMsg, sizeof(szMsg));

	// A lost device is not recoverable by retrying: every object made from it
	// is invalid too. Saying so loudly beats a frame loop that silently stops
	// drawing.
	printf("[wgpu] DEVICE LOST (reason %d): %s\n", (int)reason, szMsg);
	g_state = eWGPUContext_Failed;
}

static void OnUncapturedError(WGPUDevice const* device, WGPUErrorType type,
                              WGPUStringView message, void* ud1, void* ud2)
{
	char szMsg[512];
	CopyStringView(message, szMsg, sizeof(szMsg));

	// WebGPU validates everything and reports here rather than by failing the
	// call, so this is where a wrong pipeline or bind group actually shows up.
	printf("[wgpu] error (type %d): %s\n", (int)type, szMsg);
}

//////////////////////////////////////////////////////////////////////////

static void ConfigureSurface(void)
{
	if (!g_surface || !g_device || g_nWidth <= 0 || g_nHeight <= 0)
		return;

	WGPUSurfaceConfiguration config;
	memset(&config, 0, sizeof(config));

	config.device      = g_device;
	config.format      = g_format;
	config.usage       = WGPUTextureUsage_RenderAttachment;
	config.width       = (uint32_t)g_nWidth;
	config.height      = (uint32_t)g_nHeight;
	config.alphaMode   = WGPUCompositeAlphaMode_Opaque;

	// Fifo is the only mode every implementation must support, and it is what
	// the browser does anyway -- presentation is tied to the page's frame
	// callback, so asking for anything else would not change when frames appear.
	config.presentMode = WGPUPresentMode_Fifo;

	wgpuSurfaceConfigure(g_surface, &config);
}

//////////////////////////////////////////////////////////////////////////

static void QueryInfo(void)
{
	memset(&g_info, 0, sizeof(g_info));
	g_szAdapter[0] = 0;

	WGPULimits limits;
	memset(&limits, 0, sizeof(limits));
	if (wgpuDeviceGetLimits(g_device, &limits) == WGPUStatus_Success)
	{
		g_info.nMaxTextureDimension2D = limits.maxTextureDimension2D;
		g_info.nMaxBindGroups         = limits.maxBindGroups;
		g_info.nMaxVertexAttributes   = limits.maxVertexAttributes;
		g_info.nMaxColorAttachments   = limits.maxColorAttachments;
	}

	// The DXT formats. Far Cry's textures are almost entirely DXT1/3/5, so
	// without this every one of them needs decompressing on the CPU -- the same
	// question S3TC answers for the WebGL backend.
	g_info.bHasBC = wgpuAdapterHasFeature(g_adapter, WGPUFeatureName_TextureCompressionBC) ? 1 : 0;

	WGPUAdapterInfo ai;
	memset(&ai, 0, sizeof(ai));
	if (wgpuAdapterGetInfo(g_adapter, &ai) == WGPUStatus_Success)
	{
		char szVendor[96], szArch[96], szDesc[128];
		CopyStringView(ai.vendor,      szVendor, sizeof(szVendor));
		CopyStringView(ai.architecture, szArch,  sizeof(szArch));
		CopyStringView(ai.description, szDesc,   sizeof(szDesc));

		snprintf(g_szAdapter, sizeof(g_szAdapter), "%s %s %s",
		         szVendor[0] ? szVendor : "?",
		         szArch[0]   ? szArch   : "",
		         szDesc[0]   ? szDesc   : "");
	}
	g_info.szAdapter = g_szAdapter;
}

//////////////////////////////////////////////////////////////////////////

static void OnDeviceReceived(WGPURequestDeviceStatus status, WGPUDevice device,
                             WGPUStringView message, void* ud1, void* ud2)
{
	if (status != WGPURequestDeviceStatus_Success || !device)
	{
		char szMsg[512];
		CopyStringView(message, szMsg, sizeof(szMsg));
		printf("[wgpu] no device: %s\n", szMsg);
		g_state = eWGPUContext_Failed;
		return;
	}

	g_device = device;
	g_queue  = wgpuDeviceGetQueue(g_device);

	// The surface is created from the canvas the same way a WebGL context is
	// attached to one, except that in WebGPU the surface and the device are
	// independent objects joined by Configure().
	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc;
	memset(&canvasDesc, 0, sizeof(canvasDesc));
	canvasDesc.chain.sType   = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	canvasDesc.selector.data   = g_szCanvas;
	canvasDesc.selector.length = strlen(g_szCanvas);

	WGPUSurfaceDescriptor surfDesc;
	memset(&surfDesc, 0, sizeof(surfDesc));
	surfDesc.nextInChain = (WGPUChainedStruct*)&canvasDesc;

	g_surface = wgpuInstanceCreateSurface(g_instance, &surfDesc);
	if (!g_surface)
	{
		printf("[wgpu] could not create a surface on '%s'\n", g_szCanvas);
		g_state = eWGPUContext_Failed;
		return;
	}

	// Ask the surface which format it prefers rather than assuming BGRA8. The
	// preferred format differs between platforms, and using the wrong one costs
	// a conversion on every present.
	WGPUSurfaceCapabilities caps;
	memset(&caps, 0, sizeof(caps));
	if (wgpuSurfaceGetCapabilities(g_surface, g_adapter, &caps) == WGPUStatus_Success
	    && caps.formatCount > 0)
	{
		g_format = caps.formats[0];
		wgpuSurfaceCapabilitiesFreeMembers(caps);
	}
	else
	{
		g_format = WGPUTextureFormat_BGRA8Unorm;
	}

	ConfigureSurface();
	QueryInfo();

	g_state = eWGPUContext_Ready;

	printf("[wgpu] device ready on '%s', %dx%d\n", g_szCanvas, g_nWidth, g_nHeight);
	printf("[wgpu]   adapter : %s\n", g_info.szAdapter);
	printf("[wgpu]   max tex %u, bind groups %u, attribs %u, colour attachments %u\n",
	       g_info.nMaxTextureDimension2D, g_info.nMaxBindGroups,
	       g_info.nMaxVertexAttributes, g_info.nMaxColorAttachments);
	printf("[wgpu]   BC (DXT) textures %s\n", g_info.bHasBC ? "yes" : "no");

	if (!g_info.bHasBC)
		printf("[wgpu]   WARNING: no BC support; DXT textures will need CPU decompression\n");
}

static void OnAdapterReceived(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                              WGPUStringView message, void* ud1, void* ud2)
{
	if (status != WGPURequestAdapterStatus_Success || !adapter)
	{
		char szMsg[512];
		CopyStringView(message, szMsg, sizeof(szMsg));
		printf("[wgpu] no adapter: %s\n", szMsg);
		g_state = eWGPUContext_Failed;
		return;
	}

	g_adapter = adapter;

	// Request BC if the adapter has it. A feature not asked for is not
	// available on the device even when the adapter supports it, so this is
	// what decides whether DXT uploads work later.
	WGPUFeatureName features[1];
	size_t nFeatures = 0;
	if (wgpuAdapterHasFeature(g_adapter, WGPUFeatureName_TextureCompressionBC))
		features[nFeatures++] = WGPUFeatureName_TextureCompressionBC;

	WGPUDeviceDescriptor desc;
	memset(&desc, 0, sizeof(desc));
	desc.requiredFeatureCount = nFeatures;
	desc.requiredFeatures     = nFeatures ? features : 0;

	desc.deviceLostCallbackInfo.mode     = WGPUCallbackMode_AllowSpontaneous;
	desc.deviceLostCallbackInfo.callback = OnDeviceLost;

	desc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;

	WGPURequestDeviceCallbackInfo cb;
	memset(&cb, 0, sizeof(cb));
	cb.mode     = WGPUCallbackMode_AllowSpontaneous;
	cb.callback = OnDeviceReceived;

	wgpuAdapterRequestDevice(g_adapter, &desc, cb);
}

//////////////////////////////////////////////////////////////////////////

void WGPUContext_BeginInit(const char* szCanvasSelector, int nWidth, int nHeight)
{
	if (g_state == eWGPUContext_Pending || g_state == eWGPUContext_Ready)
		return;

	if (!szCanvasSelector)
		szCanvasSelector = "#canvas";

	strncpy(g_szCanvas, szCanvasSelector, sizeof(g_szCanvas) - 1);
	g_szCanvas[sizeof(g_szCanvas) - 1] = 0;

	g_nWidth  = nWidth;
	g_nHeight = nHeight;

	// The canvas needs its backing size set before the surface is configured,
	// or the first frame is presented at whatever size the element happened to
	// have.
	emscripten_set_canvas_element_size(g_szCanvas, nWidth, nHeight);

	if (!g_instance)
	{
		WGPUInstanceDescriptor desc;
		memset(&desc, 0, sizeof(desc));
		g_instance = wgpuCreateInstance(&desc);
	}

	if (!g_instance)
	{
		printf("[wgpu] no WebGPU instance; this browser does not support WebGPU\n");
		g_state = eWGPUContext_Failed;
		return;
	}

	g_state = eWGPUContext_Pending;

	WGPURequestAdapterCallbackInfo cb;
	memset(&cb, 0, sizeof(cb));
	cb.mode     = WGPUCallbackMode_AllowSpontaneous;
	cb.callback = OnAdapterReceived;

	wgpuInstanceRequestAdapter(g_instance, 0, cb);
}

EWGPUContextState WGPUContext_GetState(void)	{ return g_state; }
int WGPUContext_IsReady(void)					{ return g_state == eWGPUContext_Ready; }

const SWGPUContextInfo* WGPUContext_GetInfo(void)
{
	return (g_state == eWGPUContext_Ready) ? &g_info : 0;
}

void WGPUContext_Resize(int nWidth, int nHeight)
{
	if (nWidth <= 0 || nHeight <= 0)
		return;

	g_nWidth  = nWidth;
	g_nHeight = nHeight;

	emscripten_set_canvas_element_size(g_szCanvas, nWidth, nHeight);
	ConfigureSurface();
}

void WGPUContext_Destroy(void)
{
	if (g_surface)	{ wgpuSurfaceRelease(g_surface); g_surface = 0; }
	if (g_queue)	{ wgpuQueueRelease(g_queue);     g_queue   = 0; }
	if (g_device)	{ wgpuDeviceRelease(g_device);   g_device  = 0; }
	if (g_adapter)	{ wgpuAdapterRelease(g_adapter); g_adapter = 0; }
	if (g_instance)	{ wgpuInstanceRelease(g_instance); g_instance = 0; }

	g_state  = eWGPUContext_Idle;
	g_format = WGPUTextureFormat_Undefined;
}

//! Accessors for the rest of the backend. Declared here rather than in the
//! header so the header stays free of webgpu.h -- only the files that actually
//! issue GPU work need it.
WGPUDevice			WGPUContext_Device()	{ return g_device; }
WGPUQueue			WGPUContext_Queue()		{ return g_queue; }
WGPUSurface			WGPUContext_Surface()	{ return g_surface; }
WGPUTextureFormat	WGPUContext_Format()	{ return g_format; }

#else //__EMSCRIPTEN__

//////////////////////////////////////////////////////////////////////////
// Native builds have no WebGPU. See GLESContext.cpp for the same reasoning:
// this backend exists for the browser, and Dawn's native library is not
// vendored here.
//////////////////////////////////////////////////////////////////////////

void WGPUContext_BeginInit(const char* szCanvasSelector, int nWidth, int nHeight)
{
	(void)szCanvasSelector; (void)nWidth; (void)nHeight;
	printf("[wgpu] WebGPU is unavailable on this target (built without Emscripten)\n");
}

EWGPUContextState WGPUContext_GetState(void)		{ return eWGPUContext_Failed; }
int WGPUContext_IsReady(void)						{ return 0; }
const SWGPUContextInfo* WGPUContext_GetInfo(void)	{ return 0; }
void WGPUContext_Resize(int nWidth, int nHeight)	{ (void)nWidth; (void)nHeight; }
void WGPUContext_Destroy(void)						{}

#endif //__EMSCRIPTEN__
