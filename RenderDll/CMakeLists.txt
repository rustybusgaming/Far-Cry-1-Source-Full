################################################################################
# RenderDll -- the renderer.
#
# Two targets so far, chosen deliberately in this order:
#
#   CryRenderCommon  the backend-INDEPENDENT renderer: shaders, textures,
#                    render elements, the image loaders. 81 sources. Every
#                    backend needs it, so it is the prerequisite for any of
#                    them.
#
#   CryRenderNULL    Crytek's null renderer, 11 sources and ~2.4k lines,
#                    shipped for the dedicated server. It implements the full
#                    IRenderer interface and draws nothing.
#
# XRenderNULL is the important one. It is what makes a HEADLESS build possible
# -- the engine can link and run with no GL, no D3D, no Cg and no shader
# rewrite -- which is Milestone 3, and it proves out everything except the
# rasteriser. Doing it before touching XRenderOGL means the WebGL2 work starts
# from a running engine instead of a compiling one.
#
# Not built yet:
#   XRenderOGL   71k lines of OpenGL 1.x -- immediate mode, NV register
#                combiners, WGL context creation and Cg shaders whose cgGL.lib
#                is a binary blob. This is the Milestone 4 rewrite to WebGL2.
#   XRenderD3D8/D3D9   Direct3D; no path to the web at all.
#   XRenderPS2         console backend.
################################################################################

set(CRY_RENDER_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/Textures"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/Textures/Image"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/Shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/RendElements"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/NvTriStrip"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/3Dc"
)

cry_add_module(CryRenderCommon
    SOURCES
    Common/3DUtils.cpp
    Common/CRT.cpp
    Common/EvalFuncs_C.cpp
    Common/EvalFuncs_RE.cpp
    Common/LeafBufferCreate.cpp
    Common/LeafBufferRender.cpp
    Common/LeafBufferSerialize.cpp
    Common/LightMaterial.cpp
    Common/Names.cpp
    Common/NvTriStrip/NvTriStrip.cpp
    Common/NvTriStrip/NvTriStripObjects.cpp
    Common/RendElements/CRE2DQuad.cpp
    Common/RendElements/CREBeam.cpp
    Common/RendElements/CREClientPoly.cpp
    Common/RendElements/CREClientPoly2D.cpp
    Common/RendElements/CREDummy.cpp
    Common/RendElements/CREFlares.cpp
    Common/RendElements/CREFlashBang.cpp
    Common/RendElements/CREGlare.cpp
    Common/RendElements/CREHDRProcess.cpp
    Common/RendElements/CREMotionBlur.cpp
    Common/RendElements/CREOcLeaf.cpp
    Common/RendElements/CREOcean.cpp
    Common/RendElements/CREOclusionQuery.cpp
    Common/RendElements/CREParticleSpray.cpp
    Common/RendElements/CREPolyBlend.cpp
    Common/RendElements/CREPolyMesh.cpp
    Common/RendElements/CREPrefabGeom.cpp
    Common/RendElements/CREScreenProcess.cpp
    Common/RendElements/CREShadowMap.cpp
    Common/RendElements/CRESky.cpp
    Common/RendElements/CRETempMesh.cpp
    Common/RendElements/CRETerrainSector.cpp
    Common/RendElements/CRETriMeshAdditionalShadow.cpp
    Common/RendElements/CRETriMeshShadow.cpp
    Common/RendElements/FFT_SSE.cpp
    Common/RendElements/RendElement.cpp
    Common/Renderer.cpp
    Common/ResFile.cpp
    Common/Shaders/Parser.cpp
    Common/Shaders/ShaderComponents.cpp
    Common/Shaders/ShaderCore.cpp
    Common/Shaders/ShaderParse.cpp
    Common/Shaders/ShaderScript.cpp
    Common/Shaders/ShaderTemplate.cpp
    Common/SimpleFrameProfiler.cpp
    Common/Textures/Image/BmpImage.cpp
    Common/Textures/Image/CImage.cpp
    Common/Textures/Image/DDSImage.cpp
    Common/Textures/Image/JpgImage.cpp
    Common/Textures/Image/JpgImage_XBox.cpp
    Common/Textures/Image/PcxImage.cpp
    Common/Textures/Image/Quantize.cpp
    Common/Textures/Image/TgaImage.cpp
    Common/Textures/Image/XtfImage.cpp
    Common/Textures/Image/inv_cmap.cpp
    Common/Textures/Image/jpeg6/jcomapi.cpp
    Common/Textures/Image/jpeg6/jdapimin.cpp
    Common/Textures/Image/jpeg6/jdapistd.cpp
    Common/Textures/Image/jpeg6/jdatasrc.cpp
    Common/Textures/Image/jpeg6/jdcoefct.cpp
    Common/Textures/Image/jpeg6/jdcolor.cpp
    Common/Textures/Image/jpeg6/jddctmgr.cpp
    Common/Textures/Image/jpeg6/jdhuff.cpp
    Common/Textures/Image/jpeg6/jdinput.cpp
    Common/Textures/Image/jpeg6/jdmainct.cpp
    Common/Textures/Image/jpeg6/jdmarker.cpp
    Common/Textures/Image/jpeg6/jdmaster.cpp
    Common/Textures/Image/jpeg6/jdpostct.cpp
    Common/Textures/Image/jpeg6/jdsample.cpp
    Common/Textures/Image/jpeg6/jdtrans.cpp
    Common/Textures/Image/jpeg6/jerror.cpp
    Common/Textures/Image/jpeg6/jfdctflt.cpp
    Common/Textures/Image/jpeg6/jidctflt.cpp
    Common/Textures/Image/jpeg6/jmemmgr.cpp
    Common/Textures/Image/jpeg6/jmemnobs.cpp
    Common/Textures/Image/jpeg6/jutils.cpp
    Common/Textures/TexMan.cpp
    Common/Textures/TexManShadows.cpp
    Common/Textures/TexManStreaming.cpp
    Common/pip_addons.cpp
    INCLUDES ${CRY_RENDER_INCLUDES}
    DEPENDS  CryCommon
)

cry_add_module(CryRenderNULL
    SOURCES
    XRenderNULL/NULL_Font.cpp
    XRenderNULL/NULL_REOcean.cpp
    XRenderNULL/NULL_RERender.cpp
    XRenderNULL/NULL_RendPipeline.cpp
    XRenderNULL/NULL_Renderer.cpp
    XRenderNULL/NULL_Shaders.cpp
    XRenderNULL/NULL_Shadows.cpp
    XRenderNULL/NULL_System.cpp
    XRenderNULL/NULL_Textures.cpp
    XRenderNULL/NULL_TexturesStreaming.cpp
    XRenderNULL/NULL_VertBuffer.cpp
    INCLUDES ${CRY_RENDER_INCLUDES}
    DEPENDS  CryCommon
)
