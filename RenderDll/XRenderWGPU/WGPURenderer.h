#ifndef _CRY_WGPU_RENDERER_H_
#define _CRY_WGPU_RENDERER_H_

/*!
	CWGPURenderer -- the WebGPU backend.

	Built the same way as CGLESRenderer: it derives from CNULLRenderer and
	overrides what it has implemented for real, so the unimplemented tail is
	Crytek's own draw-nothing code rather than stubs, and the override list
	below is the exact scope of the backend. See XRenderGLES/GLESRenderer.h for
	the longer form of that argument -- it applies unchanged here.

	WHY A SECOND BACKEND RATHER THAN A REPLACEMENT

	XRenderGLES works and is verified against real pixels in a browser. This one
	is not verified at all yet -- see WGPUContext.h. Keeping both means the GLES
	path stays as a control: run the two side by side on the same scene and a
	difference in the output tells you which is wrong. Deleting the working one
	to make room for the new one would throw away the only reference there is.

	HOW WEBGPU DIFFERS, AND WHY IT MATTERS HERE

	WebGL is a state machine: bind a texture, set a blend mode, draw. WebGPU is
	not. Nearly everything is baked into an immutable render pipeline object at
	creation time -- shaders, vertex layout, blend state, depth state, primitive
	topology -- and drawing means selecting a pipeline and a bind group.

	That is a much better fit for what this engine actually does than it first
	appears. A CryEngine shader pass already IS a pipeline description: it
	carries its render state, its texture units and its vertex format together
	in one SShaderPass. The awkward part of the WebGL backend is that the pass
	has to be decomposed into a sequence of state calls; here it can be
	translated once into a pipeline and cached.

	The cost is that a pipeline cannot be built lazily in the middle of a draw
	call without stalling, so the mapping from "pass" to "pipeline" has to be
	keyed and cached deliberately. That is what makes the shader translation and
	the pipeline cache one piece of work rather than two.
*/

#include "NULL_Renderer.h"

class CWGPURenderer : public CNULLRenderer
{
public:
	CWGPURenderer();
	virtual ~CWGPURenderer();

	//////////////////////////////////////////////////////////////////////
	// The system and context layer -- the same set XRenderGLES owns, for the
	// same reason: NULL_System.cpp's definitions are compiled out of this
	// target's module tail and these are what a real backend cannot inherit.
	//////////////////////////////////////////////////////////////////////
	virtual WIN_HWND Init(int x, int y, int width, int height,
	                      unsigned int cbpp, int zbpp, int sbits, bool fullscreen,
	                      WIN_HINSTANCE hinst, WIN_HWND Glhwnd = 0, WIN_HDC Glhdc = 0,
	                      WIN_HGLRC hGLrc = 0, bool bReInit = false);

	virtual void	ShutDown(bool bReInit = false);

	virtual bool	SetCurrentContext(WIN_HWND hWnd);
	virtual bool	CreateContext(WIN_HWND hWnd, bool bAllowFSAA = false);
	virtual bool	DeleteContext(WIN_HWND hWnd);
	virtual void	MakeCurrent();

	virtual void	ShareResources(IRenderer* renderer);
	virtual void	RefreshResources(int nFlags);

	virtual int		EnumDisplayFormats(TArray<SDispFormat>& Formats, bool bReset);
	virtual bool	ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth,
	                                 int nNewRefreshHZ, bool bFullScreen);

	virtual bool	SetGammaDelta(const float fGamma);
	void			SetGamma(float fGamma);
	void			PS2SetDefaultState();

	//////////////////////////////////////////////////////////////////////
	// Frame lifetime.
	//
	// In WebGPU a frame is a command encoder and a render pass, not a sequence
	// of immediate calls, so BeginFrame opens them and Update submits.
	//////////////////////////////////////////////////////////////////////
	virtual void	BeginFrame();
	virtual void	Update();

	virtual void	SetViewport(int x = 0, int y = 0, int width = 0, int height = 0);
	virtual void	SetScissor(int x = 0, int y = 0, int width = 0, int height = 0);

	virtual void	ClearDepthBuffer();
	virtual void	ClearColorBuffer(const Vec3d vColor);

	virtual void	CheckError(const char* comment);

private:
	int		m_nCanvasWidth;
	int		m_nCanvasHeight;

	//! The clear colour the engine last asked for.
	//!
	//! Unlike GL, a clear is not a call -- it is the loadOp of a render pass
	//! attachment, decided when the pass begins. So the colour has to be known
	//! at BeginFrame rather than whenever ClearColorBuffer happens to be called,
	//! and a clear requested mid-frame cannot be honoured the same way.
	float	m_fClearColor[4];

	bool	m_bDeviceReady;
};

#endif //_CRY_WGPU_RENDERER_H_
