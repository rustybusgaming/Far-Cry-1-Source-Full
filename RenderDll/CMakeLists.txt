################################################################################
# RenderDll -- the renderer.
#
# ONE TARGET, NOT TWO.
#
# An earlier version of this file split the renderer into CryRenderCommon (the
# backend-independent code) and CryRenderNULL (the null backend), on the
# reasonable-looking assumption that Common is shared. It is not, and the
# assumption cost a link:
#
#   * Common/Shaders/Shader.h declares CPShader::m_PShaders and
#     CPShader::mfForName() only "#if !defined(NULL_RENDERER)". The definitions
#     live in the BACKEND -- XRenderD3D9/D3DCGPShader.cpp, XRenderOGL/
#     GLCGPShader.cpp -- and XRenderNULL has no equivalent, because with
#     NULL_RENDERER set it does not need one.
#
#   * The same define removes the nvDXTcompress/nvDXTdecompress calls in
#     TexMan.cpp and Renderer.cpp, the ATI 3Dc compressor calls in TexMan.cpp,
#     and the CImageJpgFile construction in CImage.cpp -- all of which
#     otherwise demand binary blobs (nvDXTlib.lib, CompressATI.lib, ijl15.lib)
#     that are Windows-only and cannot be part of a web build.
#
# So Common is not a library; it is a body of source each backend compiles into
# itself with its own defines. Crytek's own XRenderNULL.vcproj lists all 67
# files -- Common and XRenderNULL together -- in one project with
# NULL_RENDERER in PreprocessorDefinitions. This file now mirrors that exactly,
# and the source list below is generated from that .vcproj rather than guessed.
#
# When XRenderOGL is brought up it gets its OWN target compiling the same
# Common sources again, without NULL_RENDERER and with the GL backend's
# defines. That duplication is the design, not an accident of it.
#
# WHY THE NULL BACKEND FIRST
#
# It is what makes a headless build possible -- the engine links and runs with
# no GL, no D3D, no Cg and no shader rewrite -- so it proves out everything
# except the rasteriser. Doing it before touching XRenderOGL means the WebGL2
# work starts from a running engine instead of a compiling one.
#
# Not built yet:
#   XRenderOGL         45.7k lines of OpenGL 1.x -- immediate mode, NV register
#                      combiners, WGL context creation and Cg shaders whose
#                      cgGL.lib is a binary blob. The WebGL2 rewrite.
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
    "${CMAKE_CURRENT_SOURCE_DIR}/XRenderNULL"
)

