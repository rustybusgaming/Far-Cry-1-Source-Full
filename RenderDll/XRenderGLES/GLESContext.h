#ifndef _CRY_GLES_CONTEXT_H_
#define _CRY_GLES_CONTEXT_H_

/*!
	GLESContext -- creating and owning the WebGL2 drawing context.

	WHY THIS IS SEPARATE FROM THE RENDERER

	Everything else in this backend is engine code that happens to call GL.
	This file is the one place that talks to the *browser*, and it is the only
	part that cannot run under a unit test -- there is no canvas, and no
	WebGL implementation, outside a real page.

	Keeping it behind a small interface means the renderer above it is written
	against a context it is handed, not against Emscripten, and the seam is
	where a native EGL or desktop-GL context would slot in later. It is the
	same split CryInput/WebInput.h uses for DOM input, for the same reason.

	WHY WEBGL2 AND NOT WEBGL1

	WebGL2 is GLES 3.0. The engine's original backend is OpenGL 1.x with NV
	register combiners and assembly-language vertex/fragment programs, none of
	which exists in either. Since the shading pipeline has to be rewritten
	either way, it is rewritten against the more capable target: GLES 3.0 has
	non-power-of-two textures with mipmaps, multiple render targets, integer
	attributes, uniform buffers and sRGB -- all of which Far Cry's renderer
	assumes in one form or another, and all of which WebGL1 lacks.
*/

#ifdef __cplusplus
extern "C" {
#endif

//! Everything a caller needs to know about the context that was created.
//! Queried once at creation rather than per frame -- glGetString and
//! glGetIntegerv are round trips to JavaScript here, not register reads.
struct SGLESContextInfo
{
	int		nMaxTextureSize;
	int		nMaxCubeTextureSize;
	int		nMaxTextureUnits;		//!< fragment shader samplers
	int		nMaxVertexAttribs;
	int		nMaxColorAttachments;	//!< MRT count; 1 on WebGL1, >= 4 here
	int		nMaxSamples;			//!< MSAA sample count, 0 if unsupported
	int		bHasAnisotropic;
	int		bHasS3TC;				//!< WEBGL_compressed_texture_s3tc -- Far Cry's
									//!< textures are DXT1/3/5, so without this
									//!< every texture needs decompressing on load
	const char*	szVendor;
	const char*	szRenderer;
	const char*	szVersion;
};

//! Create a WebGL2 context on the given canvas and make it current.
//!
//! szCanvasSelector is a CSS selector ("#canvas" by default when NULL). It is
//! not a window handle: there are no windows here, and the engine's
//! WIN_HWND-shaped Init() signature has nothing meaningful to pass.
//!
//! Returns 0 on failure, having logged the reason. Failure is a normal
//! outcome, not an assertion: a machine with WebGL2 disabled, a blocklisted
//! driver or a lost GPU process all land here, and the engine should be able
//! to say so rather than trap.
int GLESContext_Create(const char* szCanvasSelector, int nWidth, int nHeight);

//! Tear the context down. Safe to call when none was created.
void GLESContext_Destroy(void);

//! Make the created context current on this thread. Emscripten contexts are
//! per-thread, so this matters as soon as anything renders off the main
//! thread.
int GLESContext_MakeCurrent(void);

//! Resize the drawing buffer. The canvas CSS size and the drawing buffer size
//! are independent in WebGL; this sets the latter, which is what the engine
//! means by resolution.
void GLESContext_Resize(int nWidth, int nHeight);

//! Capabilities of the live context. Returns 0 if no context exists.
const struct SGLESContextInfo* GLESContext_GetInfo(void);

//! Whether a context is currently alive.
int GLESContext_IsCreated(void);

#ifdef __cplusplus
}
#endif

#endif //_CRY_GLES_CONTEXT_H_
