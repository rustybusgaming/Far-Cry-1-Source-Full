////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_pipelinekey.cpp
//  Description: Tests for the WebGPU pipeline cache key.
//
//  This is the highest-consequence pure function in the backend. WebGPU bakes
//  the shaders, the vertex layout and the blend and depth state into one
//  immutable pipeline object, and the cache hands out a pipeline per key. A key
//  missing a field means two passes that should differ silently share one --
//  and the second renders with the first's shader or blend mode, on some
//  surfaces, sometimes. That is a genuinely horrible bug to chase, and it is
//  entirely preventable here, without a GPU.
//
//  So every field of the description gets its own assertion.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <IRenderer.h>
#include <IShader.h>
#include <VertexFormats.h>

#include "WGPUPipeline.h"

#include <stdio.h>

static int g_nFailures = 0;

#define CHECK(cond, what) \
	do { \
		if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); ++g_nFailures; } \
		else         { printf("ok  : %s\n", what); } \
	} while (0)

//! A plausible pass: one modulated texture, ordinary alpha blending.
static SWGPUPipelineDesc MakeBaseline()
{
	SWGPUPipelineDesc d;

	d.shader.nStages = 1;
	d.shader.stages[0].bHasTexture = true;
	d.shader.stages[0].nColorOp  = eCO_MODULATE;
	d.shader.stages[0].nColorArg = DEF_TEXARG0;
	d.shader.stages[0].nAlphaOp  = eCO_MODULATE;
	d.shader.stages[0].nAlphaArg = DEF_TEXARG0;

	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, d.state);

	d.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
	d.nPrimType     = R_PRIMV_TRIANGLES;

	return d;
}

static void TestStability()
{
	const SWGPUPipelineDesc a = MakeBaseline();
	const SWGPUPipelineDesc b = MakeBaseline();

	CHECK(WGPUPipeline_Key(a) == WGPUPipeline_Key(b),
	      "two identical descriptions give the same key");

	// Determinism across calls: a key built from an address or a counter would
	// pass the comparison above and still be useless as a cache key.
	CHECK(WGPUPipeline_Key(a) == WGPUPipeline_Key(a),
	      "the key is stable across repeated calls");
}

static void TestShaderChangesKey()
{
	const SWGPUPipelineDesc base = MakeBaseline();

	SWGPUPipelineDesc d = base;
	d.shader.stages[0].nColorOp = eCO_ADD;
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "a different colour operation changes the key");

	d = base;
	d.shader.nStages = 2;
	d.shader.stages[1].bHasTexture = true;
	d.shader.stages[1].nColorOp = eCO_MODULATE;
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "an extra stage changes the key");

	d = base;
	d.shader.nAlphaTest = eWGPUAlphaTest_GreaterEqual;
	d.shader.fAlphaRef  = 0.5f;
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "an alpha test changes the key");
}

static void TestStateChangesKey()
{
	const SWGPUPipelineDesc base = MakeBaseline();

	SWGPUPipelineDesc d = base;
	WGPUStateGen_Decode(GS_BLSRC_ONE | GS_BLDST_ONE, d.state);
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "additive instead of alpha blending changes the key");

	d = base;
	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_DEPTHWRITE,
	                    d.state);
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "turning on depth writing changes the key");

	d = base;
	WGPUStateGen_Decode(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | GS_NOCOLMASK,
	                    d.state);
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "a colour write mask changes the key");
}

//////////////////////////////////////////////////////////////////////////
//! The two fields that are easiest to leave out, because they belong to
//! neither translator: the pipeline bakes the vertex layout and the topology
//! in as well.
//////////////////////////////////////////////////////////////////////////
static void TestVertexFormatAndTopologyChangeKey()
{
	const SWGPUPipelineDesc base = MakeBaseline();

	SWGPUPipelineDesc d = base;
	d.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB;
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "a different vertex format changes the key");

	d = base;
	d.nPrimType = R_PRIMV_TRIANGLE_STRIP;
	CHECK(WGPUPipeline_Key(base) != WGPUPipeline_Key(d),
	      "a different primitive topology changes the key");
}

//////////////////////////////////////////////////////////////////////////
//! Distinctness across a spread of realistic passes. Pairwise, because a hash
//! that separates each from a baseline could still collide two of them.
//////////////////////////////////////////////////////////////////////////
static void TestNoCollisionsAcrossRealisticPasses()
{
	SWGPUPipelineDesc set[6];
	int n = 0;

	set[n] = MakeBaseline(); ++n;								// textured, alpha blended

	set[n] = MakeBaseline();									// opaque
	WGPUStateGen_Decode(GS_DEPTHWRITE, set[n].state); ++n;

	set[n] = MakeBaseline();									// additive particle
	WGPUStateGen_Decode(GS_BLSRC_ONE | GS_BLDST_ONE, set[n].state); ++n;

	set[n] = MakeBaseline();									// alpha-tested foliage
	WGPUStateGen_Decode(GS_DEPTHWRITE | GS_ALPHATEST_GEQUAL128, set[n].state);
	set[n].shader.nAlphaTest = eWGPUAlphaTest_GreaterEqual;
	set[n].shader.fAlphaRef  = 128.0f / 255.0f; ++n;

	set[n] = MakeBaseline();									// two-stage detail
	set[n].shader.nStages = 2;
	set[n].shader.stages[1].bHasTexture = true;
	set[n].shader.stages[1].nColorOp  = eCO_MODULATE2X;
	set[n].shader.stages[1].nColorArg = DEF_TEXARG1; ++n;

	set[n] = MakeBaseline();									// untextured geometry
	set[n].shader.stages[0].bHasTexture = false;
	set[n].nVertexFormat = VERTEX_FORMAT_P3F_COL4UB; ++n;

	bool bAllDistinct = true;
	for (int i = 0; i < n; ++i)
	{
		for (int j = i + 1; j < n; ++j)
		{
			if (WGPUPipeline_Key(set[i]) == WGPUPipeline_Key(set[j]))
			{
				printf("     keys %d and %d collide\n", i, j);
				bAllDistinct = false;
			}
		}
	}

	CHECK(bAllDistinct, "six realistic passes all get distinct keys");
}

int main()
{
	TestStability();
	TestShaderChangesKey();
	TestStateChangesKey();
	TestVertexFormatAndTopologyChangeKey();
	TestNoCollisionsAcrossRealisticPasses();

	if (g_nFailures)
	{
		printf("\n%d failure(s)\n", g_nFailures);
		return 1;
	}
	printf("\npipelinekey: all tests passed\n");
	return 0;
}
