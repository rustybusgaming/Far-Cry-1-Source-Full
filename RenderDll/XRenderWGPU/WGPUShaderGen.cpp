////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUShaderGen.cpp
//  Description: Texture-stage pass -> WGSL. See WGPUShaderGen.h.
//
////////////////////////////////////////////////////////////////////////////

#include "WGPUShaderGen.h"

// platform.h first: IShader.h reaches IRenderer.h, which uses HRESULT and
// DWORD before anything has defined them.
#include <platform.h>
#include <IShader.h>

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////

const char* WGPUShaderGen_OpName(int nOp)
{
	switch (nOp)
	{
	case eCO_NOSET:						return "eCO_NOSET";
	case eCO_DISABLE:					return "eCO_DISABLE";
	case eCO_REPLACE:					return "eCO_REPLACE";
	case eCO_DECAL:						return "eCO_DECAL";
	case eCO_ARG2:						return "eCO_ARG2";
	case eCO_MODULATE:					return "eCO_MODULATE";
	case eCO_MODULATE2X:				return "eCO_MODULATE2X";
	case eCO_MODULATE4X:				return "eCO_MODULATE4X";
	case eCO_BLENDDIFFUSEALPHA:			return "eCO_BLENDDIFFUSEALPHA";
	case eCO_BLENDTEXTUREALPHA:			return "eCO_BLENDTEXTUREALPHA";
	case eCO_DETAIL:					return "eCO_DETAIL";
	case eCO_ADD:						return "eCO_ADD";
	case eCO_ADDSIGNED:					return "eCO_ADDSIGNED";
	case eCO_ADDSIGNED2X:				return "eCO_ADDSIGNED2X";
	case eCO_MULTIPLYADD:				return "eCO_MULTIPLYADD";
	case eCO_BUMPENVMAP:				return "eCO_BUMPENVMAP";
	case eCO_BLEND:						return "eCO_BLEND";
	case eCO_MODULATEALPHA_ADDCOLOR:	return "eCO_MODULATEALPHA_ADDCOLOR";
	case eCO_MODULATECOLOR_ADDALPHA:	return "eCO_MODULATECOLOR_ADDALPHA";
	case eCO_MODULATEINVALPHA_ADDCOLOR:	return "eCO_MODULATEINVALPHA_ADDCOLOR";
	case eCO_MODULATEINVCOLOR_ADDALPHA:	return "eCO_MODULATEINVCOLOR_ADDALPHA";
	case eCO_DOTPRODUCT3:				return "eCO_DOTPRODUCT3";
	case eCO_LERP:						return "eCO_LERP";
	case eCO_SUBTRACT:					return "eCO_SUBTRACT";
	default:							return "eCO_<unknown>";
	}
}

//////////////////////////////////////////////////////////////////////////
//! The name of the local holding a stage's sampled texel.
//!
//! Per stage, not one shared name. Every stage's texel lives in the same
//! function scope, so a single "texel" makes the second stage a redeclaration
//! and the whole module fails to compile -- which is a runtime failure in the
//! browser, long after this code has said the translation succeeded.
//////////////////////////////////////////////////////////////////////////
static std::string TexelName(int nStage)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "texel%d", nStage);
	return buf;
}

//////////////////////////////////////////////////////////////////////////
//! Where an argument's value comes from.
//!
//! eCA_Previous is the accumulator: the result of the stage before this one. On
//! stage 0 there is nothing before, and the fixed-function pipeline defines it
//! as the diffuse colour, which is what makes a lone eCO_MODULATE stage produce
//! "texture times vertex colour" rather than "texture times nothing".
//!
//! eCA_Texture always resolves to THIS stage's texel. A stage without a texture
//! still has one declared, holding white, so there is no case where the name is
//! missing -- see the emit loop.
//////////////////////////////////////////////////////////////////////////
static std::string ArgExpr(int nArg, int nStage)
{
	switch (nArg)
	{
	case eCA_Texture:	return TexelName(nStage);
	case eCA_Diffuse:	return "diffuse";
	case eCA_Previous:	return "acc";
	case eCA_Constant:	return "uConst.color";
	case eCA_Specular:	return "specular";
	default:			return "vec4f(1.0, 1.0, 1.0, 1.0)";
	}
}

