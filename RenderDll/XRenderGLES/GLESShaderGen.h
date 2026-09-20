#ifndef _CRY_GLES_SHADERGEN_H_
#define _CRY_GLES_SHADERGEN_H_

/*!
	GLESShaderGen -- turning a CryEngine shader pass into GLSL ES 3.00.

	The pass itself is described by Common/CryPassDesc.h and the stage chain is
	emitted by Common/CryPassGen.cpp, both shared with the WebGPU backend. What
	is here is only GLSL's own boilerplate: the version directive, precision,
	uniforms, varyings and the two entry points.

	WHY THIS MATTERS MORE THAN THE WGSL ONE

	Not because WebGL2 is more important -- because it can be CHECKED. This
	container cannot obtain a WebGPU adapter, so the WGSL the other backend
	emits has never been compiled by a driver. WebGL2 works here and in every
	current browser, so a pass translated through this file can be drawn and
	read back pixel by pixel.

	Since both emitters consume the same description and the same operation
	table, a pixel test through this file is evidence about the shared decisions
	the WGSL path also depends on. What it cannot check is anything above the
	shared layer, which is exactly why the shared layer was made as large as it
	sensibly could be.

	NO GL IN THIS FILE

	Emitting a string needs no context, so this compiles and is tested natively,
	in tests/test_glesshadergen.cpp -- like the WGSL generator, and for the same
	reason.
*/

#include <string>

#include "CryPassDesc.h"

//! Emit the vertex and fragment sources for a pass.
//!
//! Two strings, not one: GL compiles the stages separately. Both are untouched
//! on failure.
//!
//! Returns false and fills sError if any stage uses an operation this
//! translation cannot express, naming the stage and the operation.
bool GLESShaderGen_Build(const SCryPassDesc& desc,
                         std::string& sVertexOut, std::string& sFragmentOut,
                         std::string& sError);

//! The sampler uniform name for a stage, e.g. "uTex0". The backend needs it to
//! resolve the uniform location, and it must agree with what the generator
//! emitted -- so both come from here.
std::string GLESShaderGen_SamplerName(int nStage);

#endif //_CRY_GLES_SHADERGEN_H_
