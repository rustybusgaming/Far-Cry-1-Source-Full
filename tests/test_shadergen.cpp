////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_shadergen.cpp
//  Description: Tests for the CryEngine-pass -> WGSL translation.
//
//  WHY THESE MATTER MORE THAN THEY LOOK
//
//  Nothing else in the WebGPU backend can be verified here: the container
//  cannot obtain a WebGPU adapter, so no shader can be compiled by a driver and
//  no pixel can be read back. But the translation is a pure function -- a stage
//  description in, WGSL out -- and that is where all the decisions live. Every
//  judgement about what eCO_DOTPRODUCT3 means, what stage 0's "previous" is,
//  which operations cannot be expressed at all: all of it is checked here,
//  natively, with no GPU involved.
//
//  What remains unverified after this is the mechanical part -- handing the
//  string to a driver and binding the right resources.
//
//  These assert the SHAPE of the emitted code, not its exact text. Asserting
//  the whole string would fail on every whitespace change and teach nobody
//  anything; asserting that a modulate stage multiplies its two arguments is
//  the actual claim.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <IShader.h>

#include "WGPUShaderGen.h"

#include "shader_structure.h"

#include <stdio.h>
#include <string.h>
#include <string>

static int g_nFailures = 0;

#define CHECK(cond, what) \
	do { \
		if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); ++g_nFailures; } \
		else         { printf("ok  : %s\n", what); } \
	} while (0)

static bool Has(const std::string& s, const char* szNeedle)
{
	return s.find(szNeedle) != std::string::npos;
}

//////////////////////////////////////////////////////////////////////////
//! Structural validity, on one shader. Called by every test that builds one.
//!
//! The checks themselves are in shader_structure.h, shared with the GLSL ES
//! generator's tests -- both generators emit their declarations from the same
//! shared code, so a bug in it has to be catchable from either side. That
//! header says what the checks are, and which real failure put them there.
//////////////////////////////////////////////////////////////////////////
static void CheckWellFormed(const std::string& sWGSL, const char* szWhat)
{
	char what[192];
	std::string sName;

	const bool bUnique = CryTest_DeclarationsAreUnique(
		sWGSL, kWGSLDeclKeywords, 2, sName);
	snprintf(what, sizeof(what), "%s: no identifier is declared twice in one scope%s%s",
	         szWhat, bUnique ? "" : " -- duplicate: ", bUnique ? "" : sName.c_str());
	CHECK(bUnique, what);

	const bool bBound = CryTest_GeneratedLocalsAreDeclared(
		sWGSL, kWGSLDeclKeywords, 2, sName);
	snprintf(what, sizeof(what), "%s: every generated local is declared%s%s",
	         szWhat, bBound ? "" : " -- undeclared: ", bBound ? "" : sName.c_str());
	CHECK(bBound, what);
}

//////////////////////////////////////////////////////////////////////////
//! The engine packs two 3-bit arguments into one int, and masks them with ~7
//! and ~(7<<3). Getting the shift wrong would silently swap every stage's
//! arguments, which for a non-commutative operation is a real difference.
//////////////////////////////////////////////////////////////////////////
static void TestArgPacking()
{
	const int nPacked = DEF_TEXARG0;	// eCA_Texture | (eCA_Diffuse << 3)

	CHECK(CryPass_Arg0(nPacked) == eCA_Texture,  "DEF_TEXARG0 arg0 is the texture");
	CHECK(CryPass_Arg1(nPacked) == eCA_Diffuse,  "DEF_TEXARG0 arg1 is the diffuse colour");

	const int nPacked1 = DEF_TEXARG1;	// eCA_Texture | (eCA_Previous << 3)
	CHECK(CryPass_Arg0(nPacked1) == eCA_Texture,  "DEF_TEXARG1 arg0 is the texture");
	CHECK(CryPass_Arg1(nPacked1) == eCA_Previous, "DEF_TEXARG1 arg1 is the previous stage");
}

