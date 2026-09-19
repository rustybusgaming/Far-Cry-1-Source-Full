////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESConform.cpp
//  Description: Running generated shaders through a real driver.
//
//  WHY THIS EXISTS
//
//  Everything about the texture-stage translation has been ASSERTED -- the
//  unit tests read the emitted string and check it says what it should. That
//  caught a lot, but it could not catch the one bug that mattered most: the
//  WGSL generator emitted "let texel = ..." once per stage into one scope, so
//  every multi-stage pass was invalid, and every substring assertion passed
//  anyway because the text was all present.
//
//  Structural checks were added for that. They are still not a compiler.
//
//  This is the compiler. Each case here builds a real program through the real
//  GLES driver, draws with it, and reads the pixel back -- so a shader that
//  does not compile fails, and a shader that compiles and computes the wrong
//  thing also fails. The expected colours are worked out by hand from what the
//  operation is DEFINED to do, not from what the generator emits, or the test
//  would only prove the generator agrees with itself.
//
//  It runs off-screen into its own framebuffer, so it neither disturbs the
//  frame the engine is drawing nor depends on anything the engine has set up.
//
//  WHAT IT IS EVIDENCE FOR
//
//  Both backends share their operation table (Common/CryPassGen.cpp), so a
//  case passing here is evidence about the WGSL path too -- which is the only
//  evidence that path can get in a container with no WebGPU adapter. What it
//  cannot check is anything above the shared layer: WGSL's own bindings and
//  entry points are still unverified, and honestly so.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESConform.h"

#if defined(__EMSCRIPTEN__)

#include "GLESShader.h"
#include "GLESShaderGen.h"
#include "GLESState.h"

#include <GLES3/gl3.h>

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
// The inputs. Chosen so every expected result lands on an exact byte rather
// than between two, which keeps the tolerance below about precision and not
// about arithmetic.
//
// 128 is 0.50196 rather than 0.5, so products are not quite the round numbers
// they look -- the expected values below are computed from the real
// normalised values, not from the pretty ones.
//////////////////////////////////////////////////////////////////////////

//! Stage 0's texture, RGBA. 0.50196, 0.25098, 0.12549, 1.0
static const unsigned char kTexA[4] = { 128, 64, 32, 255 };

//! Stage 1's texture. 1.0, 0.50196, 0.25098, 1.0
static const unsigned char kTexB[4] = { 255, 128, 64, 255 };

//! The vertex colour, in the engine's B,G,R,A packing. The shader swizzles
//! .bgra, so this reaches the fragment stage as 128,128,128,255 -- and if the
//! swizzle were dropped it would arrive as something else, which is the point
//! of not making the four bytes equal.
static const unsigned char kVertexBGRA[4] = { 128, 128, 128, 255 };

//! What the framebuffer is cleared to, so a discarded fragment is
//! distinguishable from a black one.
static const unsigned char kClear[4] = { 16, 16, 16, 255 };

//! mediump is not required to be more accurate than this, and the browser's
//! GL may itself be running on a software rasteriser.
static const int kTolerance = 2;

//////////////////////////////////////////////////////////////////////////

struct SConformCase
{
	const char*		szName;
	SCryPassDesc	desc;
	unsigned char	expect[4];
};

//! One texture, modulated by the vertex colour: the commonest pass there is.
//!
//!   0.50196 * 0.50196 = 0.25196 -> 64
//!   0.25098 * 0.50196 = 0.12598 -> 32
//!   0.12549 * 0.50196 = 0.06299 -> 16
static SConformCase CaseModulate()
{
	SConformCase c;
	c.szName = "one stage, modulate by vertex colour";

	c.desc.nStages = 1;
	c.desc.stages[0].bHasTexture = true;
	c.desc.stages[0].nColorOp  = eCO_MODULATE;
	c.desc.stages[0].nColorArg = DEF_TEXARG0;
	c.desc.stages[0].nAlphaOp  = eCO_MODULATE;
	c.desc.stages[0].nAlphaArg = DEF_TEXARG0;

	c.expect[0] = 64; c.expect[1] = 32; c.expect[2] = 16; c.expect[3] = 255;
	return c;
}

