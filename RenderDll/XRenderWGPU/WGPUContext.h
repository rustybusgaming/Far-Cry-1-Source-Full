#ifndef _CRY_WGPU_CONTEXT_H_
#define _CRY_WGPU_CONTEXT_H_

/*!
	WGPUContext -- acquiring a WebGPU device, and the ordering problem that
	comes with it.

	THE ASYNC PROBLEM

	This is the one thing that shapes the whole backend, so it is worth stating
	plainly.

	WebGL gives you a context synchronously: call
	emscripten_webgl_create_context and you either have one or you do not.
	WebGPU does not. Getting a device means requesting an adapter, waiting for a
	promise, requesting a device from that adapter, and waiting again. Neither
	wait can be turned into a blocking call, because the answer arrives on the
	browser's event loop -- the same loop the calling code would be blocking.

	CryEngine's IRenderer::Init() is synchronous and is expected to return a
	working renderer. There is no point in that call where a device can be
	obtained.

	So the device is NOT acquired by the renderer. The host acquires it before
	the engine exists: Web/WebMain.cpp calls WGPUContext_BeginInit(), returns to
	the event loop, and only calls CreateSystemInterface() once
	WGPUContext_IsReady() is true. By the time Init() runs, the device is
	already sitting here waiting for it, and Init() is an ordinary synchronous
	function again.

	That is the same shape as the main-loop inversion: the engine's structure is
	kept, and the part that cannot be synchronous is moved to the one place that
	can wait -- the host, before startup.

	Asyncify would also solve it, by letting Init() block while the promise
	resolves. It is rejected here for the same reason as before: it rewrites the
	whole module to avoid restructuring a few lines of host code.

	WHY THIS IS NOT VERIFIED HERE

	The container this was written in cannot produce a WebGPU adapter --
	navigator.gpu exists but requestAdapter() returns null under headless
	Chromium, headless=new, and a real X display, with SwiftShader's Vulkan ICD
	explicitly configured. The code compiles and links against Dawn, and it is
	written from the current API rather than from memory, but it has not drawn a
	pixel. XRenderGLES remains the verified backend and the control to compare
	against.
*/

#ifdef __cplusplus
extern "C" {
#endif

//! How far along device acquisition is.
enum EWGPUContextState
{
	eWGPUContext_Idle = 0,	//!< BeginInit has not been called
	eWGPUContext_Pending,	//!< waiting on the adapter or the device
	eWGPUContext_Ready,		//!< device and queue are usable
	eWGPUContext_Failed,	//!< no adapter, no device, or no surface
};

//! What the device turned out to support. Queried once, because in WebGPU
//! limits are a property of the device rather than something to ask per frame.
struct SWGPUContextInfo
{
	unsigned int	nMaxTextureDimension2D;
	unsigned int	nMaxBindGroups;
	unsigned int	nMaxVertexAttributes;
	unsigned int	nMaxColorAttachments;
	int				bHasBC;			//!< texture-compression-bc: the DXT formats
									//!< Far Cry's textures are stored in
	const char*		szAdapter;		//!< human-readable adapter description
};

//! Start acquiring an adapter and device. Returns immediately; the result
//! arrives on the event loop. Safe to call more than once -- later calls are
//! ignored while a request is outstanding.
void WGPUContext_BeginInit(const char* szCanvasSelector, int nWidth, int nHeight);

//! Poll from the host's pre-engine loop.
enum EWGPUContextState WGPUContext_GetState(void);

//! Non-zero once the device is usable. Shorthand for the state above.
int WGPUContext_IsReady(void);

//! Capabilities, or 0 before the device is ready.
const struct SWGPUContextInfo* WGPUContext_GetInfo(void);

//! Reconfigure the surface. In WebGPU the swap chain is the surface's
//! configuration rather than a separate object, so a resize is a reconfigure.
void WGPUContext_Resize(int nWidth, int nHeight);

void WGPUContext_Destroy(void);

#ifdef __cplusplus
}
#endif

#endif //_CRY_WGPU_CONTEXT_H_
