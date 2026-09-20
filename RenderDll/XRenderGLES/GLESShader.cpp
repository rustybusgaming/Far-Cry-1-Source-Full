////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   GLESShader.cpp
//  Description: GLSL ES program building. See GLESShader.h.
//
////////////////////////////////////////////////////////////////////////////

#include "RenderPCH.h"
#include "GLESShader.h"

#if defined(__EMSCRIPTEN__)

#include "GLESShaderGen.h"

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>

//! Programs by pass key. A null program means "this description was tried and
//! failed" -- kept so a pass that cannot build is not retranslated every frame.
static std::map<unsigned long long, SGLESProgram> g_cache;

//////////////////////////////////////////////////////////////////////////

static GLuint CompileStage(GLenum eType, const char* szSource)
{
	GLuint nShader = glCreateShader(eType);
	if (!nShader)
		return 0;

	glShaderSource(nShader, 1, &szSource, 0);
	glCompileShader(nShader);

	GLint bCompiled = 0;
	glGetShaderiv(nShader, GL_COMPILE_STATUS, &bCompiled);
	if (!bCompiled)
	{
		// The driver's log is the only thing that says WHY, and it is lost
		// the moment the shader is deleted.
		char szLog[2048];
		GLsizei nLen = 0;
		glGetShaderInfoLog(nShader, sizeof(szLog) - 1, &nLen, szLog);
		szLog[nLen < (GLsizei)sizeof(szLog) ? nLen : (GLsizei)sizeof(szLog) - 1] = 0;

		iLog->LogError("XRenderGLES: %s shader failed to compile:\n%s",
		               eType == GL_VERTEX_SHADER ? "vertex" : "fragment", szLog);

		glDeleteShader(nShader);
		return 0;
	}

	return nShader;
}

SGLESProgram GLESShader_Build(const char* szVertexSrc, const char* szFragmentSrc)
{
	SGLESProgram out;
	memset(&out, 0, sizeof(out));
	out.nProgram    = 0;
	out.nMVP        = -1;
	out.nConstColor = -1;
	out.nStages     = 0;
	for (int i = 0; i < SCryPassDesc::kMaxStages; ++i)
		out.nSamplers[i] = -1;

	GLuint nVS = CompileStage(GL_VERTEX_SHADER, szVertexSrc);
	if (!nVS)
		return out;

	GLuint nFS = CompileStage(GL_FRAGMENT_SHADER, szFragmentSrc);
	if (!nFS)
	{
		glDeleteShader(nVS);
		return out;
	}

	GLuint nProgram = glCreateProgram();
	glAttachShader(nProgram, nVS);
	glAttachShader(nProgram, nFS);

	// Bound explicitly as well as declared with layout() in the source. The
	// layout qualifiers make these authoritative in GLSL ES 3.00, and this is
	// belt and braces for the day a stage is written without them.
	glBindAttribLocation(nProgram, eGLESAttrib_Position, "aPosition");
	glBindAttribLocation(nProgram, eGLESAttrib_Color,    "aColor");
	glBindAttribLocation(nProgram, eGLESAttrib_TexCoord, "aTexCoord");

	glLinkProgram(nProgram);

	// The shader objects are reference-counted by the program; detaching and
	// deleting them here means they go away with it.
	glDetachShader(nProgram, nVS);
	glDetachShader(nProgram, nFS);
	glDeleteShader(nVS);
	glDeleteShader(nFS);

	GLint bLinked = 0;
	glGetProgramiv(nProgram, GL_LINK_STATUS, &bLinked);
	if (!bLinked)
	{
		char szLog[2048];
		GLsizei nLen = 0;
		glGetProgramInfoLog(nProgram, sizeof(szLog) - 1, &nLen, szLog);
		szLog[nLen < (GLsizei)sizeof(szLog) ? nLen : (GLsizei)sizeof(szLog) - 1] = 0;

		iLog->LogError("XRenderGLES: program failed to link:\n%s", szLog);

		glDeleteProgram(nProgram);
		return out;
	}

	out.nProgram    = nProgram;
	out.nMVP        = glGetUniformLocation(nProgram, "uMVP");
	out.nConstColor = glGetUniformLocation(nProgram, "uConstColor");

	return out;
}

void GLESShader_Destroy(SGLESProgram& program)
{
	if (program.nProgram)
		glDeleteProgram(program.nProgram);

	program.nProgram    = 0;
	program.nMVP        = -1;
	program.nConstColor = -1;
	program.nStages     = 0;
	for (int i = 0; i < SCryPassDesc::kMaxStages; ++i)
		program.nSamplers[i] = -1;
}