//! No texture bound. The stage still runs; eCA_Texture reads as white, so the
//! result is the vertex colour unchanged.
static SConformCase CaseUntextured()
{
	SConformCase c;
	c.szName = "one stage, no texture, reads as white";

	c.desc.nStages = 1;
	c.desc.stages[0].bHasTexture = false;
	c.desc.stages[0].nColorOp  = eCO_MODULATE;
	c.desc.stages[0].nColorArg = DEF_TEXARG0;
	c.desc.stages[0].nAlphaOp  = eCO_MODULATE;
	c.desc.stages[0].nAlphaArg = DEF_TEXARG0;

	c.expect[0] = 128; c.expect[1] = 128; c.expect[2] = 128; c.expect[3] = 255;
	return c;
}

//! THE CASE THAT WAS BROKEN.
//!
//! Two stages, each with its own texture. Stage 0 replaces with texture A,
//! stage 1 modulates texture B by what came before:
//!
//!   1.0     * 0.50196 = 0.50196 -> 128
//!   0.50196 * 0.25098 = 0.12598 -> 32
//!   0.25098 * 0.12549 = 0.03149 -> 8
//!
//! Under the old generator this pass declared "texel" twice in one scope and
//! did not compile at all. Nothing before this test would have noticed.
static SConformCase CaseTwoStageModulate()
{
	SConformCase c;
	c.szName = "two stages, second modulates the first";

	c.desc.nStages = 2;

	c.desc.stages[0].bHasTexture = true;
	c.desc.stages[0].nColorOp  = eCO_REPLACE;
	c.desc.stages[0].nColorArg = eCA_Texture;
	c.desc.stages[0].nAlphaOp  = eCO_REPLACE;
	c.desc.stages[0].nAlphaArg = eCA_Texture;

	c.desc.stages[1].bHasTexture = true;
	c.desc.stages[1].nColorOp  = eCO_MODULATE;
	c.desc.stages[1].nColorArg = eCA_Texture | (eCA_Previous << 3);
	c.desc.stages[1].nAlphaOp  = eCO_REPLACE;
	c.desc.stages[1].nAlphaArg = eCA_Previous;

	c.expect[0] = 128; c.expect[1] = 32; c.expect[2] = 8; c.expect[3] = 255;
	return c;
}

//! Two stages again, adding rather than multiplying -- so a generator that
//! mixed the two stages' texels up would give a different wrong answer here
//! than above, rather than both failing the same way.
//!
//!   1.0     + 0.50196 = 1.50196 -> clamped to 255
//!   0.50196 + 0.25098 = 0.75294 -> 192
//!   0.25098 + 0.12549 = 0.37647 -> 96
static SConformCase CaseTwoStageAdd()
{
	SConformCase c;
	c.szName = "two stages, second adds to the first";

	c.desc.nStages = 2;

	c.desc.stages[0].bHasTexture = true;
	c.desc.stages[0].nColorOp  = eCO_REPLACE;
	c.desc.stages[0].nColorArg = eCA_Texture;
	c.desc.stages[0].nAlphaOp  = eCO_REPLACE;
	c.desc.stages[0].nAlphaArg = eCA_Texture;

	c.desc.stages[1].bHasTexture = true;
	c.desc.stages[1].nColorOp  = eCO_ADD;
	c.desc.stages[1].nColorArg = eCA_Texture | (eCA_Previous << 3);
	c.desc.stages[1].nAlphaOp  = eCO_REPLACE;
	c.desc.stages[1].nAlphaArg = eCA_Previous;

	c.expect[0] = 255; c.expect[1] = 192; c.expect[2] = 96; c.expect[3] = 255;
	return c;
}

//! An alpha test the fragment PASSES. Alpha here is 1.0, so a >= 0.5 test
//! keeps it and the colour is the modulate case's.
static SConformCase CaseAlphaTestKeeps()
{
	SConformCase c = CaseModulate();
	c.szName = "alpha test that keeps the fragment";
	c.desc.nAlphaTest = eCryAlphaTest_GreaterEqual;
	c.desc.fAlphaRef  = 0.5f;
	return c;
}

