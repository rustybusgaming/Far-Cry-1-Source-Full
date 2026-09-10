////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   CryPassDesc.cpp
//  Description: Operation names and the pass cache key. See CryPassDesc.h.
//
////////////////////////////////////////////////////////////////////////////

#include "CryPassDesc.h"

// platform.h first: IShader.h reaches IRenderer.h, which uses HRESULT and
// DWORD before anything has defined them.
#include <platform.h>
#include <IShader.h>

#include <string.h>

//////////////////////////////////////////////////////////////////////////

const char* CryPass_OpName(int nOp)
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
//! FNV-1a over everything that changes the emitted source.
//!
//! Deliberately built from the same fields the generator reads, so a field
//! added to one without the other shows up as two passes sharing a pipeline
//! they should not.
//////////////////////////////////////////////////////////////////////////
unsigned long long CryPass_Key(const SCryPassDesc& desc)
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

	for (int i = 0; i < desc.nStages && i < SCryPassDesc::kMaxStages; ++i)
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
