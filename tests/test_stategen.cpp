////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_stategen.cpp
//  Description: Tests for GS_* render state -> pipeline state.
//
//  Like the shader generator, this is a pure function and so is checkable here
//  even though nothing that touches a GPU is. It is worth checking carefully:
//  the render state is one integer carrying eleven different things, several of
//  them negative flags, and getting one backwards produces a scene that renders
//  but is subtly and confusingly wrong -- geometry sorting backwards, or
//  cut-out foliage inverted.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <IRenderer.h>

#include "WGPUStateGen.h"

#include <stdio.h>

static int g_nFailures = 0;

#define CHECK(cond, what) \
	do { \
		if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); ++g_nFailures; } \
		else         { printf("ok  : %s\n", what); } \
	} while (0)

//////////////////////////////////////////////////////////////////////////
//! Zero is absence, not a factor.
//!
//! GS_BLSRC_ZERO is 0x1, not 0x0. An empty nibble means "this pass does not
//! blend" -- so treating 0 as the ZERO factor would turn every opaque surface
//! in the game into one that multiplies itself by nothing.
//////////////////////////////////////////////////////////////////////////
static void TestBlendAbsenceIsNotZeroFactor()
{
	SWGPUStateDesc st;

	WGPUStateGen_Decode(0, st);
	CHECK(!st.bBlendEnabled, "a state with no blend nibbles does not blend");

	// Only one side named: still not a blend.
	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA, st);
	CHECK(!st.bBlendEnabled, "a source factor alone does not enable blending");

	WGPUStateGen_Decode(GS_BLDST_ONEMINUSSRCALPHA, st);
	CHECK(!st.bBlendEnabled, "a destination factor alone does not enable blending");

	// And the real ZERO factor is a factor.
	WGPUStateGen_Decode(GS_BLSRC_ZERO | GS_BLDST_ONE, st);
	CHECK(st.bBlendEnabled, "GS_BLSRC_ZERO is a factor and does enable blending");
	CHECK(st.eSrcFactor == eWGPUBlend_Zero, "GS_BLSRC_ZERO decodes to the zero factor");
	CHECK(st.eDstFactor == eWGPUBlend_One,  "GS_BLDST_ONE decodes to the one factor");
}

//////////////////////////////////////////////////////////////////////////
//! The ordinary alpha blend, which most transparent surfaces use.
//////////////////////////////////////////////////////////////////////////
static void TestAlphaBlend()
{
	SWGPUStateDesc st;
	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, st);

	CHECK(st.bBlendEnabled, "src-alpha / one-minus-src-alpha blends");
	CHECK(st.eSrcFactor == eWGPUBlend_SrcAlpha, "source is src alpha");
	CHECK(st.eDstFactor == eWGPUBlend_OneMinusSrcAlpha, "destination is one minus src alpha");
}

//////////////////////////////////////////////////////////////////////////
//! Additive blending, used for every glow and particle in the game.
//////////////////////////////////////////////////////////////////////////
static void TestAdditiveBlend()
{
	SWGPUStateDesc st;
	WGPUStateGen_Decode(GS_BLSRC_ONE | GS_BLDST_ONE, st);

	CHECK(st.bBlendEnabled, "one / one blends");
	CHECK(st.eSrcFactor == eWGPUBlend_One, "additive source factor");
	CHECK(st.eDstFactor == eWGPUBlend_One, "additive destination factor");
}

//////////////////////////////////////////////////////////////////////////
//! Depth. GS_NODEPTHTEST is a NEGATIVE flag and GS_DEPTHWRITE is a positive
//! one, which is easy to read the wrong way round -- and doing so makes
//! geometry sort backwards, which looks like a camera bug.
//////////////////////////////////////////////////////////////////////////
static void TestDepth()
{
	SWGPUStateDesc st;

	WGPUStateGen_Decode(0, st);
	CHECK(st.bDepthTest, "depth testing is on by default");
	CHECK(!st.bDepthWrite, "depth writing is off unless asked for");
	CHECK(st.eDepthCompare == eWGPUCompare_LessEqual, "the default comparison is less-or-equal");

	WGPUStateGen_Decode(GS_DEPTHWRITE, st);
	CHECK(st.bDepthWrite, "GS_DEPTHWRITE turns depth writing on");

	WGPUStateGen_Decode(GS_NODEPTHTEST, st);
	CHECK(!st.bDepthTest, "GS_NODEPTHTEST turns depth testing off");
	// WebGPU cannot disable the test; the equivalent is a comparison that
	// always passes.
	CHECK(st.eDepthCompare == eWGPUCompare_Always,
	      "a disabled depth test becomes an always-pass comparison");

	WGPUStateGen_Decode(GS_DEPTHFUNC_EQUAL, st);
	CHECK(st.eDepthCompare == eWGPUCompare_Equal, "GS_DEPTHFUNC_EQUAL decodes");

	WGPUStateGen_Decode(GS_DEPTHFUNC_GREAT, st);
	CHECK(st.eDepthCompare == eWGPUCompare_Greater, "GS_DEPTHFUNC_GREAT decodes");
}

//////////////////////////////////////////////////////////////////////////
//! Colour write masking. R=1 G=2 B=4 A=8.
//////////////////////////////////////////////////////////////////////////
static void TestColorMask()
{
	SWGPUStateDesc st;

	WGPUStateGen_Decode(0, st);
	CHECK(st.nColorWriteMask == (1 | 2 | 4 | 8), "everything is written by default");

	WGPUStateGen_Decode(GS_NOCOLMASK, st);
	CHECK(st.nColorWriteMask == 0, "GS_NOCOLMASK writes nothing");

	WGPUStateGen_Decode(GS_COLMASKONLYALPHA, st);
	CHECK(st.nColorWriteMask == 8, "GS_COLMASKONLYALPHA writes alpha only");

	WGPUStateGen_Decode(GS_COLMASKONLYRGB, st);
	CHECK(st.nColorWriteMask == (1 | 2 | 4), "GS_COLMASKONLYRGB writes colour only");
}