//! The same fragment, tested the OTHER way. GS_ALPHATEST_LESS128 keeps
//! fragments BELOW the threshold, so alpha 1.0 is discarded and the clear
//! colour survives.
//!
//! This is the case that would have gone unnoticed: a generator that emitted
//! every alpha test as one direction still passes the case above, and inverts
//! every cut-out surface in the game.
static SConformCase CaseAlphaTestDiscards()
{
	SConformCase c = CaseModulate();
	c.szName = "alpha test that discards the fragment";
	c.desc.nAlphaTest = eCryAlphaTest_Less;
	c.desc.fAlphaRef  = 0.5f;

	c.expect[0] = kClear[0]; c.expect[1] = kClear[1];
	c.expect[2] = kClear[2]; c.expect[3] = kClear[3];
	return c;
}

//////////////////////////////////////////////////////////////////////////
// The three-argument operations.
//
// Every one of these was translated WRONGLY until the shipped Direct3D 9
// backend was read properly, and every one of them produced something
// plausible on screen while doing it. They are here because "plausible and
// wrong" is exactly what a pixel test catches and an assertion about emitted
// text does not.
//
// All four use eCA_Previous as their third argument rather than eCA_Constant.
// That is not arbitrary: the engine packs the third argument into a byte at
// bits 6-8, so eCA_Constant (4 << 6 = 256) cannot survive and no real shader
// can ask for it. Testing a configuration the engine cannot express would be
// testing nothing. At stage 0 "previous" is the diffuse colour.
//////////////////////////////////////////////////////////////////////////

//! Arguments shared by the four: texture, then diffuse, then previous.
static int ThreeArgs()
{
	return eCA_Texture | (eCA_Diffuse << 3) | (eCA_Previous << 6);
}

//! Shape shared by the four: one texture, alpha replaced by the texture's.
static SCryPassDesc ThreeArgDesc(int nColorOp)
{
	SCryPassDesc desc;
	desc.nStages = 1;
	desc.stages[0].bHasTexture = true;
	desc.stages[0].nColorOp  = nColorOp;
	desc.stages[0].nColorArg = ThreeArgs();
	desc.stages[0].nAlphaOp  = eCO_REPLACE;
	desc.stages[0].nAlphaArg = eCA_Texture;
	return desc;
}

//! D3DTOP_MULTIPLYADD: Arg1 + Arg2 * Arg0, which in the engine's packing is
//! first + second * third. Refused outright until the third argument was
//! found.
//!
//!   0.50196 + 0.50196 * 0.50196 = 0.75392 -> 192
//!   0.25098 + 0.50196 * 0.50196 = 0.50294 -> 128
//!   0.12549 + 0.50196 * 0.50196 = 0.37745 -> 96
static SConformCase CaseMultiplyAdd()
{
	SConformCase c;
	c.szName = "multiply-add, reading the third argument";
	c.desc   = ThreeArgDesc(eCO_MULTIPLYADD);

	c.expect[0] = 192; c.expect[1] = 128; c.expect[2] = 96; c.expect[3] = 255;
	return c;
}

//! D3DTOP_LERP: Arg0 * Arg1 + (1 - Arg0) * Arg2 -- the factor is the third
//! argument, a whole colour. This used to interpolate by the FIRST argument's
//! alpha, which with an opaque texture meant it returned that argument
//! unchanged and looked like a working select.
//!
//!   mix(0.50196, 0.50196, 0.50196) = 0.50196 -> 128
//!   mix(0.50196, 0.25098, 0.50196) = 0.37598 -> 96
//!   mix(0.50196, 0.12549, 0.50196) = 0.31299 -> 80
static SConformCase CaseLerp()
{
	SConformCase c;
	c.szName = "lerp, interpolating by the third argument";
	c.desc   = ThreeArgDesc(eCO_LERP);

	c.expect[0] = 128; c.expect[1] = 96; c.expect[2] = 80; c.expect[3] = 255;
	return c;
}

