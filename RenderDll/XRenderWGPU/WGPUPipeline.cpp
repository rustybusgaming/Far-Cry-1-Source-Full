////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WGPUPipeline.cpp
//  Description: The pipeline cache. See WGPUPipeline.h.
//
////////////////////////////////////////////////////////////////////////////

#include "WGPUPipeline.h"

#include <platform.h>
#include <IRenderer.h>
#include <VertexFormats.h>

#include <string.h>

//////////////////////////////////////////////////////////////////////////
//! The key is pure, so it lives outside the Emscripten guard and is tested
//! natively with the translators it is built from.
//////////////////////////////////////////////////////////////////////////
unsigned long long WGPUPipeline_Key(const SWGPUPipelineDesc& desc)
{
	unsigned long long h = 1469598103934665603ULL;

	#define MIX(v) do { \
		unsigned long long _v = (unsigned long long)(v); \
		for (int _b = 0; _b < 8; ++_b) { h ^= (_v >> (_b * 8)) & 0xFF; h *= 1099511628211ULL; } \
	} while (0)

	// Both sub-keys, so a change to either translator's inputs is reflected
	// here without this function having to know their fields.
	MIX(WGPUShaderGen_Key(desc.shader));
	MIX(WGPUStateGen_Key(desc.state));

	// The vertex layout and the topology are baked into the pipeline too, so
	// they are as much a part of its identity as the shader is.
	MIX(desc.nVertexFormat);
	MIX(desc.nPrimType);

	#undef MIX

	return h;
}

#if defined(__EMSCRIPTEN__)

#include <map>
#include <string>

#include <ILog.h>

//! Declared rather than pulled in with RenderPCH.h. This file's key function is
//! pure and is compiled natively for the tests, so the header stays free of the
//! renderer's precompiled-header world; only this device-dependent half needs
//! the module's log.
extern ILog* iLog;

extern WGPUDevice        WGPUContext_Device();
extern WGPUTextureFormat WGPUContext_Format();

static std::map<unsigned long long, WGPURenderPipeline> g_cache;

//////////////////////////////////////////////////////////////////////////

static WGPUBlendFactor ToBlendFactor(EWGPUBlendFactor e)
{
	switch (e)
	{
	case eWGPUBlend_Zero:				return WGPUBlendFactor_Zero;
	case eWGPUBlend_One:				return WGPUBlendFactor_One;
	case eWGPUBlend_SrcColor:			return WGPUBlendFactor_Src;
	case eWGPUBlend_OneMinusSrcColor:	return WGPUBlendFactor_OneMinusSrc;
	case eWGPUBlend_DstColor:			return WGPUBlendFactor_Dst;
	case eWGPUBlend_OneMinusDstColor:	return WGPUBlendFactor_OneMinusDst;
	case eWGPUBlend_SrcAlpha:			return WGPUBlendFactor_SrcAlpha;
	case eWGPUBlend_OneMinusSrcAlpha:	return WGPUBlendFactor_OneMinusSrcAlpha;
	case eWGPUBlend_DstAlpha:			return WGPUBlendFactor_DstAlpha;
	case eWGPUBlend_OneMinusDstAlpha:	return WGPUBlendFactor_OneMinusDstAlpha;
	case eWGPUBlend_SrcAlphaSaturated:	return WGPUBlendFactor_SrcAlphaSaturated;
	default:							return WGPUBlendFactor_One;
	}
}

static WGPUCompareFunction ToCompare(EWGPUCompare e)
{
	switch (e)
	{
	case eWGPUCompare_Never:		return WGPUCompareFunction_Never;
	case eWGPUCompare_Less:			return WGPUCompareFunction_Less;
	case eWGPUCompare_Equal:		return WGPUCompareFunction_Equal;
	case eWGPUCompare_LessEqual:	return WGPUCompareFunction_LessEqual;
	case eWGPUCompare_Greater:		return WGPUCompareFunction_Greater;
	case eWGPUCompare_Always:		return WGPUCompareFunction_Always;
	default:						return WGPUCompareFunction_LessEqual;
	}
}