//////////////////////////////////////////////////////////////////////////
//! The alpha test, and above all its DIRECTION.
//!
//! GS_ALPHATEST_LESS128 keeps fragments below the threshold; the others keep
//! fragments above it. Collapsing the two would invert every surface using the
//! former -- cut-out foliage would render as the holes.
//////////////////////////////////////////////////////////////////////////
static void TestAlphaTest()
{
	SWGPUStateDesc st;

	WGPUStateGen_Decode(0, st);
	CHECK(st.eAlphaTest == eWGPUAlphaTest_None, "no alpha test by default");

	WGPUStateGen_Decode(GS_ALPHATEST_GREATER0, st);
	CHECK(st.eAlphaTest == eWGPUAlphaTest_Greater, "GREATER0 keeps anything above zero");
	CHECK(st.fAlphaRef == 0.0f, "GREATER0 has a zero reference");

	WGPUStateGen_Decode(GS_ALPHATEST_GEQUAL128, st);
	CHECK(st.eAlphaTest == eWGPUAlphaTest_GreaterEqual, "GEQUAL128 is a >= test");
	CHECK(st.fAlphaRef > 0.50f && st.fAlphaRef < 0.51f, "GEQUAL128 references 128/255");

	WGPUStateGen_Decode(GS_ALPHATEST_GEQUAL64, st);
	CHECK(st.eAlphaTest == eWGPUAlphaTest_GreaterEqual, "GEQUAL64 is a >= test");
	CHECK(st.fAlphaRef > 0.25f && st.fAlphaRef < 0.26f, "GEQUAL64 references 64/255");

	WGPUStateGen_Decode(GS_ALPHATEST_LESS128, st);
	CHECK(st.eAlphaTest == eWGPUAlphaTest_Less, "LESS128 is a LESS-than test, not a >=");
	CHECK(st.fAlphaRef > 0.50f && st.fAlphaRef < 0.51f, "LESS128 references 128/255");
}

//////////////////////////////////////////////////////////////////////////
//! Combinations, since a real render state carries several of these at once.
//////////////////////////////////////////////////////////////////////////
static void TestCombined()
{
	SWGPUStateDesc st;

	// A typical foliage pass: alpha tested, depth written, no blending.
	WGPUStateGen_Decode(GS_DEPTHWRITE | GS_ALPHATEST_GEQUAL128, st);
	CHECK(!st.bBlendEnabled, "an alpha-tested pass need not blend");
	CHECK(st.bDepthWrite, "and still writes depth");
	CHECK(st.eAlphaTest == eWGPUAlphaTest_GreaterEqual, "and still tests alpha");

	// A typical particle pass: additive, depth tested but not written.
	WGPUStateGen_Decode(GS_BLSRC_ONE | GS_BLDST_ONE, st);
	CHECK(st.bBlendEnabled && !st.bDepthWrite,
	      "an additive pass blends without writing depth");

	WGPUStateGen_Decode(GS_STENCIL, st);
	CHECK(st.bStencil, "GS_STENCIL is carried through");
}

//////////////////////////////////////////////////////////////////////////
//! Every field must reach the pipeline key. Two states that produce different
//! pipelines sharing a key means one pass renders with the other's state.
//////////////////////////////////////////////////////////////////////////
static void TestKey()
{
	SWGPUStateDesc a, b;

	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, a);
	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, b);
	CHECK(WGPUStateGen_Key(a) == WGPUStateGen_Key(b), "the same state gives the same key");

	WGPUStateGen_Decode(GS_BLSRC_ONE | GS_BLDST_ONE, b);
	CHECK(WGPUStateGen_Key(a) != WGPUStateGen_Key(b), "different blending changes the key");

	WGPUStateGen_Decode(GS_DEPTHWRITE, a);
	WGPUStateGen_Decode(0, b);
	CHECK(WGPUStateGen_Key(a) != WGPUStateGen_Key(b), "depth writing changes the key");

	WGPUStateGen_Decode(GS_ALPHATEST_GEQUAL128, a);
	WGPUStateGen_Decode(GS_ALPHATEST_GEQUAL64, b);
	CHECK(WGPUStateGen_Key(a) != WGPUStateGen_Key(b),
	      "a different alpha reference changes the key");

	WGPUStateGen_Decode(GS_ALPHATEST_GEQUAL128, a);
	WGPUStateGen_Decode(GS_ALPHATEST_LESS128, b);
	CHECK(WGPUStateGen_Key(a) != WGPUStateGen_Key(b),
	      "the same reference with the opposite direction changes the key");

	WGPUStateGen_Decode(GS_NOCOLMASK, a);
	WGPUStateGen_Decode(0, b);
	CHECK(WGPUStateGen_Key(a) != WGPUStateGen_Key(b), "colour masking changes the key");
}

int main()
{
	TestBlendAbsenceIsNotZeroFactor();
	TestAlphaBlend();
	TestAdditiveBlend();
	TestDepth();
	TestColorMask();
	TestAlphaTest();
	TestCombined();
	TestKey();

	if (g_nFailures)
	{
		printf("\n%d failure(s)\n", g_nFailures);
		return 1;
	}
	printf("\nstategen: all tests passed\n");
	return 0;
}