//! eCO_DECAL shares a case with eCO_REPLACE in the Direct3D backend -- both
//! are D3DTOP_SELECTARG1. It used to be translated as a texture-alpha blend
//! against the accumulator, which is what "decal" means almost everywhere
//! else, and which put every pass using it somewhere between two colours.
static SConformCase CaseDecal()
{
	SConformCase c;
	c.szName = "decal selects its first argument";
	c.desc   = ThreeArgDesc(eCO_DECAL);

	c.expect[0] = 128; c.expect[1] = 64; c.expect[2] = 32; c.expect[3] = 255;
	return c;
}

//! eCO_DETAIL is in no shipped backend's switch, so it lands on their default,
//! which is a plain modulate. It used to be translated as modulate2x, on the
//! strength of what a detail map usually is -- twice as bright as the engine
//! makes it.
static SConformCase CaseDetail()
{
	SConformCase c;
	c.szName = "detail is a plain modulate";
	c.desc   = ThreeArgDesc(eCO_DETAIL);

	c.expect[0] = 64; c.expect[1] = 32; c.expect[2] = 16; c.expect[3] = 255;
	return c;
}

//////////////////////////////////////////////////////////////////////////
// RENDER STATE
//
// A different claim from everything above. The cases above ask "does the
// generated shader compute the right colour"; these ask "does the engine's
// GS_* word reach GL at all".
//
// It did not, until GLESState.cpp. CRenderer::EF_SetState records the word
// into m_CurState and Crytek's null-renderer implementation -- which this
// backend inherits -- does nothing else with it. Every call asking for alpha
// blending drew opaque, and the symptom appears at the thing being drawn
// rather than at the state that was dropped.
//
// Each case draws a known background, then draws the quad again with the
// state under test, and reads back what the two produced together. A state
// that is silently ignored gives the source colour unchanged, which is a
// different answer from every expectation below -- so "dropped entirely" can
// never pass.
//////////////////////////////////////////////////////////////////////////

struct SStateCase
{
	const char*		szName;
	int				nRenderState;
	unsigned char	expect[4];
};

//! The background every state case is drawn over, and the source drawn on top.
//!
//! They are DIFFERENT COLOURS, deliberately. An earlier draft drew the same
//! quad twice, which made "the background survives" and "the source survives"
//! the same expected value -- so a render state that never reached GL would
//! have passed the case meant to prove it had.
//!
//!   background  the untextured pass, 128,128,128,255
//!   source      the textured modulate pass, 64,32,16,255
//!
//! With those, a state that is dropped entirely gives the source unchanged and
//! fails three of the five cases below.
static const unsigned char kBackground[4] = { 128, 128, 128, 255 };
static const unsigned char kSrc[4]        = {  64,  32,  16, 255 };

static const int kNumStateCases = 5;

//! The state the background is drawn with: depth writing on, no blending, all
//! channels written. Whatever a case does afterwards, it starts from this.
static int BackgroundState()
{
	return GS_DEPTHWRITE;
}

