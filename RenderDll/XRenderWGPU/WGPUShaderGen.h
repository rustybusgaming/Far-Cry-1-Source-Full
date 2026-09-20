#ifndef _CRY_WGPU_SHADERGEN_H_
#define _CRY_WGPU_SHADERGEN_H_

/*!
	WGPUShaderGen -- turning a CryEngine shader pass into WGSL.

	The pass itself is described by Common/CryPassDesc.h, which is shared with
	the WebGL2 backend. Read that first: it says what the texture-stage model
	is and why only one of the three things called "Far Cry's shaders" is
	covered by it.

	This file is only the WGSL half. GLESShaderGen is the same translation
	emitted as GLSL ES, from the same description -- which is the point of
	sharing it. Every decision about what an operation MEANS is made once, in
	terms both emitters read, so the backend that can be pixel-verified in a
	browser is checking the same decisions as the one that cannot.

	WHY THIS FILE HAS NO WEBGPU IN IT

	Emitting WGSL is a pure function: a description in, a string out. It needs
	no device, no adapter and no browser -- so it compiles natively and is
	unit-tested natively, in tests/test_shadergen.cpp.

	That matters more than it sounds. The container this was written in cannot
	obtain a WebGPU adapter, so nothing that touches the GPU can be verified
	here. Keeping the translation separate from the execution means the part
	with all the decisions in it is fully testable anyway, and what remains
	unverified is the comparatively mechanical business of handing a string to
	the driver.

	ON GUESSING

	Several operations need a third argument or state this layer is not given.
	Those are reported as unsupported, by name, rather than approximated. A
	shader that fails to build is a visible problem; a shader that silently
	computes something else is a bug that takes days to find.
*/

#include <string>

#include "CryPassDesc.h"

//! Emit WGSL for a pass.
//!
//! Returns false and fills sError if any stage uses an operation this
//! translation cannot express. sOut is untouched on failure.
bool WGPUShaderGen_Build(const SCryPassDesc& desc, std::string& sOut, std::string& sError);

#endif //_CRY_WGPU_SHADERGEN_H_
