////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   CryPassGen.cpp
//  Description: The texture-stage chain, in whichever language. See the header.
//
////////////////////////////////////////////////////////////////////////////

#include "CryPassGen.h"

// platform.h first: IShader.h reaches IRenderer.h, which uses HRESULT and
// DWORD before anything has defined them.
#include <platform.h>
#include <IShader.h>

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
//! See the header.
//////////////////////////////////////////////////////////////////////////
std::string CryPassGen_TexelName(int nStage)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "texel%d", nStage);
	return buf;
}

//! Opaque white, in the caller's dialect. The identity for the multiplicative
//! operations, and what an argument reads as when it names something this pass
//! does not have.
static std::string White(const SCryShaderDialect& d)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%s(1.0, 1.0, 1.0, 1.0)", d.szVec4);
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
static std::string ArgExpr(int nArg, int nStage, const SCryShaderDialect& d)
{
	switch (nArg)
	{
	case eCA_Texture:	return CryPassGen_TexelName(nStage);
	case eCA_Diffuse:	return "diffuse";
	case eCA_Previous:	return "acc";
	case eCA_Constant:	return d.szConstColor;
	case eCA_Specular:	return "specular";
	default:			return White(d);
	}
}

//////////////////////////////////////////////////////////////////////////
//! One stage's colour or alpha expression.
//!
//! Written against vec4 throughout even for the alpha channel, and the caller
//! takes .a from the result. That keeps one code path instead of two nearly
//! identical ones, and both languages fold the unused components away.
//!
//! Returns false for an operation that needs something this layer does not
//! have. See the note in the header about not guessing.
//////////////////////////////////////////////////////////////////////////
static bool OpExpr(int nOp, const char* szA0, const char* szA1,
                   const char* szTexel, const SCryShaderDialect& d,
                   std::string& sOut, std::string& sError)
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
		         "%s(%s(4.0 * dot((%s).rgb - 0.5, (%s).rgb - 0.5)), 1.0)",
		         d.szVec4, d.szVec3, szA0, szA1);
		break;

	case eCO_MODULATEALPHA_ADDCOLOR:
		snprintf(buf, sizeof(buf),
		         "%s((%s).rgb + (%s).a * (%s).rgb, (%s).a)",
		         d.szVec4, szA0, szA0, szA1, szA1);
		break;

	case eCO_MODULATECOLOR_ADDALPHA:
		snprintf(buf, sizeof(buf),
		         "%s((%s).rgb * (%s).rgb + (%s).a, (%s).a)",
		         d.szVec4, szA0, szA1, szA0, szA1);
		break;

	case eCO_MODULATEINVALPHA_ADDCOLOR:
		snprintf(buf, sizeof(buf),
		         "%s((1.0 - (%s).a) * (%s).rgb + (%s).rgb, (%s).a)",
		         d.szVec4, szA0, szA1, szA0, szA1);
		break;

	case eCO_MODULATEINVCOLOR_ADDALPHA:
		snprintf(buf, sizeof(buf),
		         "%s((1.0 - (%s).rgb) * (%s).rgb + (%s).a, (%s).a)",
		         d.szVec4, szA0, szA1, szA0, szA1);
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
		         CryPass_OpName(nOp), nOp);
		sError = err;
		return false;
	}
	}

	sOut = buf;
	return true;
}

//////////////////////////////////////////////////////////////////////////

bool CryPassGen_Body(const SCryPassDesc& desc, const SCryShaderDialect& d,
                     std::string& sOut, std::string& sError)
{
	if (desc.nStages < 0 || desc.nStages > SCryPassDesc::kMaxStages)
	{
		sError = "stage count out of range";
		return false;
	}

	std::string s;
	char buf[512];

	snprintf(buf, sizeof(buf), "  %s diffuse = %s;\n", d.szDeclConst, d.szDiffuse);
	s += buf;

	// Specular is declared even though nothing produces one yet: eCA_Specular
	// is a legal argument source and a pass that names it must still compile.
	// Black is the identity for the additive use it gets.
	snprintf(buf, sizeof(buf), "  %s specular = %s(0.0, 0.0, 0.0, 0.0);\n",
	         d.szDeclConst, d.szVec4);
	s += buf;

	// Stage 0's "previous" is the diffuse colour; see ArgExpr.
	snprintf(buf, sizeof(buf), "  %s acc = diffuse;\n", d.szDeclVar);
	s += buf;

	for (int i = 0; i < desc.nStages; ++i)
	{
		const SCryStageDesc& st = desc.stages[i];

		if (st.nColorOp == eCO_DISABLE || st.nColorOp == eCO_NOSET)
			continue;

		s += "\n";

		const std::string sTexel = CryPassGen_TexelName(i);

		if (st.bHasTexture)
		{
			char szSample[192];
			d.pfnSample(i, szSample, sizeof(szSample));
			snprintf(buf, sizeof(buf), "  %s %s = %s;\n",
			         d.szDeclConst, sTexel.c_str(), szSample);
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
			snprintf(buf, sizeof(buf), "  %s %s = %s;\n",
			         d.szDeclConst, sTexel.c_str(), White(d).c_str());
			s += buf;
		}

		std::string sColor, sAlpha;

		const std::string sC0 = ArgExpr(CryPass_Arg0(st.nColorArg), i, d);
		const std::string sC1 = ArgExpr(CryPass_Arg1(st.nColorArg), i, d);
		const std::string sA0 = ArgExpr(CryPass_Arg0(st.nAlphaArg), i, d);
		const std::string sA1 = ArgExpr(CryPass_Arg1(st.nAlphaArg), i, d);

		if (!OpExpr(st.nColorOp, sC0.c_str(), sC1.c_str(), sTexel.c_str(), d,
		            sColor, sError))
		{
			char err[256];
			snprintf(err, sizeof(err), "stage %d colour: %s", i, sError.c_str());
			sError = err;
			return false;
		}

		if (!OpExpr(st.nAlphaOp, sA0.c_str(), sA1.c_str(), sTexel.c_str(), d,
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
		// twice into a vec4 constructor. Inlining them would write the whole
		// expression out once for .rgb and again for .a, which doubles the
		// emitted source for every stage and asks the driver's optimiser to
		// undo it.
		if (!sColor.empty())
		{
			snprintf(buf, sizeof(buf), "  %s c%d = %s;\n",
			         d.szDeclConst, i, sColor.c_str());
			s += buf;
		}
		if (!sAlpha.empty())
		{
			snprintf(buf, sizeof(buf), "  %s a%d = %s;\n",
			         d.szDeclConst, i, sAlpha.c_str());
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
			snprintf(buf, sizeof(buf), "  acc = %s(%s, %s);\n", d.szVec4, szRGB, szA);
			s += buf;
		}
	}

	// Alpha test, as a discard. The condition is the NEGATION of the keep test:
	// the engine says which fragments survive, and a shader says which to throw
	// away.
	if (desc.nAlphaTest != eCryAlphaTest_None)
	{
		switch (desc.nAlphaTest)
		{
		case eCryAlphaTest_Greater:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a > %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		case eCryAlphaTest_GreaterEqual:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a >= %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		case eCryAlphaTest_Less:
			snprintf(buf, sizeof(buf), "\n  if (!(acc.a < %.6f)) { discard; }\n", desc.fAlphaRef);
			break;
		default:
			buf[0] = 0;
			break;
		}
		s += buf;
	}

	sOut.swap(s);
	return true;
}
