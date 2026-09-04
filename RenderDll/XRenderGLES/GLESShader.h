#ifndef _CRY_GLES_SHADER_H_
#define _CRY_GLES_SHADER_H_

/*!
	GLESShader -- GLSL ES programs for the WebGL2 backend.

	WHAT THIS IS NOT

	It is not a port of the engine's shader system. Far Cry's shaders are
	written in a Crytek-specific script language that XRenderOGL compiles down
	to NV register combiners and ARB/NV assembly programs, none of which exists
	in GLES 3.0. That translation is the largest single piece of work left in
	the renderer and it has not started.

	This file is the layer underneath that: the small number of programs the
	backend needs to draw ANYTHING at all. GLES has no fixed-function pipeline,
	so where the original could just enable texturing and call glBegin, this
	backend needs a compiled program before a single triangle can appear.

	One program exists so far, for the engine's generic dynamic-vertex format
	(position, colour, one texture coordinate). Every call that reaches
	DrawDynVB uses it -- which today means CryFont's text and CryGame's script
	renderer.
*/

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

//! A compiled and linked program, with the uniform locations resolved once.
//! Locations are looked up at link time rather than per draw: glGetUniformLocation
//! is a string lookup that crosses into JavaScript here.
struct SGLESProgram
{
	GLuint	nProgram;

	GLint	nMVP;			//!< mat4, model-view-projection
	GLint	nSampler;		//!< sampler2D, texture unit 0
	GLint	nUseTexture;	//!< int used as a bool; see the note in the .cpp

	bool	IsValid() const { return nProgram != 0; }
};

//! Compile, link and report. Returns a program with nProgram == 0 on failure,
//! having logged the driver's own log -- which is the only useful diagnostic
//! for a shader that will not build, and is easy to lose.
SGLESProgram GLESShader_Build(const char* szVertexSrc, const char* szFragmentSrc);

void GLESShader_Destroy(SGLESProgram& program);

//! The program for struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F, built on first use.
const SGLESProgram* GLESShader_GetDynamic();

//! Drop every cached program. Called from the renderer's shutdown, and needed
//! because a lost context invalidates the names.
void GLESShader_Shutdown();

//! Attribute locations, bound explicitly before linking so the vertex format
//! can be described without querying the program back.
enum EGLESAttrib
{
	eGLESAttrib_Position = 0,
	eGLESAttrib_Color    = 1,
	eGLESAttrib_TexCoord = 2,
};

#endif //__EMSCRIPTEN__

#endif //_CRY_GLES_SHADER_H_