static WGPUPrimitiveTopology ToTopology(int nPrimType, bool& bOk)
{
	bOk = true;
	switch (nPrimType)
	{
	case R_PRIMV_TRIANGLES:			return WGPUPrimitiveTopology_TriangleList;
	case R_PRIMV_TRIANGLE_STRIP:	return WGPUPrimitiveTopology_TriangleStrip;
	default:
		// WebGPU has no triangle fan and no quads. Both have to be converted
		// to a triangle list before they reach a pipeline, which is the
		// drawing code's job -- see the GLES backend's quad expansion.
		bOk = false;
		return WGPUPrimitiveTopology_TriangleList;
	}
}

//////////////////////////////////////////////////////////////////////////
//! Describe the vertex layout from the engine's own tables, exactly as the
//! GLES backend does. Same reasoning: m_VertexSize[] and gBufInfoTable[]
//! already describe all seventeen formats, so there is no switch to write and
//! no format to forget. Offset 0 means "absent"; position is always at 0.
//////////////////////////////////////////////////////////////////////////
static int BuildVertexAttributes(int nFormat, WGPUVertexAttribute* pAttrs, int nMax)
{
	if (nFormat <= 0 || nFormat >= VERTEX_FORMAT_NUMS || nMax < 3)
		return 0;

	const SBufInfoTable* pOffs = &gBufInfoTable[nFormat];
	int n = 0;

	pAttrs[n].format         = WGPUVertexFormat_Float32x3;
	pAttrs[n].offset         = 0;
	pAttrs[n].shaderLocation = 0;
	++n;

	// Unorm8x4 gives the shader 0..1, matching the normalised attribute the
	// GLES backend asks for. The bytes are B,G,R,A and the shader swizzles.
	if (pOffs->OffsColor)
	{
		pAttrs[n].format         = WGPUVertexFormat_Unorm8x4;
		pAttrs[n].offset         = (uint64_t)pOffs->OffsColor;
		pAttrs[n].shaderLocation = 1;
		++n;
	}

	if (pOffs->OffsTC)
	{
		pAttrs[n].format         = WGPUVertexFormat_Float32x2;
		pAttrs[n].offset         = (uint64_t)pOffs->OffsTC;
		pAttrs[n].shaderLocation = 2;
		++n;
	}

	return n;
}

//////////////////////////////////////////////////////////////////////////