//////////////////////////////////////////////////////////////////////////
//! One stage's colour or alpha expression.
//!
//! Written against vec4 throughout even for the alpha channel, and the caller
//! takes .a from the result. That keeps one code path instead of two nearly
//! identical ones, and WGSL folds the unused components away.
//!
//! Returns false for an operation that needs something this layer does not
//! have. See the note in the header about not guessing.
//////////////////////////////////////////////////////////////////////////
static bool OpExpr(int nOp, const char* szA0, const char* szA1,
                   const char* szTexel, std::string& sOut, std::string& sError)
{
	char buf[512];

	switch (nOp)
	{
	case eCO_REPLACE:
		snprintf(buf, sizeof(buf), "%s", szA0);
		break;

	case eCO_ARG2:
		snprintf(buf, sizeof(buf), "%s", szA1);
		break;

	case eCO_MODULATE:
		snprintf(buf, sizeof(buf), "(%s * %s)", szA0, szA1);
		break;

	case eCO_MODULATE2X:
		snprintf(buf, sizeof(buf), "(%s * %s * 2.0)", szA0, szA1);
		break;

	case eCO_MODULATE4X:
		snprintf(buf, sizeof(buf), "(%s * %s * 4.0)", szA0, szA1);
		break;

	case eCO_ADD:
		snprintf(buf, sizeof(buf), "(%s + %s)", szA0, szA1);
		break;

	case eCO_SUBTRACT:
		snprintf(buf, sizeof(buf), "(%s - %s)", szA0, szA1);
		break;

	// The signed variants bias by a half so an unsigned texture can encode a
	// signed value -- the standard trick for normal maps before signed formats
	// existed.
	case eCO_ADDSIGNED:
		snprintf(buf, sizeof(buf), "(%s + %s - 0.5)", szA0, szA1);
		break;

	case eCO_ADDSIGNED2X:
		snprintf(buf, sizeof(buf), "((%s + %s - 0.5) * 2.0)", szA0, szA1);
		break;

	// DETAIL is MODULATE2X by another name in this engine's usage: a detail map
	// centred on 0.5 that brightens and darkens the base.
	case eCO_DETAIL:
		snprintf(buf, sizeof(buf), "(%s * %s * 2.0)", szA0, szA1);
		break;

	case eCO_BLENDDIFFUSEALPHA:
		snprintf(buf, sizeof(buf), "mix(%s, %s, diffuse.a)", szA1, szA0);
		break;

	case eCO_BLENDTEXTUREALPHA:
		snprintf(buf, sizeof(buf), "mix(%s, %s, %s.a)", szA1, szA0, szTexel);
		break;

	// DECAL lays the texture over what came before, using the texture's own
	// alpha as the coverage.
	case eCO_DECAL:
		snprintf(buf, sizeof(buf), "mix(acc, %s, %s.a)", szTexel, szTexel);
		break;

	case eCO_LERP:
		// Interpolates between the two arguments by the first one's alpha.
		snprintf(buf, sizeof(buf), "mix(%s, %s, (%s).a)", szA1, szA0, szA0);
		break;

	// The classic bump-mapping dot product. Both arguments are biased out of
	// unsigned range first, and the scalar result is broadcast.
	case eCO_DOTPRODUCT3:
		snprintf(buf, sizeof(buf),
		         "vec4f(vec3f(4.0 * dot((%s).rgb - 0.5, (%s).rgb - 0.5)), 1.0)",
		         szA0, szA1);
		break;

	case eCO_MODULATEALPHA_ADDCOLOR:
		snprintf(buf, sizeof(buf),
		         "vec4f((%s).rgb + (%s).a * (%s).rgb, (%s).a)", szA0, szA0, szA1, szA1);
		break;

	case eCO_MODULATECOLOR_ADDALPHA:
		snprintf(buf, sizeof(buf),
		         "vec4f((%s).rgb * (%s).rgb + (%s).a, (%s).a)", szA0, szA1, szA0, szA1);
		break;

	case eCO_MODULATEINVALPHA_ADDCOLOR:
		snprintf(buf, sizeof(buf),
		         "vec4f((1.0 - (%s).a) * (%s).rgb + (%s).rgb, (%s).a)",
		         szA0, szA1, szA0, szA1);
		break;

	case eCO_MODULATEINVCOLOR_ADDALPHA:
		snprintf(buf, sizeof(buf),
		         "vec4f((1.0 - (%s).rgb) * (%s).rgb + (%s).a, (%s).a)",
		         szA0, szA1, szA0, szA1);
		break;

	//////////////////////////////////////////////////////////////////////
	// Deliberately not translated. Each needs something this layer is not
	// given, and inventing a value would produce a shader that looks
	// plausible and computes the wrong thing.
	//////////////////////////////////////////////////////////////////////
	case eCO_MULTIPLYADD:
		sError = "eCO_MULTIPLYADD takes three arguments; SShaderTexUnit packs only two";
		return false;

	case eCO_BUMPENVMAP:
		sError = "eCO_BUMPENVMAP needs the stage's bump matrix, which is not part "
		         "of the stage description";
		return false;

	case eCO_BLEND:
		sError = "eCO_BLEND needs a blend factor from render state, which is not "
		         "part of the stage description";
		return false;

	case eCO_NOSET:
	case eCO_DISABLE:
		// Not an error: the stage contributes nothing and is skipped.
		sOut.clear();
		return true;

	default:
	{
		char err[128];
		snprintf(err, sizeof(err), "unhandled colour operation %s (%d)",
		         WGPUShaderGen_OpName(nOp), nOp);
		sError = err;
		return false;
	}
	}

	sOut = buf;
	return true;
}