static SStateCase StateCase(int i)
{
	SStateCase c;

	switch (i)
	{
	case 0:
		// No blend bits at all. The engine's zero nibble means "no blending",
		// which is NOT the same as GS_BLSRC_ZERO (0x1) -- conflating them
		// would multiply every opaque surface by nothing and draw black.
		c.szName = "no blend bits means no blending, not zero";
		c.nRenderState = GS_DEPTHWRITE;
		c.expect[0] = kSrc[0]; c.expect[1] = kSrc[1];
		c.expect[2] = kSrc[2]; c.expect[3] = kSrc[3];
		return c;

	case 1:
		// src*1 + dst*1.
		//   64 + 128 = 192,  32 + 128 = 160,  16 + 128 = 144
		c.szName = "additive blending sums with the background";
		c.nRenderState = GS_DEPTHWRITE | GS_BLSRC_ONE | GS_BLDST_ONE;
		c.expect[0] = 192; c.expect[1] = 160; c.expect[2] = 144; c.expect[3] = 255;
		return c;

	case 2:
		// src*srcAlpha + dst*(1-srcAlpha), with srcAlpha 1.0 -- the source
		// wins outright. The commonest blend in the engine, and the one
		// CryFont and CRESky ask for.
		c.szName = "source-alpha blending with an opaque source";
		c.nRenderState = GS_DEPTHWRITE | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
		c.expect[0] = kSrc[0]; c.expect[1] = kSrc[1];
		c.expect[2] = kSrc[2]; c.expect[3] = kSrc[3];
		return c;

	case 3:
		// src*0 + dst*1: the source is thrown away and the background
		// survives untouched. This is the case a dropped state cannot fake,
		// since a dropped state draws the source.
		c.szName = "zero source factor leaves the background";
		c.nRenderState = GS_DEPTHWRITE | GS_BLSRC_ZERO | GS_BLDST_ONE;
		c.expect[0] = kBackground[0]; c.expect[1] = kBackground[1];
		c.expect[2] = kBackground[2]; c.expect[3] = kBackground[3];
		return c;

	default:
		// Colour masking, over additive blending. GS_COLMASKONLYALPHA writes
		// alpha and no colour, so the background's RGB survives while the
		// alpha channel takes the blend.
		//
		// A mask applied BACKWARDS would write colour and not alpha, giving
		// the additive result above -- which is why this case sits on top of
		// additive blending rather than on top of nothing.
		c.szName = "alpha-only colour mask leaves RGB alone";
		c.nRenderState = GS_DEPTHWRITE | GS_COLMASKONLYALPHA
		               | GS_BLSRC_ONE | GS_BLDST_ONE;
		c.expect[0] = kBackground[0]; c.expect[1] = kBackground[1];
		c.expect[2] = kBackground[2]; c.expect[3] = 255;
		return c;
	}
}

//////////////////////////////////////////////////////////////////////////

static GLuint MakeTexture(const unsigned char* pRGBA)
{
	GLuint nTex = 0;
	glGenTextures(1, &nTex);
	if (!nTex)
		return 0;

	glBindTexture(GL_TEXTURE_2D, nTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pRGBA);

	// No mipmaps are generated, so a filter that wants them samples nothing.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	return nTex;
}

//////////////////////////////////////////////////////////////////////////
//! The quad, with the attribute numbering the generators emit.
//////////////////////////////////////////////////////////////////////////
static void DrawQuad(GLuint nVBO)
{
	glBindBuffer(GL_ARRAY_BUFFER, nVBO);

	// position vec3, colour 4 unsigned bytes normalised, uv vec2.
	const GLsizei nStride = 3 * sizeof(float) + 4 + 2 * sizeof(float);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, nStride, (const void*)0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, nStride,
	                      (const void*)(3 * sizeof(float)));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, nStride,
	                      (const void*)(3 * sizeof(float) + 4));

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

//////////////////////////////////////////////////////////////////////////
//! Select a pass's program and bind everything it needs. Returns 0 if the
//! program would not build.
//////////////////////////////////////////////////////////////////////////
static const SGLESProgram* BindPass(const SCryPassDesc& desc,
                                    GLuint nTexA, GLuint nTexB)
{
	const SGLESProgram* pProgram = GLESShader_GetForPass(desc);
	if (!pProgram)
		return 0;

	glUseProgram(pProgram->nProgram);

	// Identity transform: the quad is already in clip space.
	static const float kIdentity[16] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	if (pProgram->nMVP >= 0)
		glUniformMatrix4fv(pProgram->nMVP, 1, GL_FALSE, kIdentity);

	// No case reads the constant colour -- the engine cannot pack eCA_Constant
	// as a third argument, and none of these name it elsewhere. It is set to
	// magenta rather than white precisely so that a generator emitting it by
	// mistake produces something unmistakable instead of a plausible result
	// that a tolerance might swallow.
	if (pProgram->nConstColor >= 0)
		glUniform4f(pProgram->nConstColor, 1.0f, 0.0f, 1.0f, 1.0f);

	for (int nStage = 0; nStage < pProgram->nStages; ++nStage)
	{
		if (pProgram->nSamplers[nStage] < 0)
			continue;

		glActiveTexture(GL_TEXTURE0 + nStage);
		glBindTexture(GL_TEXTURE_2D, nStage == 0 ? nTexA : nTexB);
		glUniform1i(pProgram->nSamplers[nStage], nStage);
	}

	return pProgram;
}

