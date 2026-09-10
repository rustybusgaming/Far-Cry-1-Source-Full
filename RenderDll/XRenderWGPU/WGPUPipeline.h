#ifndef _CRY_WGPU_PIPELINE_H_
#define _CRY_WGPU_PIPELINE_H_

/*!
	WGPUPipeline -- the pipeline cache.

	WHY A CACHE IS NOT OPTIONAL HERE

	WebGPU bakes almost everything into an immutable render pipeline object:
	both shader modules, the vertex layout, the blend and depth state, the
	primitive topology. Creating one compiles shaders and can take milliseconds.
	Nothing may create a pipeline per draw.

	The engine, meanwhile, changes state constantly -- it was written for an API
	where a blend mode was a function call. So every distinct combination has to
	be built once and looked up thereafter, keyed on everything that went into
	it. This file is that lookup.

	It is also where the two pure translators meet: WGPUShaderGen turns the
	pass's texture stages into WGSL, WGPUStateGen turns its render-state word
	into blend and depth state, and this combines them into one pipeline. Those
	two carry the decisions and are unit-tested natively; this file is the
	mechanical part that needs a device, and is the part that cannot be verified
	without a GPU.

	KEY DISCIPLINE

	A key that is missing a field is the worst kind of bug here: two passes that
	should differ silently share a pipeline, and one renders with the other's
	shader or blend mode. The key is therefore built from the sub-keys of both
	translators plus the vertex format and topology, and nothing is added to a
	description without being added to a key.
*/

#include "WGPUShaderGen.h"
#include "WGPUStateGen.h"

//! Everything that makes one pipeline different from another.
struct SWGPUPipelineDesc
{
	SWGPUShaderDesc	shader;
	SWGPUStateDesc	state;

	int				nVertexFormat;	//!< the engine's VERTEX_FORMAT_* value
	int				nPrimType;		//!< R_PRIMV_*

	SWGPUPipelineDesc() : nVertexFormat(0), nPrimType(0) {}
};

//! The cache key. Pure, so it is tested natively along with the translators.
unsigned long long WGPUPipeline_Key(const SWGPUPipelineDesc& desc);

#if defined(__EMSCRIPTEN__)

#include <webgpu/webgpu.h>

//! Fetch or build. Returns 0 if the shader could not be translated or the
//! pipeline could not be created, having logged why.
WGPURenderPipeline WGPUPipeline_Get(const SWGPUPipelineDesc& desc);

//! Release every cached pipeline. The objects belong to the device, so this
//! must happen before it goes.
void WGPUPipeline_Shutdown();

//! How many pipelines are cached. For logging, and for a test to show that a
//! repeated description does not build a second one.
int WGPUPipeline_CacheSize();

#endif //__EMSCRIPTEN__

#endif //_CRY_WGPU_PIPELINE_H_
