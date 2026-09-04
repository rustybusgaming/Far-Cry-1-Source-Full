#ifndef _CRY_GLES_RENDERER_H_
#define _CRY_GLES_RENDERER_H_

/*!
	CGLESRenderer -- the WebGL2 backend, brought up incrementally.

	HOW THIS IS BUILT, AND WHY

	A backend is a subclass of CRenderer (RenderDll/Common) that fills in
	roughly a hundred entry points. Crytek's XRenderNULL is a complete, working
	implementation of every one of them that draws nothing, in about 2000 lines.
	XRenderOGL is the same contract implemented for real, in 45,733.

	Rather than start from an empty class and stub the tail -- which would mean
	writing 2000 lines of no-ops that already exist, and inventing my own
	version of decisions Crytek already made -- this backend derives from
	CNULLRenderer and overrides what it has implemented for real.

	That has three properties worth having:

	  * The unimplemented tail is not a pile of stubs I wrote. It is Crytek's
	    own draw-nothing implementation, which is exactly the correct
	    behaviour for a backend entry point that is not finished.

	  * Progress is measurable and cannot be faked: what is real is what
	    CGLESRenderer overrides, and everything else is visibly inherited.
	    "How far along is the renderer" has an exact answer at any moment.

	  * The delta is small enough to read. The override list below IS the
	    scope of the backend so far; nothing else is claimed.

	An earlier version of this comment said the build ENFORCED the split, by
	leaving NULL_System.cpp out of the target so the thirteen methods it holds
	would have no definition and the link would fail until this class supplied
	them. That does not work, and the reason is worth writing down:
	CNULLRenderer's vtable is emitted in NULL_Renderer.cpp -- the translation
	unit defining its key function -- and a vtable needs every one of the
	class's virtuals defined, whether or not a derived class overrides them.
	Leaving the file out breaks the base class, not just the parts being
	replaced. So NULL_System.cpp is compiled, with CRY_GLES_BACKEND removing
	only its module-level tail: the engine-interface globals and the
	PackageRenderConstructor, which this backend has to own instead.

	What is real is therefore what this class declares, and that is a fact
	about the code rather than something the toolchain checks.

	This is a scaffold with a deliberate demolition order, not a permanent
	arrangement. As each subsystem is written against GLES -- textures, vertex
	buffers, the shader pipeline -- its methods move from inherited to
	overridden, and when the last one moves the dependency on XRenderNULL
	drops out of the CMake target.

	WHAT IS REAL TODAY

	Context creation, resolution, viewport, buffer clears and frame
	begin/present. That is enough to prove the engine can drive a live WebGL2
	context, which is the thing that could not be known from the outside.
	Nothing draws geometry yet.
*/

#include "NULL_Renderer.h"

class CGLESRenderer : public CNULLRenderer
{
public:
	CGLESRenderer();
	virtual ~CGLESRenderer();

	//////////////////////////////////////////////////////////////////////
	// The system and context layer. These have no definition anywhere else
	// in this target -- see the header comment -- so every one of them is
	// this backend's own.
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
	// Frame and framebuffer. Inherited versions do nothing; these are the
	// first entry points that touch GL for real.
	//////////////////////////////////////////////////////////////////////
	virtual void	BeginFrame();
	virtual void	Update();

	virtual void	SetViewport(int x = 0, int y = 0, int width = 0, int height = 0);
	virtual void	SetScissor(int x = 0, int y = 0, int width = 0, int height = 0);

	virtual void	ClearDepthBuffer();
	virtual void	ClearColorBuffer(const Vec3d vColor);

	virtual void	CheckError(const char* comment);

	//! Reads the framebuffer back into system memory. The engine uses this for
	//! screenshots; it is also the only way to see what was actually drawn, so
	//! it is what the browser test samples.
	virtual void	ReadFrameBuffer(unsigned char* pRGB, int nSizeX, int nSizeY,
	                                bool bBackBuffer, bool bRGBA,
	                                int nScaledX = -1, int nScaledY = -1);

	//////////////////////////////////////////////////////////////////////
	// Drawing. GLES has no fixed-function pipeline, so where the original
	// backend could enable texturing and call glBegin, everything here goes
	// through a compiled program and a vertex buffer.
	//////////////////////////////////////////////////////////////////////
	virtual void	Set2DMode(bool enable, int ortox, int ortoy);

	virtual void*	GetDynVBPtr(int nVerts, int& nOffs, int Pool);
	virtual void	DrawDynVB(int nOffs, int Pool, int nVerts);
	virtual void	DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F* pBuf, ushort* pInds,
	                          int nVerts, int nInds, int nPrimType);

	//////////////////////////////////////////////////////////////////////
	// Textures. The engine passes these around as plain integer ids; see
	// GLESTexture.h for how those map onto GL names.
	//////////////////////////////////////////////////////////////////////
	virtual unsigned int DownLoadToVideoMemory(unsigned char* data, int w, int h,
	                                           ETEX_Format eTFSrc, ETEX_Format eTFDst,
	                                           int nummipmap, bool repeat = true,
	                                           int filter = FILTER_BILINEAR, int Id = 0,
	                                           char* szCacheName = NULL, int flags = 0);

	virtual void	UpdateTextureInVideoMemory(uint tid, unsigned char* data,
	                                           int posx, int posy, int w, int h,
	                                           ETEX_Format eTF = eTF_0888);

	virtual void	RemoveTexture(unsigned int TextureId);
	virtual void	SetTexture(int tnum, ETexType eTT = eTT_Base);

	//! Free the GL objects the drawing path owns. Separate from ShutDown so a
	//! context loss can be recovered from without tearing the renderer down.
	void			ReleaseDrawResources();

private:
	//! Upload and draw through the dynamic program. The single place that
	//! issues geometry, so state setup exists once.
	void			DrawDynamic(const struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F* pVerts, int nVerts,
	                            const unsigned short* pInds, int nInds, int nPrimType);

	//! Lazily create the VAO and the two buffers.
	bool			EnsureDynamicBuffers();

	//! What Init() was asked for, kept so ChangeResolution can act without a
	//! second round trip to the browser.
	int		m_nCanvasWidth;
	int		m_nCanvasHeight;

	//! The clear colour the engine last asked for. GL keeps this as state
	//! anyway, but reading it back is a synchronous call into JavaScript.
	float	m_fClearColor[4];

	bool	m_bContextCreated;

	//////////////////////////////////////////////////////////////////////
	// The dynamic drawing path.
	//////////////////////////////////////////////////////////////////////

	//! Model-view-projection handed to the program. The engine's matrix API is
	//! the fixed-function one -- PushMatrix, MultMatrix, LoadMatrix -- and
	//! GLES has none of it, so the backend keeps the matrix itself. Only the
	//! 2D orthographic case is filled in so far, which is what Set2DMode and
	//! everything drawn in screen space needs.
	float			m_matMVP[16];
	bool			m_b2DMode;

	unsigned int	m_nDynVAO;
	unsigned int	m_nDynVBO;
	unsigned int	m_nDynIBO;
	int				m_nDynVBOCapacity;	//!< bytes currently allocated
	int				m_nDynIBOCapacity;

	//! Staging for the pooled GetDynVBPtr/DrawDynVB pair, which hands the
	//! caller a pointer to fill and then asks for it to be drawn by offset.
	//! CryFont is the caller that matters.
	void*			m_pDynPool;
	int				m_nDynPoolVerts;
	int				m_nDynPoolUsed;
};

#endif //_CRY_GLES_RENDERER_H_
