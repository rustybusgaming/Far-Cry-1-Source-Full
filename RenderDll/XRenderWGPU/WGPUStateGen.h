#ifndef _CRY_WGPU_STATEGEN_H_
#define _CRY_WGPU_STATEGEN_H_

/*!
	WGPUStateGen -- translating the engine's render state into pipeline state.

	WHY THIS IS A SEPARATE, WEBGPU-FREE FILE

	Same reason as WGPUShaderGen: it is a pure function of an integer, so it
	compiles natively and is unit-tested natively without a device. That keeps
	the decisions -- which blend factor is which, what an alpha test becomes
	when the API has no alpha test -- inside the part that can actually be
	checked here.

	It deliberately does NOT produce WGPU types. It produces neutral enums which
	WGPUPipeline.cpp maps to Dawn's, one line each. Putting webgpu.h in here
	would make the whole thing untestable to save a switch statement.

	WHAT THE ENGINE GIVES US

	One int. The low byte is a source and destination blend factor, four bits
	each; the rest is a bag of flags for depth, colour masking, stencil and the
	alpha test. It is the D3D8 render-state word, and every backend in the tree
	decodes it the same way.
*/

//! Blend factors, in the engine's vocabulary rather than any API's.
enum EWGPUBlendFactor
{
	eWGPUBlend_Zero = 0,
	eWGPUBlend_One,
	eWGPUBlend_SrcColor,
	eWGPUBlend_OneMinusSrcColor,
	eWGPUBlend_DstColor,
	eWGPUBlend_OneMinusDstColor,
	eWGPUBlend_SrcAlpha,
	eWGPUBlend_OneMinusSrcAlpha,
	eWGPUBlend_DstAlpha,
	eWGPUBlend_OneMinusDstAlpha,
	eWGPUBlend_SrcAlphaSaturated,
};

enum EWGPUCompare
{
	eWGPUCompare_Never = 0,
	eWGPUCompare_Less,
	eWGPUCompare_Equal,
	eWGPUCompare_LessEqual,
	eWGPUCompare_Greater,
	eWGPUCompare_Always,
};

//! How the alpha test is expressed. WebGPU has none, so it becomes a discard in
//! the fragment shader -- and the comparison direction matters: the engine has
//! both "keep when alpha is at least X" and "keep when alpha is below X".
enum EWGPUAlphaTest
{
	eWGPUAlphaTest_None = 0,
	eWGPUAlphaTest_Greater,		//!< keep while alpha >  fRef
	eWGPUAlphaTest_GreaterEqual,//!< keep while alpha >= fRef
	eWGPUAlphaTest_Less,		//!< keep while alpha <  fRef
};

struct SWGPUStateDesc
{
	bool				bBlendEnabled;
	EWGPUBlendFactor	eSrcFactor;
	EWGPUBlendFactor	eDstFactor;

	bool				bDepthTest;
	bool				bDepthWrite;
	EWGPUCompare		eDepthCompare;

	//! Per-channel colour write mask, as four bits: R=1, G=2, B=4, A=8.
	int					nColorWriteMask;

	EWGPUAlphaTest		eAlphaTest;
	float				fAlphaRef;

	bool				bStencil;
};

//! Decode one GS_* render-state word.
//!
//! Never fails: an unrecognised blend factor falls back to one that is at least
//! visible rather than refusing to draw, and says so through
//! WGPUStateGen_LastWarning(). A pass that renders slightly wrong is easier to
//! diagnose than a pass that does not render.
void WGPUStateGen_Decode(int nRenderState, SWGPUStateDesc& out);

//! Description of the last thing Decode could not translate exactly, or an
//! empty string. Not thread safe, and only meant for logging.
const char* WGPUStateGen_LastWarning();

//! Fold the state into a pipeline cache key.
unsigned long long WGPUStateGen_Key(const SWGPUStateDesc& desc);

#endif //_CRY_WGPU_STATEGEN_H_
