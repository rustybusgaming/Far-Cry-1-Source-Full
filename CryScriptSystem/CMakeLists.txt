################################################################################
# CryScriptSystem -- the Lua binding layer and script object model.
#
# The Lua VM itself is built separately as CryLua (see cmake/CryVendor.cmake):
# it is C rather than C++, and it includes Crytek's vectorlib.c, which lives in
# this directory rather than under LUA/.
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
        CryLua
)
