////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_glesshadergen.cpp
//  Description: Tests for the CryEngine-pass -> GLSL ES 3.00 translation.
//
//  WHY THIS SUITE IS NOT A COPY OF THE WGSL ONE
//
//  The two generators now share their operation table -- what eCO_MODULATE
//  does, what stage 0's "previous" is, which operations are refused -- so
//  re-asserting all of that here would test the same code twice and prove
//  nothing about this file.
//
//  What is genuinely this file's own is the GLSL boilerplate: the version
//  directive, the precision qualifier, the sampler uniforms, the varyings, and
//  splitting the result into two sources because GL compiles the stages
//  separately. That is what is checked here, plus two things worth holding
//  down from both sides:
//
//    * the emitted GLSL is structurally valid, using the same checks that
//      caught the WGSL generator emitting one "texel" per stage;
//
//    * the two languages AGREE. Given one description they must apply the same
//      operation to the same arguments. That is the claim the sharing exists to
//      make, and it is the one that would quietly stop being true if someone
//      special-cased one backend.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <IShader.h>

#include "GLESShaderGen.h"
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

static const int kNumGLSLKeywords =
	(int)(sizeof(kGLSLDeclKeywords) / sizeof(kGLSLDeclKeywords[0]));

//////////////////////////////////////////////////////////////////////////
//! Structural validity. See shader_structure.h.
//////////////////////////////////////////////////////////////////////////
static void CheckWellFormed(const std::string& sGLSL, const char* szWhat)
{
	char what[192];
	std::string sName;

	const bool bUnique = CryTest_DeclarationsAreUnique(
		sGLSL, kGLSLDeclKeywords, kNumGLSLKeywords, sName);
	snprintf(what, sizeof(what), "%s: no identifier is declared twice in one scope%s%s",
	         szWhat, bUnique ? "" : " -- duplicate: ", bUnique ? "" : sName.c_str());
	CHECK(bUnique, what);

	const bool bBound = CryTest_GeneratedLocalsAreDeclared(
		sGLSL, kGLSLDeclKeywords, kNumGLSLKeywords, sName);
	snprintf(what, sizeof(what), "%s: every generated local is declared%s%s",
	         szWhat, bBound ? "" : " -- undeclared: ", bBound ? "" : sName.c_str());
	CHECK(bBound, what);
}

//! The pass the engine draws most: one texture modulated by vertex colour.
static SCryPassDesc ModulateDesc()
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_MODULATE;
	desc.stages[0].nColorArg = DEF_TEXARG0;
	desc.stages[0].nAlphaOp  = eCO_MODULATE;
	desc.stages[0].nAlphaArg = DEF_TEXARG0;
	return desc;
}

//////////////////////////////////////////////////////////////////////////
//! The boilerplate GLES needs and WGSL does not.
//////////////////////////////////////////////////////////////////////////
static void TestGLSLPreamble()
{
	std::string sVS, sFS, sError;
	CHECK(GLESShaderGen_Build(ModulateDesc(), sVS, sFS, sError),
	      "a modulate pass builds");

	// Both stages need the version directive, and it has to be the FIRST line:
	// GLSL ES rejects anything before it, comments included.
	CHECK(sVS.compare(0, 15, "#version 300 es") == 0,
	      "the vertex source opens with the version directive");
	CHECK(sFS.compare(0, 15, "#version 300 es") == 0,
	      "the fragment source opens with the version directive");

	// Fragment shaders have no default float precision in GLES 3.0. Without
	// this the shader does not compile at all.
	CHECK(Has(sFS, "precision mediump float;"),
	      "the fragment source declares a float precision");

	CHECK(Has(sVS, "layout(location = 0) in vec3 aPosition;"), "binds position to 0");
	CHECK(Has(sVS, "layout(location = 1) in vec4 aColor;"),    "binds colour to 1");
	CHECK(Has(sVS, "layout(location = 2) in vec2 aTexCoord;"), "binds texcoord to 2");

	CHECK(Has(sVS, "uniform mat4 uMVP;"),  "the vertex stage takes a transform");
	CHECK(Has(sVS, "gl_Position = uMVP"),  "the transform is applied");

	// The engine stores UCol as B,G,R,A and GLES 3.0 has no BGRA vertex format,
	// so without this every colour comes out with red and blue swapped.
	CHECK(Has(sVS, "vColor = aColor.bgra;"), "swizzles the BGRA vertex colour");

	CHECK(Has(sFS, "uniform sampler2D uTex0;"), "declares stage 0's sampler");
	CHECK(Has(sFS, "texture(uTex0, vTexCoord)"), "samples stage 0");
	CHECK(Has(sFS, "out vec4 oColor;"), "declares a fragment output");
	CHECK(Has(sFS, "oColor = acc;"),    "writes the accumulator out");

	// The varyings have to match across the two sources or the program does not
	// link -- and a link failure is the one error that names neither stage.
	CHECK(Has(sVS, "out vec4 vColor;") && Has(sFS, "in vec4 vColor;"),
	      "the colour varying matches across the two stages");
	CHECK(Has(sVS, "out vec2 vTexCoord;") && Has(sFS, "in vec2 vTexCoord;"),
	      "the texcoord varying matches across the two stages");

	CheckWellFormed(sFS, "modulate fragment stage");

	if (g_nFailures == 0)
		printf("\n--- emitted GLSL ES for a single modulate stage ---\n%s\n%s\n",
		       sVS.c_str(), sFS.c_str());
}