//////////////////////////////////////////////////////////////////////////
//! The commonest pass in the engine: one texture modulated by vertex colour.
//////////////////////////////////////////////////////////////////////////
static void TestSingleModulateStage()
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_MODULATE;
	desc.stages[0].nColorArg = DEF_TEXARG0;
	desc.stages[0].nAlphaOp  = eCO_MODULATE;
	desc.stages[0].nAlphaArg = DEF_TEXARG0;

	std::string sWGSL, sError;
	CHECK(WGPUShaderGen_Build(desc, sWGSL, sError), "single modulate stage builds");

	CHECK(Has(sWGSL, "@vertex"),   "emits a vertex stage");
	CHECK(Has(sWGSL, "@fragment"), "emits a fragment stage");

	// The texture must actually be declared, sampled, and multiplied by the
	// diffuse colour.
	CHECK(Has(sWGSL, "texture_2d<f32>"),  "declares a texture");
	CHECK(Has(sWGSL, "textureSample(tex0, samp0, in.uv)"), "samples stage 0");
	CHECK(Has(sWGSL, "(texel0 * diffuse)"), "modulates texture by diffuse");

	// The vertex colour swizzle. The engine stores UCol as B,G,R,A and neither
	// WebGL2 nor WebGPU has a BGRA vertex format, so this has to be in the
	// shader or every colour comes out with red and blue swapped.
	CHECK(Has(sWGSL, "color.bgra"), "swizzles the BGRA vertex colour");

	CheckWellFormed(sWGSL, "single modulate stage");

	if (g_nFailures == 0)
		printf("\n--- emitted WGSL for a single modulate stage ---\n%s\n", sWGSL.c_str());
}

//////////////////////////////////////////////////////////////////////////
//! Stage 0's "previous" is the diffuse colour, not black or white. That is
//! what makes a lone eCO_MODULATE with eCA_Previous behave correctly.
//////////////////////////////////////////////////////////////////////////
static void TestPreviousStartsAsDiffuse()
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_MODULATE;
	desc.stages[0].nColorArg = eCA_Texture | (eCA_Previous << 3);
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;

	std::string sWGSL, sError;
	CHECK(WGPUShaderGen_Build(desc, sWGSL, sError), "previous-as-arg builds");
	CHECK(Has(sWGSL, "var acc = diffuse;"), "the accumulator starts at the diffuse colour");
	CHECK(Has(sWGSL, "(texel0 * acc)"), "stage 0 multiplies the texture by the accumulator");
	CheckWellFormed(sWGSL, "previous-as-argument");
}

//////////////////////////////////////////////////////////////////////////
//! A second stage has to chain off the first, and get its own texture.
//////////////////////////////////////////////////////////////////////////
static void TestTwoStagesChain()
{
	SCryPassDesc desc;
	desc.nStages = 2;

	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_REPLACE;
	desc.stages[0].nColorArg = eCA_Texture;
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;

	desc.stages[1].bHasTexture = true;
	desc.stages[1].nColorOp  = eCO_ADD;
	desc.stages[1].nColorArg = eCA_Texture | (eCA_Previous << 3);
	desc.stages[1].nAlphaOp  = eCO_REPLACE;
	desc.stages[1].nAlphaArg = eCA_Previous;

	std::string sWGSL, sError;
	CHECK(WGPUShaderGen_Build(desc, sWGSL, sError), "two stages build");

	CHECK(Has(sWGSL, "textureSample(tex0, samp0"), "stage 0 has its own texture");
	CHECK(Has(sWGSL, "textureSample(tex1, samp1"), "stage 1 has its own texture");
	CHECK(Has(sWGSL, "(texel1 + acc)"), "stage 1 adds to the accumulator");

	// Each stage's texel is a separate binding. One shared name was valid text
	// and invalid WGSL; see the note above CheckWellFormed.
	CHECK(Has(sWGSL, "let texel0 = textureSample(tex0, samp0, in.uv);"),
	      "stage 0's texel has its own name");
	CHECK(Has(sWGSL, "let texel1 = textureSample(tex1, samp1, in.uv);"),
	      "stage 1's texel has its own name");
	CheckWellFormed(sWGSL, "two chained stages");

	// Each texture needs its own binding pair, or the two stages sample the
	// same image.
	CHECK(Has(sWGSL, "@binding(1)") && Has(sWGSL, "@binding(2)")
	   && Has(sWGSL, "@binding(3)") && Has(sWGSL, "@binding(4)"),
	      "each stage gets its own sampler and texture bindings");
}

