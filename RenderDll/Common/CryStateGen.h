#ifndef _CRY_STATEGEN_H_
#define _CRY_STATEGEN_H_

/*!
	CryStateGen -- decoding the engine's GS_* render-state word.

	WHY THIS IS A SEPARATE, API-FREE FILE IN Common/

	It is a pure function of an integer, so it compiles natively and is unit
	tested natively without a device. That keeps the decisions -- which blend
	factor is which, what an alpha test becomes when the API has no alpha test
	-- inside the part that can actually be checked here.

	It deliberately produces no API types, only neutral enums that a backend
	maps to its own, one line each: WGPUPipeline.cpp to Dawn's, the WebGL2
	backend to GL's. Putting webgpu.h or gl3.h in here would make the whole
	thing untestable to save a switch statement, and would stop the two
	backends sharing one decoder.

	WHAT THE ENGINE GIVES US

	One int. The low byte is a source and destination blend factor, four bits
	each; the rest is a bag of flags for depth, colour masking, stencil and the
	alpha test. It is the D3D8 render-state word, and every backend in the tree
	decodes it the same way.
*/

//! Blend factors, in the engine's vocabulary rather than any API's.
enum ECryBlendFactor
{
	eCryBlend_Zero = 0,
	eCryBlend_One,
	eCryBlend_SrcColor,
	eCryBlend_OneMinusSrcColor,
	eCryBlend_DstColor,
	eCryBlend_OneMinusDstColor,
	eCryBlend_SrcAlpha,
	eCryBlend_OneMinusSrcAlpha,
	eCryBlend_DstAlpha,
	eCryBlend_OneMinusDstAlpha,
	eCryBlend_SrcAlphaSaturated,
};

enum ECryCompare
{
	eCryCompare_Never = 0,
	eCryCompare_Less,
	eCryCompare_Equal,
	eCryCompare_LessEqual,
	eCryCompare_Greater,
	eCryCompare_Always,
};

//! How the alpha test is expressed. WebGPU has none, so it becomes a discard in
//! the fragment shader -- and the comparison direction matters: the engine has
//! both "keep when alpha is at least X" and "keep when alpha is below X".
enum ECryAlphaTest
{
	eCryAlphaTest_None = 0,
	eCryAlphaTest_Greater,		//!< keep while alpha >  fRef
	eCryAlphaTest_GreaterEqual,//!< keep while alpha >= fRef
	eCryAlphaTest_Less,		//!< keep while alpha <  fRef
};

struct SCryStateDesc
{
	bool				bBlendEnabled;
	ECryBlendFactor	eSrcFactor;
	ECryBlendFactor	eDstFactor;

	bool				bDepthTest;
	bool				bDepthWrite;
	ECryCompare		eDepthCompare;

	//! Per-channel colour write mask, as four bits: R=1, G=2, B=4, A=8.
	int					nColorWriteMask;

	ECryAlphaTest		eAlphaTest;
	float				fAlphaRef;

	bool				bStencil;
};

//! Decode one GS_* render-state word.
//!
//! Never fails: an unrecognised blend factor falls back to one that is at least
//! visible rather than refusing to draw, and says so through
//! CryStateGen_LastWarning(). A pass that renders slightly wrong is easier to
//! diagnose than a pass that does not render.
void CryStateGen_Decode(int nRenderState, SCryStateDesc& out);

//! Description of the last thing Decode could not translate exactly, or an
//! empty string. Not thread safe, and only meant for logging.
const char* CryStateGen_LastWarning();

//! Fold the state into a pipeline cache key.
unsigned long long CryStateGen_Key(const SCryStateDesc& desc);

#endif //_CRY_STATEGEN_H_
