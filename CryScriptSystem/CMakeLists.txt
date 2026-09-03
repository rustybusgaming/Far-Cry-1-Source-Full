################################################################################
# CryScriptSystem -- the Lua binding layer and script object model.
#
# Compiles, does not link: like Cry3DEngine it calls into modules that are not
# ported yet. Compiling is the milestone.
################################################################################

cry_add_module(CryScriptSystem
    SOURCES
    CryScriptSystem.cpp
    FunctionHandler.cpp
    LuaCryPakIO.cpp
    MemStats.cpp
    RecycleAllocator.cpp
    ScriptBuffer.cpp
    ScriptObject.cpp
    ScriptSystem.cpp
    StdAfx.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/LUA"
        "${CMAKE_CURRENT_SOURCE_DIR}/LUA/lib"
    DEPENDS
        CryCommon
)
