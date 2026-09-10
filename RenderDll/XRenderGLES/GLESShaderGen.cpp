////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESShaderGen.cpp
//  Description: Texture-stage pass -> GLSL ES 3.00. See GLESShaderGen.h.
//
////////////////////////////////////////////////////////////////////////////

#include "GLESShaderGen.h"

#include "CryPassGen.h"

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
//! Attribute locations. These are bound explicitly before linking rather than
//! queried back, and they match the WGSL generator's @location numbering, so
//! both backends read the same vertex buffers.
//!
//! Kept as literals here rather than including GLESShader.h, which pulls in
//! GLES3/gl3.h and would stop this file compiling natively.
//////////////////////////////////////////////////////////////////////////
static const int kAttribPosition = 0;
static const int kAttribColor    = 1;
static const int kAttribTexCoord = 2;

std::string GLESShaderGen_SamplerName(int nStage)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "uTex%d", nStage);
	return buf;
}

//////////////////////////////////////////////////////////////////////////
//! GLSL has one combined sampler object where WGSL has a texture and a
//! sampler, which is the whole of the difference at the sampling site.
//////////////////////////////////////////////////////////////////////////
static void SampleGLSL(int nStage, char* szOut, size_t nSize)
{
	snprintf(szOut, nSize, "texture(uTex%d, vTexCoord)", nStage);
}

//! GLSL ES has no let/var distinction, so both declaration forms are the type
//! name. Everything the stage chain builds is a vec4.
static const SCryShaderDialect g_glsl =
{
	"vec4",			// szVec4
	"vec3",			// szVec3
	"vec4",			// szDeclConst
	"vec4",			// szDeclVar
	"vColor",		// szDiffuse
	"uConstColor",	// szConstColor
	SampleGLSL,
};

//////////////////////////////////////////////////////////////////////////

bool GLESShaderGen_Build(const SCryPassDesc& desc,
                         std::string& sVertexOut, std::string& sFragmentOut,
                         std::string& sError)
{
	std::string sBody;
	if (!CryPassGen_Body(desc, g_glsl, sBody, sError))
		return false;

	char buf[256];

	//////////////////////////////////////////////////////////////////////
	// Vertex stage.
	//////////////////////////////////////////////////////////////////////
	std::string sVS;
	sVS += "#version 300 es\n";
	sVS += "// Generated from a CryEngine shader pass. Do not edit.\n";
	sVS += "uniform mat4 uMVP;\n";

	snprintf(buf, sizeof(buf),
	         "layout(location = %d) in vec3 aPosition;\n"
	         "layout(location = %d) in vec4 aColor;\n"
	         "layout(location = %d) in vec2 aTexCoord;\n",
	         kAttribPosition, kAttribColor, kAttribTexCoord);
	sVS += buf;

	sVS += "out vec4 vColor;\n";
	sVS += "out vec2 vTexCoord;\n";
	sVS += "void main()\n";
	sVS += "{\n";

	// The engine packs UCol as B,G,R,A -- the Direct3D byte order -- and
	// GLES 3.0 has no BGRA vertex format, so the bytes are read in their
	// natural order and reordered here. Doing it in the shader costs nothing;
	// doing it on the CPU would mean touching every vertex.
	if (desc.bHasVertexColor)
		sVS += "  vColor = aColor.bgra;\n";
	else
		sVS += "  vColor = vec4(1.0, 1.0, 1.0, 1.0);\n";

	sVS += desc.bHasTexCoord ? "  vTexCoord = aTexCoord;\n"
	                         : "  vTexCoord = vec2(0.0, 0.0);\n";
	sVS += "  gl_Position = uMVP * vec4(aPosition, 1.0);\n";
	sVS += "}\n";

	//////////////////////////////////////////////////////////////////////
	// Fragment stage.
	//
	// mediump, not highp: highp is not guaranteed in fragment shaders on
	// GLES 3.0, and a shader that demands it fails to compile on hardware that
	// would otherwise have run it. Colour arithmetic does not need the range.
	//////////////////////////////////////////////////////////////////////
	std::string sFS;
	sFS += "#version 300 es\n";
	sFS += "// Generated from a CryEngine shader pass. Do not edit.\n";
	sFS += "precision mediump float;\n";
	sFS += "uniform vec4 uConstColor;\n";

	// One sampler per textured stage. A stage without a texture declares none,
	// so an unused sampler never occupies a texture unit.
	for (int i = 0; i < desc.nStages; ++i)
	{
		if (!desc.stages[i].bHasTexture)
			continue;

		snprintf(buf, sizeof(buf), "uniform sampler2D %s;\n",
		         GLESShaderGen_SamplerName(i).c_str());
		sFS += buf;
	}

	sFS += "in vec4 vColor;\n";
	sFS += "in vec2 vTexCoord;\n";
	sFS += "out vec4 oColor;\n";
	sFS += "void main()\n";
	sFS += "{\n";
	sFS += sBody;
	sFS += "\n  oColor = acc;\n";
	sFS += "}\n";

	sVertexOut.swap(sVS);
	sFragmentOut.swap(sFS);
	return true;
}