# ------------------------------------------------------------------------------
# The source list, in the order XRenderNULL.vcproj gives it: the shared Common
# sources first, then the null backend's own.
# ------------------------------------------------------------------------------
cry_add_module(CryRenderNULL
    SOURCES
    RenderPCH.cpp
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
    Common/RendElements/CREGlare.cpp
    Common/RendElements/CREHDRProcess.cpp
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
    Common/Textures/Image/BmpImage.cpp
    Common/Textures/Image/CImage.cpp
    Common/Textures/Image/DDSImage.cpp
    Common/Textures/Image/JpgImage.cpp
    Common/Textures/Image/PcxImage.cpp
    Common/Textures/Image/Quantize.cpp
    Common/Textures/Image/TgaImage.cpp
    Common/Textures/Image/inv_cmap.cpp
    Common/Textures/TexMan.cpp
    Common/Textures/TexManShadows.cpp
    Common/Textures/TexManStreaming.cpp
    Common/pip_addons.cpp
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

# The define that makes all of the above a NULL renderer rather than a backend
# with no backend. See the header comment.
target_compile_definitions(CryRenderNULL PRIVATE NULL_RENDERER)

# ------------------------------------------------------------------------------
# Sources in Common/ that XRenderNULL.vcproj does NOT list, and why:
#
#   Textures/Image/XtfImage.cpp       ATI .xtf loader; needs CompressATI.lib.
#   Textures/Image/JpgImage_XBox.cpp  Xbox-only variant of JpgImage.cpp.
#   Textures/Image/jpeg6/*.cpp        libjpeg 6, reached only from the PS2
#                                     branch of JpgImage.cpp.
#   RendElements/CREFlashBang.cpp     }  present in the tree but in no
#   RendElements/CREMotionBlur.cpp    }  shipped project file; post-process
#   SimpleFrameProfiler.cpp           }  effects and a profiler that appear to
#                                        have been cut before release.
#
# They still COMPILE -- tools/triage.py covers them -- they are simply not part
# of this backend's link.
# ------------------------------------------------------------------------------


################################################################################
# CryRenderGLES -- the WebGL2 backend, under construction.
#
# Same Common sources as the null backend, plus every one of XRenderNULL's own
# sources EXCEPT NULL_System.cpp, plus this backend's files on top.
#
# The exclusion is the mechanism, not an oversight. NULL_System.cpp holds
# CNULLRenderer's system and context layer -- Init, ShutDown, the context
# calls, resolution, gamma -- and CGLESRenderer derives from CNULLRenderer.
# Leaving that one file out means those thirteen methods have no definition in
# this target, so the link fails until XRenderGLES supplies them. They are
# exactly the ones a real backend cannot inherit.
#
# Everything else falls back to Crytek's own draw-nothing implementation, which
# is the correct behaviour for a backend entry point that is not written yet,
# and makes progress measurable: what is real is what CGLESRenderer overrides.
# When the last subsystem moves from inherited to overridden, the XRenderNULL
# sources drop out of this list and the backend stands alone.
#
# NULL_RENDERER stays defined. It is what removes the references to
# CPShader::mfForName, the nvDXT/ATI compressors and the Intel JPEG library
# (see the header comment above) -- none of which can exist in a web build, and
# none of which the GLES shading path will use, since that is being written
# against GLSL ES rather than the assembly and register-combiner pipeline those
# guards belong to.
################################################################################

if(EMSCRIPTEN)
    cry_add_module(CryRenderGLES
        SOURCES
    RenderPCH.cpp
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
    Common/RendElements/CREGlare.cpp
    Common/RendElements/CREHDRProcess.cpp
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
    Common/Textures/Image/BmpImage.cpp
    Common/Textures/Image/CImage.cpp
    Common/Textures/Image/DDSImage.cpp
    Common/Textures/Image/JpgImage.cpp
    Common/Textures/Image/PcxImage.cpp
    Common/Textures/Image/Quantize.cpp
    Common/Textures/Image/TgaImage.cpp
    Common/Textures/Image/inv_cmap.cpp
    Common/Textures/TexMan.cpp
    Common/Textures/TexManShadows.cpp
    Common/Textures/TexManStreaming.cpp
    Common/pip_addons.cpp
    XRenderNULL/NULL_Font.cpp
    XRenderNULL/NULL_REOcean.cpp
    XRenderNULL/NULL_RERender.cpp
    XRenderNULL/NULL_RendPipeline.cpp
    XRenderNULL/NULL_Renderer.cpp
    XRenderNULL/NULL_Shaders.cpp
    XRenderNULL/NULL_Shadows.cpp
    XRenderNULL/NULL_Textures.cpp
    XRenderNULL/NULL_TexturesStreaming.cpp
    XRenderNULL/NULL_VertBuffer.cpp
        XRenderGLES/GLESContext.cpp
        XRenderGLES/GLESSystem.cpp
        XRenderGLES/GLESFrame.cpp
        INCLUDES ${CRY_RENDER_INCLUDES}
                 "${CMAKE_CURRENT_SOURCE_DIR}/XRenderGLES"
        DEPENDS  CryCommon
    )
    target_compile_definitions(CryRenderGLES PRIVATE NULL_RENDERER)
else()
    # No GL on this target: no /usr/include/GL, no EGL. The backend exists for
    # WebGL2, and a native GLES path would need EGL with a surfaceless platform
    # extension -- worth adding only as a debugging aid. See
    # XRenderGLES/GLESContext.cpp for the non-Emscripten branch.
    message(STATUS "cry: CryRenderGLES skipped (needs Emscripten)")
endif()
