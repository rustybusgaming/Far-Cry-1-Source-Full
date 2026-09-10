#ifndef _CRY_WGPU_SHADERGEN_H_
#define _CRY_WGPU_SHADERGEN_H_

/*!
	WGPUShaderGen -- translating CryEngine shader passes into WGSL.

	WHAT IS ACTUALLY BEING TRANSLATED

	"Far Cry's shaders" is three different things wearing one name, and only one
	of them is what this file handles:

	  1. The fixed-function texture-stage pipeline. Each pass carries a list of
	     SShaderTexUnit, and each unit says "combine my texture with what came
	     before, using this operation and these two arguments" -- eCO_MODULATE,
	     eCO_BLENDTEXTUREALPHA, eCO_DOTPRODUCT3 and so on. This is the D3D8-era
	     SetTextureStageState model. It is the bulk of the shaders, it is fully
	     described by data the engine already parses, and it is what this file
	     turns into WGSL.

	  2. NV register combiners and ARB/NV assembly programs, for the hardware
	     paths of the day. Separate problem, not started.

	  3. Cg programs, whose compiler ships as a binary blob with no source and
	     therefore cannot be run at all. Those need re-authoring rather than
	     translating.

	WHY THIS FILE HAS NO WEBGPU IN IT

	Emitting WGSL is a pure function: a stage description in, a string out. It
	needs no device, no adapter and no browser -- so it compiles natively and is
	unit-tested natively, in tests/test_shadergen.cpp.

	That matters more than it sounds. The container this was written in cannot
	obtain a WebGPU adapter, so nothing that touches the GPU can be verified
	here. Keeping the translation separate from the execution means the part
	with all the decisions in it is fully testable anyway, and what remains
	unverified is the comparatively mechanical business of handing a string to
	the driver.

	ON GUESSING

	Several operations need a third argument or state this layer is not given.
	Those are reported as unsupported, by name, rather than approximated. A
	shader that fails to build is a visible problem; a shader that silently
	computes something else is a bug that takes days to find.
*/

#include <string>

// For EWGPUAlphaTest. Both of these are pure, WebGPU-free translation headers.
#include "WGPUStateGen.h"

//! One texture stage, as SShaderTexUnit describes it.
struct SWGPUStageDesc
{
	int		nColorOp;		//!< EColorOp   (eCO_*)
	int		nColorArg;		//!< two EColorArg values packed 3 bits apart
	int		nAlphaOp;		//!< EColorOp   (eCO_*)
	int		nAlphaArg;		//!< as nColorArg
	bool	bHasTexture;	//!< false for a stage with no texture bound

	SWGPUStageDesc()
		: nColorOp(0), nColorArg(0), nAlphaOp(0), nAlphaArg(0), bHasTexture(false) {}
};

//! The whole pass.
struct SWGPUShaderDesc
{
	enum { kMaxStages = 8 };

	int				nStages;
	SWGPUStageDesc	stages[kMaxStages];

	bool			bHasVertexColor;	//!< the vertex format carries a colour
	bool			bHasTexCoord;		//!< ... and a texture coordinate

	//! Alpha test. WebGPU has no alpha-test state -- it went with the rest of
	//! the fixed-function pipeline -- so it becomes a discard in the shader,
	//! which is where modern APIs put it.
	//!
	//! The DIRECTION is carried, not just a threshold. The engine has both
	//! "keep while alpha is at least X" and "keep while alpha is below X"
	//! (GS_ALPHATEST_LESS128), and collapsing those into one comparison would
	//! invert every surface that uses the latter.
	int				nAlphaTest;		//!< EWGPUAlphaTest
	float			fAlphaRef;

	SWGPUShaderDesc()
		: nStages(0), bHasVertexColor(true), bHasTexCoord(true)
		, nAlphaTest(eWGPUAlphaTest_None), fAlphaRef(0.0f) {}
};

//! Unpack the two arguments the engine packs into one int.
//! The engine writes these as "arg0 | (arg1 << 3)" and masks them with ~7 and
//! ~(7<<3), so three bits each.
inline int WGPUShaderGen_Arg0(int nPacked) { return nPacked & 7; }
inline int WGPUShaderGen_Arg1(int nPacked) { return (nPacked >> 3) & 7; }

//! Emit WGSL for a pass.
//!
//! Returns false and fills sError if any stage uses an operation this
//! translation cannot express. sOut is untouched on failure.
bool WGPUShaderGen_Build(const SWGPUShaderDesc& desc, std::string& sOut, std::string& sError);

//! A stable key for the description, so identical passes share one pipeline.
//!
//! WebGPU bakes shaders, blend state and vertex layout into an immutable
//! pipeline object, and building one is expensive enough that it cannot happen
//! per draw. Everything that affects the emitted source has to be in this key,
//! or two different passes would collide on one pipeline.
unsigned long long WGPUShaderGen_Key(const SWGPUShaderDesc& desc);

//! Name of an operation, for diagnostics. Returns "eCO_<n>" if unrecognised.
const char* WGPUShaderGen_OpName(int nOp);

#endif //_CRY_WGPU_SHADERGEN_H_
