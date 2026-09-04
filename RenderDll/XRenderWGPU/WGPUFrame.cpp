////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUFrame.cpp
//  Description: Frame lifetime for the WebGPU backend.
//
//  WHY A FRAME LOOKS DIFFERENT HERE
//
//  In GL a frame is a sequence of immediate calls, and a clear is one of them.
//  In WebGPU nothing is immediate: commands are recorded into an encoder,
//  grouped into a render pass, finished into a command buffer and submitted to
//  a queue. Only submission does anything.
//
//  The consequence that matters for this engine is that CLEARING IS NOT A CALL.
//  It is the loadOp of a render pass attachment, decided when the pass begins.
//  So the clear colour has to be known at BeginFrame, and ClearColorBuffer --
//  which the engine may call at any point -- can only record the colour for the
//  next pass rather than clear immediately. Where the engine really means
//  "clear now, mid-frame", that has to become a new render pass, which is why
//  the pass is kept open on the renderer rather than being local to BeginFrame.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "WGPURenderer.h"
#include "WGPUContext.h"

#if defined(__EMSCRIPTEN__)

#include <webgpu/webgpu.h>

// Declared in WGPUContext.cpp; kept out of the header so webgpu.h only reaches
// the files that issue GPU work.
extern WGPUDevice        WGPUContext_Device();
extern WGPUQueue         WGPUContext_Queue();
extern WGPUSurface       WGPUContext_Surface();
extern WGPUTextureFormat WGPUContext_Format();

//! Per-frame objects. Held here rather than on the renderer so the header does
//! not need webgpu.h; there is exactly one frame in flight from the engine's
//! point of view.
static WGPUCommandEncoder	g_encoder = 0;
static WGPURenderPassEncoder g_pass   = 0;
static WGPUTexture			g_backbuffer = 0;
static WGPUTextureView		g_backbufferView = 0;

#endif //__EMSCRIPTEN__

//////////////////////////////////////////////////////////////////////////

void CWGPURenderer::BeginFrame()
{
#if defined(__EMSCRIPTEN__)
	if (!m_bDeviceReady)
		return;

	WGPUDevice device = WGPUContext_Device();
	WGPUSurface surface = WGPUContext_Surface();
	if (!device || !surface)
		return;

	// The backbuffer is acquired per frame and is only valid for this frame.
	// Unlike GL's default framebuffer it is not a persistent object.
	WGPUSurfaceTexture surfaceTexture;
	memset(&surfaceTexture, 0, sizeof(surfaceTexture));
	wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);

	if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
	    surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
	{
		// Suboptimal is fine to draw into; anything else means the surface needs
		// reconfiguring, which happens on the next resize.
		iLog->LogWarning("XRenderWGPU: no backbuffer this frame (status %d)",
		                 (int)surfaceTexture.status);
		return;
	}

	g_backbuffer = surfaceTexture.texture;
	g_backbufferView = wgpuTextureCreateView(g_backbuffer, 0);

	WGPUCommandEncoderDescriptor encDesc;
	memset(&encDesc, 0, sizeof(encDesc));
	g_encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

	// The clear happens here, as the pass's load operation, because that is the
	// only place WebGPU allows it.
	WGPURenderPassColorAttachment colorAttachment;
	memset(&colorAttachment, 0, sizeof(colorAttachment));
	colorAttachment.view       = g_backbufferView;
	colorAttachment.loadOp     = WGPULoadOp_Clear;
	colorAttachment.storeOp    = WGPUStoreOp_Store;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	colorAttachment.clearValue.r = m_fClearColor[0];
	colorAttachment.clearValue.g = m_fClearColor[1];
	colorAttachment.clearValue.b = m_fClearColor[2];
	colorAttachment.clearValue.a = m_fClearColor[3];

	WGPURenderPassDescriptor passDesc;
	memset(&passDesc, 0, sizeof(passDesc));
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments     = &colorAttachment;

	// No depth attachment yet: there is no depth texture until geometry needs
	// one, and attaching a depth buffer nothing writes to would only cost
	// bandwidth.
	g_pass = wgpuCommandEncoderBeginRenderPass(g_encoder, &passDesc);
#endif
}

void CWGPURenderer::Update()
{
#if defined(__EMSCRIPTEN__)
	if (!g_encoder)
		return;

	if (g_pass)
	{
		wgpuRenderPassEncoderEnd(g_pass);
		wgpuRenderPassEncoderRelease(g_pass);
		g_pass = 0;
	}

	WGPUCommandBufferDescriptor cbDesc;
	memset(&cbDesc, 0, sizeof(cbDesc));
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(g_encoder, &cbDesc);

	wgpuQueueSubmit(WGPUContext_Queue(), 1, &commands);

	wgpuCommandBufferRelease(commands);
	wgpuCommandEncoderRelease(g_encoder);
	g_encoder = 0;

	if (g_backbufferView)	{ wgpuTextureViewRelease(g_backbufferView); g_backbufferView = 0; }
	if (g_backbuffer)		{ wgpuTextureRelease(g_backbuffer); g_backbuffer = 0; }

	// No present call. As with WebGL, the browser presents when the drawing
	// task yields to the event loop.
#endif
}

//////////////////////////////////////////////////////////////////////////

void CWGPURenderer::SetViewport(int x, int y, int width, int height)
{
#if defined(__EMSCRIPTEN__)
	if (!g_pass)
		return;	// viewport is a render-pass command; outside a pass it has
				// nowhere to go, and the engine sets it during Init

	if (width <= 0 || height <= 0)
	{
		width  = m_width;
		height = m_height;
	}

	wgpuRenderPassEncoderSetViewport(g_pass, (float)x, (float)y,
	                                 (float)width, (float)height, 0.0f, 1.0f);
#endif
}

void CWGPURenderer::SetScissor(int x, int y, int width, int height)
{
#if defined(__EMSCRIPTEN__)
	if (!g_pass)
		return;

	// The engine's convention is that an all-zero rectangle means no scissor.
	// WebGPU has no way to disable one, so "no scissor" is the whole target.
	if (!width || !height)
	{
		x = y = 0;
		width  = m_width;
		height = m_height;
	}

	wgpuRenderPassEncoderSetScissorRect(g_pass, (uint32_t)x, (uint32_t)y,
	                                    (uint32_t)width, (uint32_t)height);
#endif
}

//////////////////////////////////////////////////////////////////////////
//! Records the colour for the next pass. See the header comment: a clear
//! cannot be issued mid-pass in WebGPU, so a call arriving after BeginFrame
//! affects the following frame rather than this one. That is a real behavioural
//! difference from the GL backend and not a rounding error -- if the engine
//! turns out to depend on mid-frame clears, this has to become a new pass.
//////////////////////////////////////////////////////////////////////////
void CWGPURenderer::ClearColorBuffer(const Vec3d vColor)
{
	m_fClearColor[0] = vColor.x;
	m_fClearColor[1] = vColor.y;
	m_fClearColor[2] = vColor.z;
	m_fClearColor[3] = 1.0f;
}

void CWGPURenderer::ClearDepthBuffer()
{
	// Likewise a load operation, and there is no depth attachment yet.
}

//////////////////////////////////////////////////////////////////////////
//! WebGPU has no glGetError. Validation errors are reported asynchronously to
//! the uncaptured-error callback that WGPUContext installs, which is both more
//! informative and impossible to poll for here.
//////////////////////////////////////////////////////////////////////////
void CWGPURenderer::CheckError(const char* comment)
{
}
