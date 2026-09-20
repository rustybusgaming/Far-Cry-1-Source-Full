#ifndef _CRY_PASS_DESC_H_
#define _CRY_PASS_DESC_H_

/*!
	CryPassDesc -- what a CryEngine shader pass is, in a form any backend can read.

	WHY THIS IS IN Common/ AND NOT IN A BACKEND

	This started life inside XRenderWGPU, because WebGPU was the first backend
	that needed it. But nothing in it is about WebGPU: it is the engine's own
	fixed-function texture-stage model, and every backend that has to reproduce
	that model needs exactly the same description.

	Sharing it is not tidiness. It means the WebGL2 backend and the WebGPU
	backend translate the SAME description into their two languages -- so a
	decision about what eCO_DOTPRODUCT3 means is made once, and the backend that
	can be pixel-verified in a browser is verifying the same decision the one
	that cannot is relying on.

	WHAT IS ACTUALLY BEING DESCRIBED

	"Far Cry's shaders" is three different things wearing one name, and only the
	first is what this describes:

	  1. The fixed-function texture-stage pipeline. Each pass carries a list of
	     SShaderTexUnit, and each unit says "combine my texture with what came
	     before, using this operation and these two arguments" -- eCO_MODULATE,
	     eCO_BLENDTEXTUREALPHA, eCO_DOTPRODUCT3 and so on. This is the D3D8-era
	     SetTextureStageState model. It is the bulk of the shaders and it is
	     fully described by data the engine already parses.

	  2. NV register combiners and ARB/NV assembly programs, for the hardware
	     paths of the day. Separate problem, not started.

	  3. Cg programs, whose compiler ships as a binary blob with no source and
	     therefore cannot be run at all. Those need re-authoring rather than
	     translating.

	Everything here is a plain description with no API types in it, so it
	compiles natively and the generators that consume it are unit-tested
	natively, with no device and no browser.
*/

#include "CryStateGen.h"

//! One texture stage, as SShaderTexUnit describes it.
struct SCryStageDesc
{
	int		nColorOp;		//!< EColorOp   (eCO_*)
	int		nColorArg;		//!< two EColorArg values packed 3 bits apart
	int		nAlphaOp;		//!< EColorOp   (eCO_*)
	int		nAlphaArg;		//!< as nColorArg
	bool	bHasTexture;	//!< false for a stage with no texture bound

	SCryStageDesc()
		: nColorOp(0), nColorArg(0), nAlphaOp(0), nAlphaArg(0), bHasTexture(false) {}
};

//! The whole pass.
struct SCryPassDesc
{
	enum { kMaxStages = 8 };

	int				nStages;
	SCryStageDesc	stages[kMaxStages];

	bool			bHasVertexColor;	//!< the vertex format carries a colour
	bool			bHasTexCoord;		//!< ... and a texture coordinate

	//! Alpha test. Neither WebGL2 nor WebGPU has alpha-test state -- it went
	//! with the rest of the fixed-function pipeline -- so it becomes a discard
	//! in the fragment shader, which is where modern APIs put it.
	//!
	//! The DIRECTION is carried, not just a threshold. The engine has both
	//! "keep while alpha is at least X" and "keep while alpha is below X"
	//! (GS_ALPHATEST_LESS128), and collapsing those into one comparison would
	//! invert every surface that uses the latter.
	int				nAlphaTest;		//!< ECryAlphaTest
	float			fAlphaRef;

	SCryPassDesc()
		: nStages(0), bHasVertexColor(true), bHasTexCoord(true)
		, nAlphaTest(eCryAlphaTest_None), fAlphaRef(0.0f) {}
};

//! Unpack the THREE arguments the engine packs into one int.
//!
//! ShaderParse.cpp writes them as "arg0 | (arg1 << 3) | (arg2 << 6)", masking
//! with ~7, ~(7<<3) and ~(7<<6) -- three bits each. The shipped Direct3D 9
//! backend reads all three: bits 0-2 become D3DTSS_COLORARG1, bits 3-5
//! COLORARG2, and bits 6-8 COLORARG0.
//!
//! A THIRD ARGUMENT EXISTS, AND THIS FILE USED TO SAY IT DID NOT
//!
//! The first version of the translation claimed SShaderTexUnit packed only
//! two, and refused eCO_MULTIPLYADD on that basis. It was wrong, and reading
//! D3DRendPipeline.cpp is what showed it: that backend sets three stage
//! arguments per stage, from one byte.
//!
//! THE THIRD ONE IS TRUNCATED, IN THE ORIGINAL ENGINE
//!
//! m_eColorArg and m_eAlphaArg are declared byte, so "arg2 << 6" keeps only
//! bits 6 and 7. eCA_Constant is 4, and 4 << 6 is 256 -- entirely outside a
//! byte, so a shader script asking for Constant as its third argument gets
//! eCA_Specular (0) instead, silently. The Direct3D backend's
//! "case eCA_Constant" in its third-argument switch is unreachable.
//!
//! That is not a bug introduced here and it is not corrected here. A shader
//! written for Far Cry was authored against the engine that truncates, so
//! reproducing the truncation is what renders what the artist saw. Reading
//! straight from the byte, as CryPass_FromTexUnit does, reproduces it exactly.
inline int CryPass_Arg0(int nPacked) { return nPacked & 7; }
inline int CryPass_Arg1(int nPacked) { return (nPacked >> 3) & 7; }
inline int CryPass_Arg2(int nPacked) { return (nPacked >> 6) & 7; }

//! Name of an operation, for diagnostics. Returns "eCO_<unknown>" if
//! unrecognised.
const char* CryPass_OpName(int nOp);

//! Build a stage description from one of the engine's own texture units.
//!
//! Takes the fields rather than SShaderTexUnit itself, so this header does not
//! have to reach into RenderDll/Common/Shaders/Shader.h -- which would drag the
//! whole renderer in and stop any of this compiling natively. The caller does
//! the one-line unpack; see CryPass_FromTexUnit's note in the .cpp.
//!
//! nColorArg and nAlphaArg are the engine's PACKED bytes, passed through
//! unchanged. Passing the byte rather than the unpacked arguments is what
//! reproduces the third-argument truncation described above.
void CryPass_FromTexUnit(int nColorOp, int nColorArg,
                         int nAlphaOp, int nAlphaArg,
                         bool bHasTexture, SCryStageDesc& out);

//! Fill in a pass's alpha test from the engine's GS_* render-state word.
//!
//! The descriptor has carried nAlphaTest and fAlphaRef since the beginning and
//! nothing ever set them from engine data -- every pass built by hand in a test
//! or by GLESShader_DynamicPass had no alpha test, so the field was exercised
//! only by tests. This is the join that was missing.
void CryPass_SetAlphaTestFromRenderState(unsigned int nRenderState,
                                         SCryPassDesc& out);

//! A stable key for the description, so identical passes share one compiled
//! program or pipeline.
//!
//! Built from the same fields the generators read, deliberately, so a field
//! added to one without the other shows up as two passes sharing something
//! they should not.
unsigned long long CryPass_Key(const SCryPassDesc& desc);

#endif //_CRY_PASS_DESC_H_
