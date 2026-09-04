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

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
//! The program for the engine's generic dynamic vertex format.
//!
//! Two details are not arbitrary:
//!
//!   The colour is swizzled .bgra. The engine packs UCol as B,G,R,A when
//!   gbRgb is false, which is what every backend in this tree sets -- it is
//!   the Direct3D byte order, and the GL backends asked the driver for
//!   GL_BGRA to compensate. GLES 3.0 has no BGRA vertex format, so the bytes
//!   are read in their natural order and reordered here instead. Doing it in
//!   the shader costs nothing; doing it on the CPU would mean touching every
//!   vertex.
//!
//!   uUseTexture is an int rather than a bool. GLSL ES bool uniforms are set
//!   with glUniform1i anyway, and keeping the type as int avoids a class of
//!   mistake where a driver disagrees about the encoding of true.
//////////////////////////////////////////////////////////////////////////
static const char* g_szDynamicVS =
	"#version 300 es\n"
	"uniform mat4 uMVP;\n"
	"layout(location = 0) in vec3 aPosition;\n"
	"layout(location = 1) in vec4 aColor;\n"
	"layout(location = 2) in vec2 aTexCoord;\n"
	"out vec4 vColor;\n"
	"out vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"    vColor = aColor.bgra;\n"
	"    vTexCoord = aTexCoord;\n"
	"    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
	"}\n";

static const char* g_szDynamicFS =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D uTexture;\n"
	"uniform int uUseTexture;\n"
	"in vec4 vColor;\n"
	"in vec2 vTexCoord;\n"
	"out vec4 oColor;\n"
	"void main()\n"
	"{\n"
	"    oColor = (uUseTexture != 0)\n"
	"           ? texture(uTexture, vTexCoord) * vColor\n"
	"           : vColor;\n"
	"}\n";

static SGLESProgram	g_dynamic = { 0, -1, -1, -1 };
static bool			g_bDynamicTried = false;

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
	out.nProgram = 0;
	out.nMVP = out.nSampler = out.nUseTexture = -1;

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
	out.nSampler    = glGetUniformLocation(nProgram, "uTexture");
	out.nUseTexture = glGetUniformLocation(nProgram, "uUseTexture");

	return out;
}

void GLESShader_Destroy(SGLESProgram& program)
{
	if (program.nProgram)
		glDeleteProgram(program.nProgram);

	program.nProgram = 0;
	program.nMVP = program.nSampler = program.nUseTexture = -1;
}

const SGLESProgram* GLESShader_GetDynamic()
{
	if (!g_bDynamicTried)
	{
		g_bDynamicTried = true;
		g_dynamic = GLESShader_Build(g_szDynamicVS, g_szDynamicFS);

		if (g_dynamic.IsValid())
			iLog->Log("XRenderGLES: dynamic-vertex program built");
		else
			iLog->LogError("XRenderGLES: dynamic-vertex program unavailable; "
			               "nothing will be drawn");
	}

	return g_dynamic.IsValid() ? &g_dynamic : 0;
}

void GLESShader_Shutdown()
{
	GLESShader_Destroy(g_dynamic);

	// Cleared so a program is rebuilt if the renderer comes back up -- after a
	// context loss the old name means nothing.
	g_bDynamicTried = false;
}

#endif //__EMSCRIPTEN__
