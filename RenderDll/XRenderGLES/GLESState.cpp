////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESState.cpp
//  Description: GS_* render state -> GL calls. See GLESState.h.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESState.h"

#if defined(__EMSCRIPTEN__)

#include "CryStateGen.h"

#include <GLES3/gl3.h>

//! The last word applied, and whether anything has been applied at all. -1 is
//! not a usable sentinel: the engine's words are arbitrary ints and a caller
//! could legitimately pass it.
static int  g_nLastState  = 0;
static bool g_bHaveState  = false;

//////////////////////////////////////////////////////////////////////////
//! The engine's blend vocabulary to GL's, one line each.
//!
//! GLES 3.0 has every factor the engine names, so nothing here has to
//! approximate. eCryBlend_SrcAlphaSaturated is the one with an asymmetry --
//! GL only allows it as a SOURCE factor -- and the caller below is where that
//! is handled, since this function cannot see which side it is on.
//////////////////////////////////////////////////////////////////////////
static GLenum ToGLFactor(ECryBlendFactor eFactor)
{
	switch (eFactor)
	{
	case eCryBlend_Zero:				return GL_ZERO;
	case eCryBlend_One:					return GL_ONE;
	case eCryBlend_SrcColor:			return GL_SRC_COLOR;
	case eCryBlend_OneMinusSrcColor:	return GL_ONE_MINUS_SRC_COLOR;
	case eCryBlend_DstColor:			return GL_DST_COLOR;
	case eCryBlend_OneMinusDstColor:	return GL_ONE_MINUS_DST_COLOR;
	case eCryBlend_SrcAlpha:			return GL_SRC_ALPHA;
	case eCryBlend_OneMinusSrcAlpha:	return GL_ONE_MINUS_SRC_ALPHA;
	case eCryBlend_DstAlpha:			return GL_DST_ALPHA;
	case eCryBlend_OneMinusDstAlpha:	return GL_ONE_MINUS_DST_ALPHA;
	case eCryBlend_SrcAlphaSaturated:	return GL_SRC_ALPHA_SATURATE;
	default:							return GL_ONE;
	}
}

//////////////////////////////////////////////////////////////////////////

static GLenum ToGLCompare(ECryCompare eCompare)
{
	switch (eCompare)
	{
	case eCryCompare_Never:		return GL_NEVER;
	case eCryCompare_Less:		return GL_LESS;
	case eCryCompare_Equal:		return GL_EQUAL;
	case eCryCompare_LessEqual:	return GL_LEQUAL;
	case eCryCompare_Greater:	return GL_GREATER;
	case eCryCompare_Always:	return GL_ALWAYS;
	default:					return GL_LEQUAL;
	}
}

//////////////////////////////////////////////////////////////////////////

void GLESState_Apply(int nRenderState)
{
	if (g_bHaveState && nRenderState == g_nLastState)
		return;

	SCryStateDesc state;
	CryStateGen_Decode(nRenderState, state);

	// The decoder never fails, but it does report what it could not translate
	// exactly. Logged once per distinct state rather than per draw, which is
	// what the cache above makes possible.
	const char* szWarning = CryStateGen_LastWarning();
	if (szWarning && szWarning[0])
	{
		iLog->LogWarning("XRenderGLES: render state 0x%08x: %s",
		                 (unsigned)nRenderState, szWarning);
	}

	//////////////////////////////////////////////////////////////////////
	// Blending.
	//////////////////////////////////////////////////////////////////////
	if (state.bBlendEnabled)
	{
		glEnable(GL_BLEND);

		GLenum eSrc = ToGLFactor(state.eSrcFactor);
		GLenum eDst = ToGLFactor(state.eDstFactor);

		// GL_SRC_ALPHA_SATURATE is a source-only factor in GLES 3.0. The
		// engine's word has one nibble per side and nothing stops it naming
		// saturate as the destination, so clamp rather than let the driver
		// raise GL_INVALID_ENUM and drop the whole call -- which would leave
		// the PREVIOUS blend function in force and draw something arbitrary.
		if (eDst == GL_SRC_ALPHA_SATURATE)
		{
			iLog->LogWarning("XRenderGLES: render state 0x%08x names "
			                 "alpha-saturate as the destination factor, which "
			                 "GLES has only as a source; using GL_ONE",
			                 (unsigned)nRenderState);
			eDst = GL_ONE;
		}

		// One pair for both colour and alpha: the engine predates separate
		// alpha blending and has no second pair to give.
		glBlendFunc(eSrc, eDst);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	//////////////////////////////////////////////////////////////////////
	// Depth.
	//
	// Test and write are independent, and the engine expresses them as one
	// negative flag and one positive one. Either read backwards gives a scene
	// that renders but sorts wrongly, which reads as a camera bug.
	//////////////////////////////////////////////////////////////////////
	if (state.bDepthTest)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);

	glDepthFunc(ToGLCompare(state.eDepthCompare));
	glDepthMask(state.bDepthWrite ? GL_TRUE : GL_FALSE);

	//////////////////////////////////////////////////////////////////////
	// Colour mask.
	//////////////////////////////////////////////////////////////////////
	glColorMask((state.nColorWriteMask & 1) ? GL_TRUE : GL_FALSE,
	            (state.nColorWriteMask & 2) ? GL_TRUE : GL_FALSE,
	            (state.nColorWriteMask & 4) ? GL_TRUE : GL_FALSE,
	            (state.nColorWriteMask & 8) ? GL_TRUE : GL_FALSE);

	//////////////////////////////////////////////////////////////////////
	// The alpha test is NOT applied here. GLES 3.0 has no alpha-test state --
	// it went with the rest of the fixed-function pipeline -- so it is compiled
	// into the fragment shader as a discard instead, from the same decoded
	// description. See CryPass_SetAlphaTestFromRenderState.
	//
	// Stencil is decoded and not applied either: nothing in this backend
	// creates a stencil buffer yet, and enabling the test against an absent
	// attachment would discard everything.
	//////////////////////////////////////////////////////////////////////

	g_nLastState = nRenderState;
	g_bHaveState = true;
}

void GLESState_Invalidate()
{
	g_bHaveState = false;
}

int GLESState_Current()
{
	return g_nLastState;
}

#endif //__EMSCRIPTEN__