//////////////////////////////////////////////////////////////////////////
//! The operations that cannot be expressed must FAIL, by name. Guessing a
//! value for them would produce a shader that looks right and is not.
//////////////////////////////////////////////////////////////////////////
static void TestUnsupportedOpsAreRejected()
{
	const int kUnsupported[] = { eCO_MULTIPLYADD, eCO_BUMPENVMAP, eCO_BLEND };

	for (int i = 0; i < 3; ++i)
	{
		SCryPassDesc desc;
		desc.nStages = 1;
		desc.stages[0].bHasTexture = true;
		desc.stages[0].nColorOp  = kUnsupported[i];
		desc.stages[0].nColorArg = DEF_TEXARG0;
		desc.stages[0].nAlphaOp  = eCO_REPLACE;
		desc.stages[0].nAlphaArg = eCA_Texture;

		std::string sWGSL, sError;
		const bool bBuilt = WGPUShaderGen_Build(desc, sWGSL, sError);

		char what[160];
		snprintf(what, sizeof(what), "%s is rejected rather than approximated",
		         CryPass_OpName(kUnsupported[i]));
		CHECK(!bBuilt, what);

		// The error has to say which stage and which operation, or it is no
		// use when a real shader fails to build.
		snprintf(what, sizeof(what), "%s reports a useful reason",
		         CryPass_OpName(kUnsupported[i]));
		CHECK(!sError.empty() && sError.find("stage 0") != std::string::npos, what);
	}
}

//////////////////////////////////////////////////////////////////////////
//! A disabled stage contributes nothing, and is not an error.
//////////////////////////////////////////////////////////////////////////
static void TestDisabledStageSkipped()
{
	SCryPassDesc desc;
	desc.nStages = 2;

	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_REPLACE;
	desc.stages[0].nColorArg = eCA_Texture;
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;

	desc.stages[1].bHasTexture = true;
	desc.stages[1].nColorOp  = eCO_DISABLE;
	desc.stages[1].nAlphaOp  = eCO_DISABLE;

	std::string sWGSL, sError;
	CHECK(WGPUShaderGen_Build(desc, sWGSL, sError), "a disabled stage is not an error");
	CHECK(!Has(sWGSL, "textureSample(tex1"), "a disabled stage emits no sampling");
}

//////////////////////////////////////////////////////////////////////////
//! Alpha test becomes a discard, since WebGPU has no such render state.
//////////////////////////////////////////////////////////////////////////
static void TestAlphaTestBecomesDiscard()
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_REPLACE;
	desc.stages[0].nColorArg = eCA_Texture;
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;

	std::string sNoTest, sWithTest, sError;
	CHECK(WGPUShaderGen_Build(desc, sNoTest, sError), "builds without an alpha test");
	CHECK(!Has(sNoTest, "discard"), "no discard when no alpha test is asked for");

	desc.nAlphaTest = eCryAlphaTest_GreaterEqual;
	desc.fAlphaRef  = 0.5f;
	CHECK(WGPUShaderGen_Build(desc, sWithTest, sError), "builds with an alpha test");
	CHECK(Has(sWithTest, "discard"), "an alpha test emits a discard");
	CHECK(Has(sWithTest, "acc.a >= 0.5"), "a >= test emits a >= comparison");

	// The direction is the point. GS_ALPHATEST_LESS128 keeps fragments BELOW
	// the threshold; emitting the same comparison as the >= tests would invert
	// every surface that uses it.
	std::string sLess;
	desc.nAlphaTest = eCryAlphaTest_Less;
	CHECK(WGPUShaderGen_Build(desc, sLess, sError), "builds a less-than alpha test");
	CHECK(Has(sLess, "acc.a < 0.5"), "a less-than test emits a < comparison");
	CHECK(!Has(sLess, "acc.a >= 0.5"), "a less-than test is not emitted as >=");
}