//////////////////////////////////////////////////////////////////////////

bool WGPUShaderGen_Build(const SWGPUShaderDesc& desc, std::string& sOut, std::string& sError)
{
	if (desc.nStages < 0 || desc.nStages > SWGPUShaderDesc::kMaxStages)
	{
		sError = "stage count out of range";
		return false;
	}

	std::string s;

	//////////////////////////////////////////////////////////////////////
	// Uniforms and resources.
	//
	// One bind group: the transform and constant colour in a uniform buffer,
	// then a sampler and texture per stage. Grouping every stage into one bind
	// group means a draw sets one thing rather than N.
	//////////////////////////////////////////////////////////////////////
	s += "// Generated from a CryEngine shader pass. Do not edit.\n";
	s += "struct Uniforms {\n";
	s += "  mvp   : mat4x4f,\n";
	s += "  color : vec4f,\n";
	s += "};\n";
	s += "@group(0) @binding(0) var<uniform> uConst : Uniforms;\n";

	int nBinding = 1;
	for (int i = 0; i < desc.nStages; ++i)
	{
		if (!desc.stages[i].bHasTexture)
			continue;

		char buf[256];
		snprintf(buf, sizeof(buf),
		         "@group(0) @binding(%d) var samp%d : sampler;\n"
		         "@group(0) @binding(%d) var tex%d  : texture_2d<f32>;\n",
		         nBinding, i, nBinding + 1, i);
		s += buf;
		nBinding += 2;
	}

	//////////////////////////////////////////////////////////////////////
	// Vertex stage.
	//
	// The attribute numbering matches the GLES backend's, so both backends read
	// the same vertex buffers without a second layout to keep in step.
	//////////////////////////////////////////////////////////////////////
	s += "\nstruct VSOut {\n";
	s += "  @builtin(position) pos : vec4f,\n";
	s += "  @location(0) color : vec4f,\n";
	s += "  @location(1) uv    : vec2f,\n";
	s += "};\n\n";

	s += "@vertex\n";
	s += "fn vs_main(\n";
	s += "  @location(0) position : vec3f,\n";
	s += "  @location(1) color    : vec4f,\n";
	s += "  @location(2) uv       : vec2f,\n";
	s += ") -> VSOut {\n";
	s += "  var out : VSOut;\n";
	s += "  out.pos = uConst.mvp * vec4f(position, 1.0);\n";

	// The engine packs vertex colours as B,G,R,A -- the Direct3D order -- and
	// WebGPU has no BGRA vertex format any more than WebGL2 does, so the
	// swizzle happens here exactly as it does in the GLES shader.
	if (desc.bHasVertexColor)
		s += "  out.color = color.bgra;\n";
	else
		s += "  out.color = vec4f(1.0, 1.0, 1.0, 1.0);\n";

	s += desc.bHasTexCoord ? "  out.uv = uv;\n" : "  out.uv = vec2f(0.0, 0.0);\n";
	s += "  return out;\n";
	s += "}\n\n";

	//////////////////////////////////////////////////////////////////////
	// Fragment stage: the texture-stage chain.
	//////////////////////////////////////////////////////////////////////
	s += "@fragment\n";
	s += "fn fs_main(in : VSOut) -> @location(0) vec4f {\n";
	s += "  let diffuse  = in.color;\n";
	s += "  let specular = vec4f(0.0, 0.0, 0.0, 0.0);\n";

	// Stage 0's "previous" is the diffuse colour; see ArgExpr.
	s += "  var acc = diffuse;\n";

	for (int i = 0; i < desc.nStages; ++i)
	{
		const SWGPUStageDesc& st = desc.stages[i];

		if (st.nColorOp == eCO_DISABLE || st.nColorOp == eCO_NOSET)
			continue;

		char buf[256];
		s += "\n";

		const std::string sTexel = TexelName(i);

		if (st.bHasTexture)
		{
			snprintf(buf, sizeof(buf),
			         "  let %s = textureSample(tex%d, samp%d, in.uv);\n",
			         sTexel.c_str(), i, i);
			s += buf;
		}
		else
		{
			// A stage with no texture still runs its operation; anything
			// reading eCA_Texture gets white, which is the identity for the
			// multiplicative operations that dominate.
			//
			// It is declared rather than substituted inline so that every
			// stage's texel has a name, including the ops that read it without
			// naming it as an argument (DECAL, BLENDTEXTUREALPHA).
			snprintf(buf, sizeof(buf),
			         "  let %s = vec4f(1.0, 1.0, 1.0, 1.0);\n", sTexel.c_str());
			s += buf;
		}

		std::string sColor, sAlpha;

		const std::string sC0 = ArgExpr(WGPUShaderGen_Arg0(st.nColorArg), i);
		const std::string sC1 = ArgExpr(WGPUShaderGen_Arg1(st.nColorArg), i);
		const std::string sA0 = ArgExpr(WGPUShaderGen_Arg0(st.nAlphaArg), i);
		const std::string sA1 = ArgExpr(WGPUShaderGen_Arg1(st.nAlphaArg), i);

		if (!OpExpr(st.nColorOp, sC0.c_str(), sC1.c_str(), sTexel.c_str(),
		            sColor, sError))
		{
			char err[256];
			snprintf(err, sizeof(err), "stage %d colour: %s", i, sError.c_str());
			sError = err;
			return false;
		}

		if (!OpExpr(st.nAlphaOp, sA0.c_str(), sA1.c_str(), sTexel.c_str(),
		            sAlpha, sError))
		{
			char err[256];
			snprintf(err, sizeof(err), "stage %d alpha: %s", i, sError.c_str());
			sError = err;
			return false;
		}

		// Colour and alpha are separate operations on the same stage, computed
		// independently and recombined -- exactly what the fixed-function
		// hardware did.
		//
		// Each goes into its own local first rather than being substituted
		// twice into a vec4f(). Inlining them would write the whole expression
		// out once for .rgb and again for .a, which doubles the emitted source
		// for every stage and asks the driver's optimiser to undo it.
		if (!sColor.empty())
		{
			snprintf(buf, sizeof(buf), "  let c%d = %s;\n", i, sColor.c_str());
			s += buf;
		}
		if (!sAlpha.empty())
		{
			snprintf(buf, sizeof(buf), "  let a%d = %s;\n", i, sAlpha.c_str());
			s += buf;
		}

		char szRGB[32], szA[32];
		if (sColor.empty())
			snprintf(szRGB, sizeof(szRGB), "acc.rgb");
		else
			snprintf(szRGB, sizeof(szRGB), "c%d.rgb", i);

		if (sAlpha.empty())
			snprintf(szA, sizeof(szA), "acc.a");
		else
			snprintf(szA, sizeof(szA), "a%d.a", i);

		if (!sColor.empty() || !sAlpha.empty())
		{
			snprintf(buf, sizeof(buf), "  acc = vec4f(%s, %s);\n", szRGB, szA);
			s += buf;
		}
	}

	// Alpha test, as a discard. The condition is the NEGATION of the keep test:
	// the engine says which fragments survive, and a shader says which to throw
	// away.
	if (desc.nAlphaTest != eWGPUAlphaTest_None)
	{
		char buf[160];
		switch (desc.nAlphaTest)
		{
		case eWGPUAlphaTest_Greater:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a > %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		case eWGPUAlphaTest_GreaterEqual:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a >= %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		case eWGPUAlphaTest_Less:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a < %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		default:
			buf[0] = 0;
			break;
		}
		s += buf;
	}

	s += "\n  return acc;\n";
	s += "}\n";

	sOut.swap(s);
	return true;
}

