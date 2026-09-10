////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUStateGen.cpp
//  Description: GS_* render state -> neutral pipeline state. See the header.
//
////////////////////////////////////////////////////////////////////////////

#include "WGPUStateGen.h"

#include <platform.h>
#include <IRenderer.h>

#include <stdio.h>
#include <string.h>

static char g_szWarning[256] = { 0 };

const char* WGPUStateGen_LastWarning() { return g_szWarning; }

//////////////////////////////////////////////////////////////////////////

static EWGPUBlendFactor DecodeSrc(int nState, bool& bOk)
{
	bOk = true;
	switch (nState & GS_BLSRC_MASK)
	{
	case GS_BLSRC_ZERO:					return eWGPUBlend_Zero;
	case GS_BLSRC_ONE:					return eWGPUBlend_One;
	case GS_BLSRC_DSTCOL:				return eWGPUBlend_DstColor;
	case GS_BLSRC_ONEMINUSDSTCOL:		return eWGPUBlend_OneMinusDstColor;
	case GS_BLSRC_SRCALPHA:				return eWGPUBlend_SrcAlpha;
	case GS_BLSRC_ONEMINUSSRCALPHA:		return eWGPUBlend_OneMinusSrcAlpha;
	case GS_BLSRC_DSTALPHA:				return eWGPUBlend_DstAlpha;
	case GS_BLSRC_ONEMINUSDSTALPHA:		return eWGPUBlend_OneMinusDstAlpha;
	case GS_BLSRC_ALPHASATURATE:		return eWGPUBlend_SrcAlphaSaturated;
	default:
		bOk = false;
		return eWGPUBlend_One;
	}
}

static EWGPUBlendFactor DecodeDst(int nState, bool& bOk)
{
	bOk = true;
	switch (nState & GS_BLDST_MASK)
	{
	case GS_BLDST_ZERO:					return eWGPUBlend_Zero;
	case GS_BLDST_ONE:					return eWGPUBlend_One;
	case GS_BLDST_SRCCOL:				return eWGPUBlend_SrcColor;
	case GS_BLDST_ONEMINUSSRCCOL:		return eWGPUBlend_OneMinusSrcColor;
	case GS_BLDST_SRCALPHA:				return eWGPUBlend_SrcAlpha;
	case GS_BLDST_ONEMINUSSRCALPHA:		return eWGPUBlend_OneMinusSrcAlpha;
	case GS_BLDST_DSTALPHA:				return eWGPUBlend_DstAlpha;
	case GS_BLDST_ONEMINUSDSTALPHA:		return eWGPUBlend_OneMinusDstAlpha;
	default:
		bOk = false;
		return eWGPUBlend_Zero;
	}
}

//////////////////////////////////////////////////////////////////////////