//////////////////////////////////////////////////////////////////////////
//! Log a shader the driver rejected, with line numbers.
//!
//! The generated source exists nowhere on disk, so a driver error naming
//! "line 24" is unusable on its own. Printing it numbered is the difference
//! between a fixable report and a dead end -- and this is the only place the
//! source can still be seen, since it is discarded once the program links.
//////////////////////////////////////////////////////////////////////////
static void LogSource(const char* szWhich, const std::string& sSource)
{
	iLog->LogError("XRenderGLES: generated %s source was:", szWhich);

	int nLine = 1;
	size_t nStart = 0;
	while (nStart <= sSource.size())
	{
		const size_t nEnd = sSource.find('\n', nStart);
		const std::string sLine = sSource.substr(
			nStart, nEnd == std::string::npos ? std::string::npos : nEnd - nStart);

		iLog->LogError("  %3d | %s", nLine++, sLine.c_str());

		if (nEnd == std::string::npos)
			break;
		nStart = nEnd + 1;
	}
}

//////////////////////////////////////////////////////////////////////////

SCryPassDesc GLESShader_DynamicPass(bool bTextured)
{
	SCryPassDesc desc;
	desc.nStages = 1;

	desc.stages[0].bHasTexture = bTextured;

	// eCO_MODULATE with DEF_TEXARG0 is "texture times diffuse" -- the
	// fixed-function default, and what every DrawDynVB caller in the engine
	// expects. Without a texture the stage's texel reads as white and the
	// same expression collapses to the vertex colour, which is exactly what
	// the old hand-written program's untextured branch did.
	desc.stages[0].nColorOp  = eCO_MODULATE;
	desc.stages[0].nColorArg = DEF_TEXARG0;
	desc.stages[0].nAlphaOp  = eCO_MODULATE;
	desc.stages[0].nAlphaArg = DEF_TEXARG0;

	desc.bHasVertexColor = true;
	desc.bHasTexCoord    = true;

	return desc;
}

const SGLESProgram* GLESShader_GetForPass(const SCryPassDesc& desc)
{
	const unsigned long long nKey = CryPass_Key(desc);

	std::map<unsigned long long, SGLESProgram>::iterator it = g_cache.find(nKey);
	if (it != g_cache.end())
		return it->second.IsValid() ? &it->second : 0;

	SGLESProgram program;
	memset(&program, 0, sizeof(program));
	program.nProgram = 0;

	std::string sVS, sFS, sError;
	if (!GLESShaderGen_Build(desc, sVS, sFS, sError))
	{
		iLog->LogError("XRenderGLES: cannot translate pass: %s", sError.c_str());

		// Cached as a failure. Retranslating an impossible pass once per draw
		// would turn a rendering bug into a frame-rate one and bury the log.
		g_cache[nKey] = program;
		return 0;
	}

	program = GLESShader_Build(sVS.c_str(), sFS.c_str());

	if (!program.IsValid())
	{
		LogSource("vertex", sVS);
		LogSource("fragment", sFS);
		g_cache[nKey] = program;
		return 0;
	}

	// The sampler locations, by the names the generator emitted. Both come
	// from GLESShaderGen_SamplerName so they cannot disagree.
	program.nStages = desc.nStages;
	for (int i = 0; i < desc.nStages && i < SCryPassDesc::kMaxStages; ++i)
	{
		if (!desc.stages[i].bHasTexture)
			continue;

		program.nSamplers[i] = glGetUniformLocation(
			program.nProgram, GLESShaderGen_SamplerName(i).c_str());
	}

	g_cache[nKey] = program;

	iLog->Log("XRenderGLES: built program for a %d-stage pass (%d cached)",
	          desc.nStages, (int)g_cache.size());

	return &g_cache[nKey];
}

int GLESShader_CacheSize()
{
	return (int)g_cache.size();
}

void GLESShader_Shutdown()
{
	for (std::map<unsigned long long, SGLESProgram>::iterator it = g_cache.begin();
	     it != g_cache.end(); ++it)
	{
		GLESShader_Destroy(it->second);
	}

	// Cleared rather than kept: after a context loss every program name means
	// nothing, so the next frame must build again from scratch.
	g_cache.clear();
}

#endif //__EMSCRIPTEN__
