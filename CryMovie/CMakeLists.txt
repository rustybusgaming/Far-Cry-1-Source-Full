################################################################################
# CryMovie -- the cutscene sequencer: animation tracks, splines, entity and
# camera nodes.
#
# Compiles, does not link: like Cry3DEngine it calls into modules that are not
# ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryMovie
    SOURCES
    AnimCameraNode.cpp
    AnimNode.cpp
    AnimSequence.cpp
    AnimSplineTrack.cpp
    AnimTrack.cpp
    BoolTrack.cpp
    CVarNode.cpp
    CharacterTrack.cpp
    ConsoleTrack.cpp
    CryMovie.cpp
    EntityNode.cpp
    EventTrack.cpp
    ExprTrack.cpp
    MaterialNode.cpp
    Movie.cpp
    MusicTrack.cpp
    SceneNode.cpp
    ScriptVarNode.cpp
    SelectTrack.cpp
    SequenceIt.cpp
    SoundTrack.cpp
    StdAfx.cpp
    xml/xml.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/xml"
        "${CMAKE_CURRENT_SOURCE_DIR}/xml/Expat"
    DEPENDS
        CryCommon
)