//////////////////////////////////////////////////////////////////////////
//! FNV-1a over everything that changes the emitted source.
//!
//! Deliberately built from the same fields the generator reads, so a field
//! added to one without the other shows up as two passes sharing a pipeline
//! they should not.
//////////////////////////////////////////////////////////////////////////
unsigned long long WGPUShaderGen_Key(const SWGPUShaderDesc& desc)
{
	unsigned long long h = 1469598103934665603ULL;

	#define MIX(v) do { \
		unsigned long long _v = (unsigned long long)(v); \
		for (int _b = 0; _b < 8; ++_b) { \
			h ^= (_v >> (_b * 8)) & 0xFF; \
			h *= 1099511628211ULL; \
		} \
	} while (0)

	MIX(desc.nStages);
	MIX(desc.bHasVertexColor ? 1 : 0);
	MIX(desc.bHasTexCoord ? 1 : 0);

	// Both the direction and the threshold change the emitted source, so both
	// are in the key rather than just the presence of a test.
	MIX(desc.nAlphaTest);
	int nAlphaBits = 0;
	memcpy(&nAlphaBits, &desc.fAlphaRef, sizeof(nAlphaBits));
	MIX(nAlphaBits);

	for (int i = 0; i < desc.nStages && i < SWGPUShaderDesc::kMaxStages; ++i)
	{
		MIX(desc.stages[i].nColorOp);
		MIX(desc.stages[i].nColorArg);
		MIX(desc.stages[i].nAlphaOp);
		MIX(desc.stages[i].nAlphaArg);
		MIX(desc.stages[i].bHasTexture ? 1 : 0);
	}

	#undef MIX

	return h;
}