void WGPUStateGen_Decode(int nRenderState, SWGPUStateDesc& out)
{
	g_szWarning[0] = 0;

	memset(&out, 0, sizeof(out));

	//////////////////////////////////////////////////////////////////////
	// Blending.
	//
	// Blending is on only when the state names BOTH factors. A zero in either
	// nibble is the engine's way of saying "no blending" -- note that this is
	// distinct from GS_BLSRC_ZERO, which is 0x1. The zero value is absence, not
	// a factor.
	//////////////////////////////////////////////////////////////////////
	const int nSrc = nRenderState & GS_BLSRC_MASK;
	const int nDst = nRenderState & GS_BLDST_MASK;

	if (nSrc && nDst)
	{
		bool bSrcOk = false, bDstOk = false;
		out.eSrcFactor = DecodeSrc(nRenderState, bSrcOk);
		out.eDstFactor = DecodeDst(nRenderState, bDstOk);
		out.bBlendEnabled = true;

		if (!bSrcOk || !bDstOk)
			snprintf(g_szWarning, sizeof(g_szWarning),
			         "unrecognised blend factors in state 0x%08x (src 0x%x, dst 0x%x)",
			         nRenderState, nSrc, nDst >> 4);
	}
	else
	{
		out.bBlendEnabled = false;
		out.eSrcFactor = eWGPUBlend_One;
		out.eDstFactor = eWGPUBlend_Zero;
	}

	//////////////////////////////////////////////////////////////////////
	// Depth.
	//
	// Both of these are NEGATIVE flags -- the engine's default is "test and
	// write" and the flags turn each off, except GS_DEPTHWRITE which is
	// positive. Reading either the wrong way round produces geometry that
	// sorts backwards, which is easy to mistake for a camera problem.
	//////////////////////////////////////////////////////////////////////
	out.bDepthTest  = (nRenderState & GS_NODEPTHTEST) == 0;
	out.bDepthWrite = (nRenderState & GS_DEPTHWRITE) != 0;

	if (nRenderState & GS_DEPTHFUNC_EQUAL)
		out.eDepthCompare = eWGPUCompare_Equal;
	else if (nRenderState & GS_DEPTHFUNC_GREAT)
		out.eDepthCompare = eWGPUCompare_Greater;
	else
		out.eDepthCompare = eWGPUCompare_LessEqual;

	// WebGPU has no way to disable the depth test as such; the equivalent is a
	// compare function that always passes.
	if (!out.bDepthTest)
		out.eDepthCompare = eWGPUCompare_Always;

	//////////////////////////////////////////////////////////////////////
	// Colour write mask. R=1 G=2 B=4 A=8.
	//////////////////////////////////////////////////////////////////////
	if (nRenderState & GS_NOCOLMASK)
		out.nColorWriteMask = 0;
	else if (nRenderState & GS_COLMASKONLYALPHA)
		out.nColorWriteMask = 8;
	else if (nRenderState & GS_COLMASKONLYRGB)
		out.nColorWriteMask = 1 | 2 | 4;
	else
		out.nColorWriteMask = 1 | 2 | 4 | 8;

	//////////////////////////////////////////////////////////////////////
	// Alpha test.
	//
	// The reference values are the D3D8 ones in 0..255, expressed here in 0..1
	// because that is what a shader compares against. The direction matters:
	// LESS128 keeps fragments BELOW the threshold, the opposite of the others,
	// and treating it as another >= test would invert every surface that uses
	// it.
	//////////////////////////////////////////////////////////////////////
	switch (nRenderState & GS_ALPHATEST_MASK)
	{
	case GS_ALPHATEST_GREATER0:
		out.eAlphaTest = eWGPUAlphaTest_Greater;
		out.fAlphaRef  = 0.0f;
		break;

	case GS_ALPHATEST_LESS128:
		out.eAlphaTest = eWGPUAlphaTest_Less;
		out.fAlphaRef  = 128.0f / 255.0f;
		break;

	case GS_ALPHATEST_GEQUAL128:
		out.eAlphaTest = eWGPUAlphaTest_GreaterEqual;
		out.fAlphaRef  = 128.0f / 255.0f;
		break;

	case GS_ALPHATEST_GEQUAL64:
		out.eAlphaTest = eWGPUAlphaTest_GreaterEqual;
		out.fAlphaRef  = 64.0f / 255.0f;
		break;

	default:
		out.eAlphaTest = eWGPUAlphaTest_None;
		out.fAlphaRef  = 0.0f;
		break;
	}

	out.bStencil = (nRenderState & GS_STENCIL) != 0;
}

//////////////////////////////////////////////////////////////////////////

unsigned long long WGPUStateGen_Key(const SWGPUStateDesc& desc)
{
	unsigned long long h = 1469598103934665603ULL;

	#define MIX(v) do { \
		unsigned long long _v = (unsigned long long)(v); \
		for (int _b = 0; _b < 8; ++_b) { h ^= (_v >> (_b * 8)) & 0xFF; h *= 1099511628211ULL; } \
	} while (0)

	MIX(desc.bBlendEnabled ? 1 : 0);
	MIX(desc.eSrcFactor);
	MIX(desc.eDstFactor);
	MIX(desc.bDepthTest ? 1 : 0);
	MIX(desc.bDepthWrite ? 1 : 0);
	MIX(desc.eDepthCompare);
	MIX(desc.nColorWriteMask);
	MIX(desc.eAlphaTest);
	MIX(desc.bStencil ? 1 : 0);

	// The reference is compiled into the shader, so two thresholds cannot share
	// a pipeline.
	int nRefBits = 0;
	memcpy(&nRefBits, &desc.fAlphaRef, sizeof(nRefBits));
	MIX(nRefBits);

	#undef MIX

	return h;
}