static WGPURenderPipeline Build(const SWGPUPipelineDesc& desc)
{
	WGPUDevice device = WGPUContext_Device();
	if (!device)
		return 0;

	//////////////////////////////////////////////////////////////////////
	// The shader.
	//////////////////////////////////////////////////////////////////////
	std::string sWGSL, sError;
	if (!WGPUShaderGen_Build(desc.shader, sWGSL, sError))
	{
		iLog->LogError("XRenderWGPU: cannot translate pass: %s", sError.c_str());
		return 0;
	}

	WGPUShaderSourceWGSL wgsl;
	memset(&wgsl, 0, sizeof(wgsl));
	wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgsl.code.data   = sWGSL.c_str();
	wgsl.code.length = sWGSL.size();

	WGPUShaderModuleDescriptor smDesc;
	memset(&smDesc, 0, sizeof(smDesc));
	smDesc.nextInChain = (WGPUChainedStruct*)&wgsl;

	WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &smDesc);
	if (!module)
	{
		iLog->LogError("XRenderWGPU: shader module creation failed");
		return 0;
	}

	//////////////////////////////////////////////////////////////////////
	// Bind group layout: the uniform buffer, then a sampler and texture per
	// textured stage. Must match the bindings WGPUShaderGen emits.
	//////////////////////////////////////////////////////////////////////
	WGPUBindGroupLayoutEntry entries[1 + 2 * SWGPUShaderDesc::kMaxStages];
	memset(entries, 0, sizeof(entries));
	int nEntries = 0;

	entries[nEntries].binding    = 0;
	entries[nEntries].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
	entries[nEntries].buffer.type = WGPUBufferBindingType_Uniform;
	++nEntries;

	int nBinding = 1;
	for (int i = 0; i < desc.shader.nStages; ++i)
	{
		if (!desc.shader.stages[i].bHasTexture)
			continue;

		entries[nEntries].binding       = nBinding;
		entries[nEntries].visibility    = WGPUShaderStage_Fragment;
		entries[nEntries].sampler.type  = WGPUSamplerBindingType_Filtering;
		++nEntries;

		entries[nEntries].binding                = nBinding + 1;
		entries[nEntries].visibility             = WGPUShaderStage_Fragment;
		entries[nEntries].texture.sampleType     = WGPUTextureSampleType_Float;
		entries[nEntries].texture.viewDimension  = WGPUTextureViewDimension_2D;
		++nEntries;

		nBinding += 2;
	}

	WGPUBindGroupLayoutDescriptor bglDesc;
	memset(&bglDesc, 0, sizeof(bglDesc));
	bglDesc.entryCount = (size_t)nEntries;
	bglDesc.entries    = entries;

	// Every wgpuDeviceCreate* can return null -- WebGPU reports failure through
	// the returned handle, there is no glGetError to ask afterwards. An
	// unchecked null is passed straight into the next call and surfaces as a
	// browser-side validation error naming a line that is not the cause.
	WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
	if (!bgl)
	{
		iLog->LogError("XRenderWGPU: bind group layout creation failed");
		wgpuShaderModuleRelease(module);
		return 0;
	}

	WGPUPipelineLayoutDescriptor plDesc;
	memset(&plDesc, 0, sizeof(plDesc));
	plDesc.bindGroupLayoutCount = 1;
	plDesc.bindGroupLayouts     = &bgl;

	WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
	if (!layout)
	{
		iLog->LogError("XRenderWGPU: pipeline layout creation failed");
		wgpuBindGroupLayoutRelease(bgl);
		wgpuShaderModuleRelease(module);
		return 0;
	}

	//////////////////////////////////////////////////////////////////////
	// Vertex layout.
	//////////////////////////////////////////////////////////////////////
	WGPUVertexAttribute attrs[3];
	memset(attrs, 0, sizeof(attrs));
	const int nAttrs = BuildVertexAttributes(desc.nVertexFormat, attrs, 3);
	if (!nAttrs)
	{
		iLog->LogError("XRenderWGPU: unknown vertex format %d", desc.nVertexFormat);
		wgpuPipelineLayoutRelease(layout);
		wgpuBindGroupLayoutRelease(bgl);
		wgpuShaderModuleRelease(module);
		return 0;
	}

	WGPUVertexBufferLayout vbLayout;
	memset(&vbLayout, 0, sizeof(vbLayout));
	vbLayout.arrayStride    = (uint64_t)m_VertexSize[desc.nVertexFormat];
	vbLayout.stepMode       = WGPUVertexStepMode_Vertex;
	vbLayout.attributeCount = (size_t)nAttrs;
	vbLayout.attributes     = attrs;

	//////////////////////////////////////////////////////////////////////
	// Blend and colour target.
	//////////////////////////////////////////////////////////////////////
	WGPUBlendState blend;
	memset(&blend, 0, sizeof(blend));
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = ToBlendFactor(desc.state.eSrcFactor);
	blend.color.dstFactor = ToBlendFactor(desc.state.eDstFactor);
	// The engine has one pair of factors for both colour and alpha; it predates
	// separate alpha blending.
	blend.alpha = blend.color;

	WGPUColorTargetState target;
	memset(&target, 0, sizeof(target));
	target.format = WGPUContext_Format();
	target.blend  = desc.state.bBlendEnabled ? &blend : 0;

	target.writeMask = WGPUColorWriteMask_None;
	if (desc.state.nColorWriteMask & 1) target.writeMask |= WGPUColorWriteMask_Red;
	if (desc.state.nColorWriteMask & 2) target.writeMask |= WGPUColorWriteMask_Green;
	if (desc.state.nColorWriteMask & 4) target.writeMask |= WGPUColorWriteMask_Blue;
	if (desc.state.nColorWriteMask & 8) target.writeMask |= WGPUColorWriteMask_Alpha;

	WGPUFragmentState fragment;
	memset(&fragment, 0, sizeof(fragment));
	fragment.module      = module;
	fragment.entryPoint.data   = "fs_main";
	fragment.entryPoint.length = strlen("fs_main");
	fragment.targetCount = 1;
	fragment.targets     = &target;

	//////////////////////////////////////////////////////////////////////
	// The pipeline.
	//////////////////////////////////////////////////////////////////////
	WGPURenderPipelineDescriptor rpDesc;
	memset(&rpDesc, 0, sizeof(rpDesc));
	rpDesc.layout = layout;

	rpDesc.vertex.module            = module;
	rpDesc.vertex.entryPoint.data   = "vs_main";
	rpDesc.vertex.entryPoint.length = strlen("vs_main");
	rpDesc.vertex.bufferCount       = 1;
	rpDesc.vertex.buffers           = &vbLayout;

	bool bTopologyOk = false;
	rpDesc.primitive.topology  = ToTopology(desc.nPrimType, bTopologyOk);
	if (!bTopologyOk)
		iLog->LogWarning("XRenderWGPU: primitive type %d has no WebGPU topology; "
		                 "drawn as a triangle list", desc.nPrimType);

	// The engine's winding and culling are handled by the drawing code rather
	// than baked in here, so a pass does not need a second pipeline purely to
	// flip a cull mode.
	rpDesc.primitive.cullMode  = WGPUCullMode_None;
	rpDesc.primitive.frontFace = WGPUFrontFace_CCW;

	rpDesc.multisample.count = 1;
	rpDesc.multisample.mask  = 0xFFFFFFFF;

	// No depth attachment is created yet -- see WGPUFrame.cpp -- so a pipeline
	// must not declare depth state either, or it will not match the render
	// pass. The decoded depth state is still part of the cache key, so this
	// becomes correct as soon as a depth buffer exists.
	rpDesc.depthStencil = 0;

	rpDesc.fragment = &fragment;

	WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &rpDesc);

	// The pipeline holds references to all of these.
	wgpuPipelineLayoutRelease(layout);
	wgpuBindGroupLayoutRelease(bgl);
	wgpuShaderModuleRelease(module);

	if (!pipeline)
		iLog->LogError("XRenderWGPU: render pipeline creation failed");

	return pipeline;
}

//////////////////////////////////////////////////////////////////////////

WGPURenderPipeline WGPUPipeline_Get(const SWGPUPipelineDesc& desc)
{
	const unsigned long long nKey = WGPUPipeline_Key(desc);

	std::map<unsigned long long, WGPURenderPipeline>::iterator it = g_cache.find(nKey);
	if (it != g_cache.end())
		return it->second;

	WGPURenderPipeline pipeline = Build(desc);

	// Cached even on failure, as a null. Otherwise a pass that cannot be
	// translated would try again -- and log again -- on every single draw.
	g_cache[nKey] = pipeline;

	if (pipeline)
		iLog->Log("XRenderWGPU: built pipeline %d (key %llx)",
		          (int)g_cache.size(), nKey);

	return pipeline;
}

void WGPUPipeline_Shutdown()
{
	for (std::map<unsigned long long, WGPURenderPipeline>::iterator it = g_cache.begin();
	     it != g_cache.end(); ++it)
	{
		if (it->second)
			wgpuRenderPipelineRelease(it->second);
	}
	g_cache.clear();
}

int WGPUPipeline_CacheSize()
{
	return (int)g_cache.size();
}

#endif //__EMSCRIPTEN__