//////////////////////////////////////////////////////////////////////////
//! The pipeline cache key. Two passes that emit different source MUST NOT
//! share a key, or one silently renders with the other's shader.
//////////////////////////////////////////////////////////////////////////
static void TestKeyDistinguishesDescriptions()
{
	SCryPassDesc a;
	a.nStages = 1;
	a.stages[0].bHasTexture = true;
	a.stages[0].nColorOp  = eCO_MODULATE;
	a.stages[0].nColorArg = DEF_TEXARG0;

	SCryPassDesc b = a;
	CHECK(CryPass_Key(a) == CryPass_Key(b), "identical descriptions share a key");

	b.stages[0].nColorOp = eCO_ADD;
	CHECK(CryPass_Key(a) != CryPass_Key(b), "a different operation changes the key");

	SCryPassDesc c = a;
	c.stages[0].nColorArg = DEF_TEXARG1;
	CHECK(CryPass_Key(a) != CryPass_Key(c), "different arguments change the key");

	SCryPassDesc d = a;
	d.nStages = 2;
	CHECK(CryPass_Key(a) != CryPass_Key(d), "a different stage count changes the key");

	SCryPassDesc e = a;
	e.nAlphaTest = eCryAlphaTest_GreaterEqual;
	e.fAlphaRef  = 0.5f;
	CHECK(CryPass_Key(a) != CryPass_Key(e), "an alpha test changes the key");

	// Same threshold, opposite direction: different shader, so different key.
	SCryPassDesc eLess = e;
	eLess.nAlphaTest = eCryAlphaTest_Less;
	CHECK(CryPass_Key(e) != CryPass_Key(eLess),
	      "the alpha test direction changes the key");

	SCryPassDesc f = a;
	f.stages[0].bHasTexture = false;
	CHECK(CryPass_Key(a) != CryPass_Key(f), "losing the texture changes the key");

	// The threshold is part of the emitted source, so two different thresholds
	// cannot share a pipeline either.
	SCryPassDesc g = e;
	g.fAlphaRef = 0.25f;
	CHECK(CryPass_Key(e) != CryPass_Key(g),
	      "a different alpha threshold changes the key");
}

//////////////////////////////////////////////////////////////////////////
//! A stage with no texture bound still runs, and anything reading the texture
//! gets white -- the identity for the multiplicative operations that dominate.
//////////////////////////////////////////////////////////////////////////
static void TestUntexturedStage()
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = false;
	desc.stages[0].nColorOp  = eCO_MODULATE;
	desc.stages[0].nColorArg = DEF_TEXARG0;
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Diffuse;

	std::string sWGSL, sError;
	CHECK(WGPUShaderGen_Build(desc, sWGSL, sError), "an untextured stage builds");
	CHECK(!Has(sWGSL, "texture_2d<f32>"), "an untextured stage declares no texture");
	CHECK(Has(sWGSL, "let texel0 = vec4f(1.0, 1.0, 1.0, 1.0);"),
	      "the texture argument reads as white");
	CheckWellFormed(sWGSL, "untextured stage");
}

int main()
{
	TestArgPacking();
	TestSingleModulateStage();
	TestPreviousStartsAsDiffuse();
	TestTwoStagesChain();
	TestUnsupportedOpsAreRejected();
	TestDisabledStageSkipped();
	TestAlphaTestBecomesDiscard();
	TestKeyDistinguishesDescriptions();
	TestUntexturedStage();

	if (g_nFailures)
	{
		printf("\n%d failure(s)\n", g_nFailures);
		return 1;
	}
	printf("\nshadergen: all tests passed\n");
	return 0;
}
