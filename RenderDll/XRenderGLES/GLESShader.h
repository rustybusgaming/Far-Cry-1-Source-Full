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

	This file is the layer underneath that: compiling and caching the programs
	the backend draws with. GLES has no fixed-function pipeline, so where the
	original could just enable texturing and call glBegin, this backend needs a
	compiled program before a single triangle can appear.

	The programs are no longer hardcoded. GLESShaderGen turns a pass
	description into GLSL ES, and this file compiles the result and keeps it,
	keyed on the description -- so a pass that names eCO_ADD or an alpha test
	gets a program that does that, rather than the one thing the backend used
	to know how to do.

	WHY A CACHE

	Compiling and linking crosses into the browser's GL implementation and is
	far too slow to do per draw, while the engine changes state constantly. Two
	draws with the same description must therefore find the same program, which
	is what CryPass_Key is for. A key missing a field would mean two different
	passes silently sharing one program and the second rendering with the
	first's shader.
*/

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

#include "CryPassDesc.h"

//! A compiled and linked program, with the uniform locations resolved once.
//! Locations are looked up at link time rather than per draw: glGetUniformLocation
//! is a string lookup that crosses into JavaScript here.
struct SGLESProgram
{
	GLuint	nProgram;

	GLint	nMVP;			//!< mat4, model-view-projection
	GLint	nConstColor;	//!< vec4, the pass's constant colour

	//! One sampler per texture stage, -1 where the stage has no texture. The
	//! generated source declares a sampler only for the stages that need one,
	//! so an unused stage never occupies a texture unit.
	GLint	nSamplers[SCryPassDesc::kMaxStages];

	//! How many stages this program was built for. The draw code binds this
	//! many texture units and no more.
	int		nStages;

	bool	IsValid() const { return nProgram != 0; }
};

//! Compile, link and report. Returns a program with nProgram == 0 on failure,
//! having logged the driver's own log -- which is the only useful diagnostic
//! for a shader that will not build, and is easy to lose.
SGLESProgram GLESShader_Build(const char* szVertexSrc, const char* szFragmentSrc);

void GLESShader_Destroy(SGLESProgram& program);

//! Fetch or build the program for a pass.
//!
//! Returns 0 if the pass could not be translated or the program would not
//! compile, having logged why -- including the generated source, because a
//! driver's error message is a line number into source that exists nowhere on
//! disk and is useless without it.
//!
//! A failed description is remembered as a failure, so a pass that cannot
//! build does not try again on every frame.
const SGLESProgram* GLESShader_GetForPass(const SCryPassDesc& desc);

//! The description for the engine's generic dynamic-vertex format: one texture
//! stage modulated by the vertex colour, which is what the fixed-function
//! default produces and what DrawDynVB callers expect.
//!
//! Textured and untextured are two different descriptions, and so two
//! programs. The old single program carried a uUseTexture uniform and branched
//! per fragment; generating both removes the branch and, more importantly,
//! means the untextured case goes down the same generated path rather than
//! being a special case in hand-written source.
SCryPassDesc GLESShader_DynamicPass(bool bTextured);

//! How many programs are cached. For logging, and for a test to show that a
//! repeated description does not compile a second one.
int GLESShader_CacheSize();

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