//////////////////////////////////////////////////////////////////////////
//! Each textured stage gets its own sampler, and the untextured ones get none.
//////////////////////////////////////////////////////////////////////////
static void TestSamplersPerStage()
{
	SCryPassDesc desc;
	desc.nStages = 3;

	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = eCO_REPLACE;
	desc.stages[0].nColorArg = eCA_Texture;
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;

	// No texture on stage 1. It still runs, against white.
	desc.stages[1].bHasTexture = false;
	desc.stages[1].nColorOp  = eCO_MODULATE;
	desc.stages[1].nColorArg = eCA_Previous | (eCA_Constant << 3);
	desc.stages[1].nAlphaOp  = eCO_REPLACE;
	desc.stages[1].nAlphaArg = eCA_Previous;

	desc.stages[2].bHasTexture = true;
	desc.stages[2].nColorOp  = eCO_ADD;
	desc.stages[2].nColorArg = eCA_Texture | (eCA_Previous << 3);
	desc.stages[2].nAlphaOp  = eCO_REPLACE;
	desc.stages[2].nAlphaArg = eCA_Previous;

	std::string sVS, sFS, sError;
	CHECK(GLESShaderGen_Build(desc, sVS, sFS, sError), "a three-stage pass builds");

	CHECK(Has(sFS, "uniform sampler2D uTex0;"), "stage 0 has a sampler");
	CHECK(!Has(sFS, "uniform sampler2D uTex1;"),
	      "the untextured stage declares no sampler");
	CHECK(Has(sFS, "uniform sampler2D uTex2;"), "stage 2 has a sampler");

	// The sampler name the backend will look up must be the one emitted, or
	// the texture binds to nothing and the stage samples black.
	CHECK(GLESShaderGen_SamplerName(2) == "uTex2",
	      "the sampler name the backend resolves matches the one emitted");

	CHECK(Has(sFS, "uConstColor"), "the constant colour reaches the shader");

	// This is the case that broke the WGSL generator: three stages, three
	// texels, one scope.
	CheckWellFormed(sFS, "three stages");
}

//////////////////////////////////////////////////////////////////////////
//! The alpha test becomes a discard here too, with its direction intact.
//////////////////////////////////////////////////////////////////////////
static void TestAlphaTestBecomesDiscard()
{
	SCryPassDesc desc = ModulateDesc();
	desc.nAlphaTest = eCryAlphaTest_GreaterEqual;
	desc.fAlphaRef  = 0.5f;

	std::string sVS, sFS, sError;
	CHECK(GLESShaderGen_Build(desc, sVS, sFS, sError), "an alpha-tested pass builds");
	CHECK(Has(sFS, "discard;"), "the alpha test emits a discard");
	CHECK(Has(sFS, "acc.a >= 0.5"), "the threshold and direction survive");

	// GLES 3.0 has an alpha test in neither the API nor the shader, so the
	// discard is the only expression of it. It must not also appear as state.
	desc.nAlphaTest = eCryAlphaTest_Less;
	CHECK(GLESShaderGen_Build(desc, sVS, sFS, sError), "a less-than test builds");
	CHECK(Has(sFS, "acc.a < 0.5"), "a less-than test compares the other way");
	CHECK(!Has(sFS, "acc.a >= 0.5"), "a less-than test is not emitted as >=");
}

