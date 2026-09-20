////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUShaderGen.cpp
//  Description: Texture-stage pass -> WGSL. See WGPUShaderGen.h.
//
//  The stage chain itself is Common/CryPassGen.cpp, shared with the WebGL2
//  backend. What is left here is WGSL's own boilerplate: bind groups, the
//  vertex stage, and the entry points.
//
////////////////////////////////////////////////////////////////////////////

#include "WGPUShaderGen.h"

#include "CryPassGen.h"

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
//! WGSL wants a texture and a sampler as separate objects, so a stage's
//! sample names both.
//////////////////////////////////////////////////////////////////////////
static void SampleWGSL(int nStage, char* szOut, size_t nSize)
{
	snprintf(szOut, nSize, "textureSample(tex%d, samp%d, in.uv)", nStage, nStage);
}

static const SCryShaderDialect g_wgsl =
{
	"vec4f",		// szVec4
	"vec3f",		// szVec3
	"let",			// szDeclConst
	"var",			// szDeclVar
	"in.color",		// szDiffuse
	"uConst.color",	// szConstColor
	SampleWGSL,
};

//////////////////////////////////////////////////////////////////////////

bool WGPUShaderGen_Build(const SCryPassDesc& desc, std::string& sOut, std::string& sError)
{
	std::string sBody;
	if (!CryPassGen_Body(desc, g_wgsl, sBody, sError))
		return false;

	std::string s;

	//////////////////////////////////////////////////////////////////////
	// Uniforms and resources.
	//
	// One bind group: the transform and constant colour in a uniform buffer,
	// then a sampler and texture per stage. Grouping every stage into one bind
	// group means a draw sets one thing rather than N.
	//////////////////////////////////////////////////////////////////////
	s += "// Generated from a CryEngine shader pass. Do not edit.\n";
	s += "struct Uniforms {\n";
	s += "  mvp   : mat4x4f,\n";
	s += "  color : vec4f,\n";
	s += "};\n";
	s += "@group(0) @binding(0) var<uniform> uConst : Uniforms;\n";

	int nBinding = 1;
	for (int i = 0; i < desc.nStages; ++i)
	{
		if (!desc.stages[i].bHasTexture)
			continue;

		char buf[256];
		snprintf(buf, sizeof(buf),
		         "@group(0) @binding(%d) var samp%d : sampler;\n"
		         "@group(0) @binding(%d) var tex%d  : texture_2d<f32>;\n",
		         nBinding, i, nBinding + 1, i);
		s += buf;
		nBinding += 2;
	}

	//////////////////////////////////////////////////////////////////////
	// Vertex stage.
	//
	// The attribute numbering matches the GLES backend's, so both backends read
	// the same vertex buffers without a second layout to keep in step.
	//////////////////////////////////////////////////////////////////////
	s += "\nstruct VSOut {\n";
	s += "  @builtin(position) pos : vec4f,\n";
	s += "  @location(0) color : vec4f,\n";
	s += "  @location(1) uv    : vec2f,\n";
	s += "};\n\n";

	s += "@vertex\n";
	s += "fn vs_main(\n";
	s += "  @location(0) position : vec3f,\n";
	s += "  @location(1) color    : vec4f,\n";
	s += "  @location(2) uv       : vec2f,\n";
	s += ") -> VSOut {\n";
	s += "  var out : VSOut;\n";
	s += "  out.pos = uConst.mvp * vec4f(position, 1.0);\n";

	// The engine packs vertex colours as B,G,R,A -- the Direct3D order -- and
	// WebGPU has no BGRA vertex format any more than WebGL2 does, so the
	// swizzle happens here exactly as it does in the GLES shader.
	if (desc.bHasVertexColor)
		s += "  out.color = color.bgra;\n";
	else
		s += "  out.color = vec4f(1.0, 1.0, 1.0, 1.0);\n";

	s += desc.bHasTexCoord ? "  out.uv = uv;\n" : "  out.uv = vec2f(0.0, 0.0);\n";
	s += "  return out;\n";
	s += "}\n\n";

	//////////////////////////////////////////////////////////////////////
	// Fragment stage: the texture-stage chain.
	//////////////////////////////////////////////////////////////////////
	s += "@fragment\n";
	s += "fn fs_main(in : VSOut) -> @location(0) vec4f {\n";
	s += sBody;
	s += "\n  return acc;\n";
	s += "}\n";

	sOut.swap(s);
	return true;
}
