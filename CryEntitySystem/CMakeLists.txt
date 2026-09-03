################################################################################
# CryEntitySystem -- the entity component system: spawning, persistence, physics and
# render binding, script glue.
#
# Compiles, does not link: like Cry3DEngine it calls into modules that are not
# ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryEntitySystem
    SOURCES
    CryEntityDLL.cpp
    Entity.cpp
    EntityCamera.cpp
    EntityCreation.cpp
    EntityIt.cpp
    EntityPesistence.cpp
    EntityRender.cpp
    EntitySystem.cpp
    IDGenerator.cpp
    LipSync.cpp
    RandomExprLoadSink.cpp
    stdafx.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"

    DEPENDS
        CryCommon
)