//////////////////////////////////////////////////////////////////////////
//! Unsupported operations are refused here too, not approximated.
//////////////////////////////////////////////////////////////////////////
static void TestUnsupportedOpsAreRejected()
{
	SCryPassDesc desc = ModulateDesc();
	desc.stages[0].nColorOp = eCO_BUMPENVMAP;

	std::string sVS, sFS, sError;
	CHECK(!GLESShaderGen_Build(desc, sVS, sFS, sError),
	      "eCO_BUMPENVMAP is rejected rather than approximated");
	CHECK(sError.find("stage 0") != std::string::npos,
	      "the error names the stage");
}

//////////////////////////////////////////////////////////////////////////
//! The two languages must agree.
//!
//! This is the claim the shared operation table exists to make. If someone
//! special-cases one backend, the two stop computing the same thing -- and
//! because WebGPU cannot be run in this container, the divergence would only
//! show up on hardware nobody has in front of them.
//!
//! Compared on the SHAPE of the expression rather than the exact text, since
//! the two languages legitimately spell constructors and sampling differently.
//////////////////////////////////////////////////////////////////////////
static void TestBothLanguagesAgree()
{
	struct SCase
	{
		int			nOp;
		const char*	szExpect;	//!< the operator applied, in both languages
		const char*	szWhat;
	};

	static const SCase kCases[] =
	{
		{ eCO_MODULATE,   "(texel0 * diffuse)",       "modulate"   },
		{ eCO_ADD,        "(texel0 + diffuse)",       "add"        },
		{ eCO_SUBTRACT,   "(texel0 - diffuse)",       "subtract"   },
		{ eCO_MODULATE2X, "(texel0 * diffuse * 2.0)", "modulate2x" },
		{ eCO_ADDSIGNED,  "(texel0 + diffuse - 0.5)", "addsigned"  },
		{ eCO_REPLACE,    "texel0",                   "replace"    },
		{ eCO_ARG2,       "diffuse",                  "arg2"       },
	};

	for (int i = 0; i < (int)(sizeof(kCases) / sizeof(kCases[0])); ++i)
	{
		SCryPassDesc desc = ModulateDesc();
		desc.stages[0].nColorOp  = kCases[i].nOp;
		desc.stages[0].nColorArg = DEF_TEXARG0;	// texture, then diffuse

		std::string sWGSL, sVS, sFS, sError;
		const bool bW = WGPUShaderGen_Build(desc, sWGSL, sError);
		const bool bG = GLESShaderGen_Build(desc, sVS, sFS, sError);

		char what[192];
		snprintf(what, sizeof(what), "%s: both languages accept it", kCases[i].szWhat);
		CHECK(bW && bG, what);

		snprintf(what, sizeof(what), "%s: both apply the same operation", kCases[i].szWhat);
		CHECK(Has(sWGSL, kCases[i].szExpect) && Has(sFS, kCases[i].szExpect), what);
	}

	// And they must refuse the same things. A backend that quietly accepted an
	// operation the other rejects would render differently rather than fail.
	static const int kUnsupported[] = { eCO_MULTIPLYADD, eCO_BUMPENVMAP, eCO_BLEND };

	for (int i = 0; i < 3; ++i)
	{
		SCryPassDesc desc = ModulateDesc();
		desc.stages[0].nColorOp = kUnsupported[i];

		std::string sWGSL, sVS, sFS, sErrW, sErrG;
		const bool bW = WGPUShaderGen_Build(desc, sWGSL, sErrW);
		const bool bG = GLESShaderGen_Build(desc, sVS, sFS, sErrG);

		char what[192];
		snprintf(what, sizeof(what), "%s is refused by both languages",
		         CryPass_OpName(kUnsupported[i]));
		CHECK(!bW && !bG, what);

		snprintf(what, sizeof(what), "%s gives the same reason in both",
		         CryPass_OpName(kUnsupported[i]));
		CHECK(sErrW == sErrG, what);
	}
}

int main()
{
	TestGLSLPreamble();
	TestSamplersPerStage();
	TestAlphaTestBecomesDiscard();
	TestUnsupportedOpsAreRejected();
	TestBothLanguagesAgree();

	if (g_nFailures)
	{
		printf("\n%d failure(s)\n", g_nFailures);
		return 1;
	}
	printf("\nglesshadergen: all tests passed\n");
	return 0;
}
