#ifndef _CRY_PASS_GEN_H_
#define _CRY_PASS_GEN_H_

/*!
	CryPassGen -- the texture-stage chain, emitted once for every language.

	WHY THIS IS NOT TWO GENERATORS

	The WebGL2 and WebGPU backends both have to reproduce the same
	fixed-function model, and the interesting part of that is not syntax: it is
	deciding what eCO_DOTPRODUCT3 means, that eCO_DETAIL is MODULATE2X in this
	engine's usage, that stage 0's "previous" is the diffuse colour, and which
	operations cannot be expressed at all and must be refused.

	Written twice, those decisions drift. One backend gets a fix and the other
	does not, and because the two run on different machines -- WebGPU cannot
	even be tested in this container -- the divergence is invisible until
	something renders wrong on hardware nobody has in front of them.

	So the operation table lives here, once, and the languages differ only in
	how they SPELL things: what a vec4 constructor is called, how a local is
	declared, how a texture is sampled. That is SCryShaderDialect, and it is
	deliberately small. If something bigger than spelling has to differ, that
	is a sign the difference is real and belongs in the backend.

	WHAT IS EMITTED

	Only the body: the per-stage texel declarations, each stage's colour and
	alpha expressions, the accumulator they fold into, and the alpha-test
	discard. The surrounding boilerplate -- version directives, bindings,
	varyings, the vertex stage, and how the final colour leaves the shader --
	is each backend's own, because none of it is shared in any useful sense.

	The caller can rely on a local named "acc" holding the result.
*/

#include <stddef.h>
#include <string>

#include "CryPassDesc.h"

//! How one shading language spells the handful of things the operation table
//! needs. Everything here is syntax; nothing here is a decision.
struct SCryShaderDialect
{
	const char*	szVec4;			//!< vec4 constructor: "vec4f" or "vec4"
	const char*	szVec3;			//!< vec3 constructor: "vec3f" or "vec3"

	//! Declaration keywords. A language without the distinction (GLSL) uses
	//! its type name for both.
	const char*	szDeclConst;	//!< "let"  or "vec4"
	const char*	szDeclVar;		//!< "var"  or "vec4"

	const char*	szDiffuse;		//!< where the interpolated colour comes from
	const char*	szConstColor;	//!< where the pass's constant colour comes from

	//! Writes the expression that samples stage nStage's texture. A function
	//! rather than a format string because the two languages do not even agree
	//! on how many things a sample takes: WGSL wants a texture and a sampler,
	//! GLSL one combined sampler object.
	void (*pfnSample)(int nStage, char* szOut, size_t nSize);
};

//! Emit the stage chain for a pass.
//!
//! Returns false and fills sError if any stage uses an operation this
//! translation cannot express, naming the stage and the operation. sOut is
//! untouched on failure.
bool CryPassGen_Body(const SCryPassDesc& desc, const SCryShaderDialect& dialect,
                     std::string& sOut, std::string& sError);

//! The name of the local holding a stage's sampled texel.
//!
//! Per stage, not one shared name. Every stage's texel lives in the same
//! function scope, so a single "texel" makes the second stage a redeclaration
//! and the whole shader fails to compile -- which is a runtime failure in the
//! browser, long after this code has said the translation succeeded. That was
//! a real bug, and tests/test_shadergen.cpp now checks the emitted code's
//! structure because of it.
std::string CryPassGen_TexelName(int nStage);

#endif //_CRY_PASS_GEN_H_
