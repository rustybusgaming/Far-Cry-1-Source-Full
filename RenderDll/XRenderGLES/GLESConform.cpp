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
//! Run one case. Returns true if the pixel came back as expected.
//////////////////////////////////////////////////////////////////////////
static bool RunCase(const SConformCase& c, GLuint nTexA, GLuint nTexB,
                    GLuint nVBO, std::string& sDetail)
{
	const SGLESProgram* pProgram = GLESShader_GetForPass(c.desc);

	if (!pProgram)
	{
		sDetail = "program did not build";
		return false;
	}

	glUseProgram(pProgram->nProgram);

	// Identity transform: the quad below is already in clip space.
	static const float kIdentity[16] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
	if (pProgram->nMVP >= 0)
		glUniformMatrix4fv(pProgram->nMVP, 1, GL_FALSE, kIdentity);

	// A constant colour no case reads, set anyway so the uniform is not left
	// at whatever the driver defaulted it to.
	if (pProgram->nConstColor >= 0)
		glUniform4f(pProgram->nConstColor, 1.0f, 1.0f, 1.0f, 1.0f);

	for (int nStage = 0; nStage < pProgram->nStages; ++nStage)
	{
		if (pProgram->nSamplers[nStage] < 0)
			continue;

		glActiveTexture(GL_TEXTURE0 + nStage);
		glBindTexture(GL_TEXTURE_2D, nStage == 0 ? nTexA : nTexB);
		glUniform1i(pProgram->nSamplers[nStage], nStage);
	}

	glClearColor(kClear[0] / 255.0f, kClear[1] / 255.0f,
	             kClear[2] / 255.0f, kClear[3] / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glBindBuffer(GL_ARRAY_BUFFER, nVBO);

	// position vec3, colour 4 unsigned bytes normalised, uv vec2 -- the same
	// attribute numbering the generator emits.
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

	SConformCase cases[6];
	cases[0] = CaseModulate();
	cases[1] = CaseUntextured();
	cases[2] = CaseTwoStageModulate();
	cases[3] = CaseTwoStageAdd();
	cases[4] = CaseAlphaTestKeeps();
	cases[5] = CaseAlphaTestDiscards();

	const int kNumCases = 6;

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

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &nFBO);
	glDeleteTextures(1, &nTarget);
	glDeleteTextures(1, &nTexA);
	glDeleteTextures(1, &nTexB);
	glDeleteBuffers(1, &nVBO);

	glActiveTexture(GL_TEXTURE0);
	glUseProgram(0);

	return nPassed == nTotal;
}

#endif //__EMSCRIPTEN__