//////////////////////////////////////////////////////////////////////////
//! Run one case. Returns true if the pixel came back as expected.
//////////////////////////////////////////////////////////////////////////
static bool RunCase(const SConformCase& c, GLuint nTexA, GLuint nTexB,
                    GLuint nVBO, std::string& sDetail)
{
	if (!BindPass(c.desc, nTexA, nTexB))
	{
		sDetail = "program did not build";
		return false;
	}

	// These cases are about the SHADER, so blending is off and the quad is the
	// only thing in the target. The render-state cases below are the ones that
	// care what was already there.
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GLESState_Invalidate();

	glClearColor(kClear[0] / 255.0f, kClear[1] / 255.0f,
	             kClear[2] / 255.0f, kClear[3] / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	DrawQuad(nVBO);

	unsigned char px[4] = { 0, 0, 0, 0 };
	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

	bool bOk = true;
	for (int i = 0; i < 4; ++i)
	{
		const int nDiff = (int)px[i] - (int)c.expect[i];
		if (nDiff > kTolerance || nDiff < -kTolerance)
			bOk = false;
	}

	char buf[192];
	snprintf(buf, sizeof(buf), "got %d,%d,%d,%d expected %d,%d,%d,%d",
	         px[0], px[1], px[2], px[3],
	         c.expect[0], c.expect[1], c.expect[2], c.expect[3]);
	sDetail = buf;

	return bOk;
}

//////////////////////////////////////////////////////////////////////////
//! Run one render-state case.
//!
//! Two draws: the background with blending off, then the source with the state
//! under test. What comes back is what the two produced together, so a state
//! that never reached GL gives the source unchanged -- which three of the five
//! expectations do not accept.
//////////////////////////////////////////////////////////////////////////
static bool RunStateCase(const SStateCase& c, GLuint nTexA, GLuint nTexB,
                         GLuint nVBO, std::string& sDetail)
{
	//////////////////////////////////////////////////////////////////////
	// The background: the untextured pass, drawn opaque.
	//////////////////////////////////////////////////////////////////////
	if (!BindPass(CaseUntextured().desc, nTexA, nTexB))
	{
		sDetail = "background program did not build";
		return false;
	}

	GLESState_Apply(BackgroundState());

	glClearColor(kClear[0] / 255.0f, kClear[1] / 255.0f,
	             kClear[2] / 255.0f, kClear[3] / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	DrawQuad(nVBO);

	//////////////////////////////////////////////////////////////////////
	// The source, with the state being tested. Applied through the same
	// GLESState_Apply the draw path uses -- testing a private copy of the
	// translation would prove nothing about what the engine gets.
	//////////////////////////////////////////////////////////////////////
	if (!BindPass(CaseModulate().desc, nTexA, nTexB))
	{
		sDetail = "source program did not build";
		return false;
	}

	GLESState_Apply(c.nRenderState);

	DrawQuad(nVBO);

	unsigned char px[4] = { 0, 0, 0, 0 };
	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

	bool bOk = true;
	for (int i = 0; i < 4; ++i)
	{
		const int nDiff = (int)px[i] - (int)c.expect[i];
		if (nDiff > kTolerance || nDiff < -kTolerance)
			bOk = false;
	}

	char buf[192];
	snprintf(buf, sizeof(buf), "got %d,%d,%d,%d expected %d,%d,%d,%d",
	         px[0], px[1], px[2], px[3],
	         c.expect[0], c.expect[1], c.expect[2], c.expect[3]);
	sDetail = buf;

	return bOk;
}

//////////////////////////////////////////////////////////////////////////

bool GLESConform_Run(int& nPassed, int& nTotal)
{
	nPassed = 0;
	nTotal  = 0;

	SConformCase cases[10];
	cases[0] = CaseModulate();
	cases[1] = CaseUntextured();
	cases[2] = CaseTwoStageModulate();
	cases[3] = CaseTwoStageAdd();
	cases[4] = CaseAlphaTestKeeps();
	cases[5] = CaseAlphaTestDiscards();
	cases[6] = CaseMultiplyAdd();
	cases[7] = CaseLerp();
	cases[8] = CaseDecal();
	cases[9] = CaseDetail();

	const int kNumCases = 10;

	//////////////////////////////////////////////////////////////////////
	// An off-screen target, so this neither reads nor disturbs the canvas.
	//////////////////////////////////////////////////////////////////////
	GLuint nTarget = 0, nFBO = 0;
	glGenTextures(1, &nTarget);
	glBindTexture(GL_TEXTURE_2D, nTarget);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &nFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, nFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, nTarget, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		iLog->LogError("XRenderGLES: shader conformance needs an off-screen "
		               "target and could not make one");
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &nFBO);
		glDeleteTextures(1, &nTarget);
		return false;
	}

	glViewport(0, 0, 4, 4);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	// Set behind GLESState's back, so its cache must not be trusted after
	// this runs. Invalidated at the end too -- see the teardown.
	GLESState_Invalidate();

	//////////////////////////////////////////////////////////////////////
	// A quad covering clip space, with the colour packed the way the engine
	// packs it.
	//////////////////////////////////////////////////////////////////////
	struct SVertex
	{
		float			x, y, z;
		unsigned char	bgra[4];
		float			u, v;
	};

	SVertex verts[4];
	static const float kX[4] = { -1.0f,  1.0f, -1.0f, 1.0f };
	static const float kY[4] = { -1.0f, -1.0f,  1.0f, 1.0f };

	for (int i = 0; i < 4; ++i)
	{
		verts[i].x = kX[i];
		verts[i].y = kY[i];
		verts[i].z = 0.0f;
		memcpy(verts[i].bgra, kVertexBGRA, 4);

		// The textures are 1x1, so any coordinate samples the same texel; the
		// centre keeps it away from wrap behaviour at the edges.
		verts[i].u = 0.5f;
		verts[i].v = 0.5f;
	}

	GLuint nVBO = 0;
	glGenBuffers(1, &nVBO);
	glBindBuffer(GL_ARRAY_BUFFER, nVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

	const GLuint nTexA = MakeTexture(kTexA);
	const GLuint nTexB = MakeTexture(kTexB);

	//////////////////////////////////////////////////////////////////////

	iLog->Log("XRenderGLES: shader conformance, %d cases", kNumCases);

	for (int i = 0; i < kNumCases; ++i)
	{
		std::string sDetail;
		const bool bOk = RunCase(cases[i], nTexA, nTexB, nVBO, sDetail);

		++nTotal;
		if (bOk)
		{
			++nPassed;
			iLog->Log("  ok   %s (%s)", cases[i].szName, sDetail.c_str());
		}
		else
		{
			iLog->LogError("  FAIL %s (%s)", cases[i].szName, sDetail.c_str());
		}
	}

	//////////////////////////////////////////////////////////////////////
	// The render-state cases. A different claim: not what the shader computes,
	// but whether the engine's GS_* word reaches GL at all.
	//////////////////////////////////////////////////////////////////////

	iLog->Log("XRenderGLES: render-state conformance, %d cases", kNumStateCases);

	for (int i = 0; i < kNumStateCases; ++i)
	{
		const SStateCase c = StateCase(i);

		std::string sDetail;
		const bool bOk = RunStateCase(c, nTexA, nTexB, nVBO, sDetail);

		++nTotal;
		if (bOk)
		{
			++nPassed;
			iLog->Log("  ok   %s (%s)", c.szName, sDetail.c_str());
		}
		else
		{
			iLog->LogError("  FAIL %s (%s)", c.szName, sDetail.c_str());
		}
	}

	//////////////////////////////////////////////////////////////////////

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &nFBO);
	glDeleteTextures(1, &nTarget);
	glDeleteTextures(1, &nTexA);
	glDeleteTextures(1, &nTexB);
	glDeleteBuffers(1, &nVBO);

	glActiveTexture(GL_TEXTURE0);
	glUseProgram(0);

	// The engine's next draw must re-apply everything: this run left blend,
	// depth and the viewport wherever its last case put them.
	GLESState_Invalidate();

	return nPassed == nTotal;
}

#endif //__EMSCRIPTEN__
