################################################################################
# Cry3DEngine -- the scene renderer's front end: terrain, vegetation, visareas,
# static objects, decals, particles, lightmaps.
#
# All 73 translation units compile. Unlike CryCommon and CrySystem this module
# needed almost no shimming: once the include spellings were corrected it was
# four root causes and a handful of ill-formed constructs MSVC 7.1 accepted.
#
# It does NOT link yet, and is not meant to. Cry3DEngine calls into the
# renderer (IRenderer), CryPhysics and CryAnimation, none of which are ported.
# Compiling is the milestone; linking waits for those modules.
################################################################################

cry_add_module(Cry3DEngine
    SOURCES
    3DEngineLight.cpp
    3DEngineRender.cpp
    3DEngineScreenEffects.cpp
    3dEngine.cpp
    3dEngineLoad.cpp
    BasicArea.cpp
    Brush.cpp
    BrushLM.cpp
    Cry3DEngine.cpp
    CryStaticModel.cpp
    Decal.cpp
    DecalManager.cpp
    File.cpp
    Geom.cpp
    Helper.cpp
    LMSerializationManager2.cpp
    Light.cpp
    LightLoad.cpp
    LightMan.cpp
    MatMan.cpp
    Meshidx.cpp
    Node.cpp
    ObjMan.cpp
    ObjManDraw.cpp
    ObjManDrawEntity.cpp
    ObjManFar.cpp
    ObjManPhys.cpp
    ObjManShadows.cpp
    ObjManStreaming.cpp
    ParticleEffect.cpp
    ParticleEmitter.cpp
    SaveCFG.cpp
    SpriteManager.cpp
    StatObjCompiler.cpp
    StatObjConstr.cpp
    StatObjFar.cpp
    StatObjLoad.cpp
    StatObjPhys.cpp
    StatObjRend.cpp
    StatObjSerialize.cpp
    StatObjShadow.cpp
    StatObjShadowVolumes.cpp
    StatObjStream.cpp
    StdAfx.cpp
    StencilShadowConnectivityBuilder.cpp
    StencilShadowEdgeDetector.cpp
    Vegetation.cpp
    VisAreaMan.cpp
    VisAreas.cpp
    WaterVolumes.cpp
    bflyes.cpp
    cbuffer.cpp
    cvars.cpp
    detail_grass.cpp
    particle.cpp
    partman.cpp
    partpolygon.cpp
    rain.cpp
    terrain.cpp
    terrain_damage.cpp
    terrain_det_tex.cpp
    terrain_hmap.cpp
    terrain_init.cpp
    terrain_light.cpp
    terrain_load.cpp
    terrain_render.cpp
    terrain_sector.cpp
    terrain_sector_beach.cpp
    terrain_sector_render.cpp
    terrain_sector_tex.cpp
    terrain_tex_pool.cpp
    terrain_water_quad.cpp
    terran_edit.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        CryCommon
)

# Compile-only for now: nothing resolves its renderer/physics/animation
# externals until those modules are ported.
set_target_properties(Cry3DEngine PROPERTIES EXCLUDE_FROM_ALL FALSE)
