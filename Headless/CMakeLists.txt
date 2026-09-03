################################################################################
# Headless -- the port's first executable.
#
# Up to now every module has been a compile-only target: they built, but nothing
# proved they fit together. This target links them and runs CrySystem against
# Crytek's own null renderer.
#
# WHY THE LINK NEEDS HELP
#
# The engine was built as one DLL per module, so the modules were free to
# reference each other in a cycle -- CrySystem calls into Cry3DEngine, which
# calls back into CrySystem -- and the dynamic loader resolved it at runtime.
# Static archives have no such luxury: ld walks the link line once, left to
# right, and an archive it has already passed will not be revisited. The
# cycles are real and there is no ordering that satisfies them all, so the
# whole module set is wrapped in a LINK_GROUP (--start-group/--end-group),
# which tells the linker to keep re-scanning until nothing new resolves.
#
# This is not a workaround for a mistake in the port; it is the standard answer
# for mutually recursive static libraries, and it is what the single-link-unit
# wasm build needs anyway.
################################################################################

add_executable(Headless HeadlessMain.cpp)
cry_apply_common_settings(Headless)

target_include_directories(Headless PRIVATE
    ${CMAKE_SOURCE_DIR}/CryCommon
)

set(_cry_modules
    CrySystem
    CryStaticModules
    CryLua
    CryFreeType
    Cry3DEngine
    CryEntitySystem
    CryMovie
    CryScriptSystem
    CryAISystem
    CryInput
    CryNetwork
    CryPhysics
    CryAnimation
    CryFont
    # CrySoundSystem is deliberately NOT linked. Two independent reasons:
    #
    #   1. Crytek already excluded it here. CSystem::InitSound is wrapped
    #      entirely in "#if !defined(LINUX)", so on this platform the engine
    #      never asks for a sound system at all.
    #
    #   2. It could not be linked even if it did. CrySoundSystem is a wrapper
    #      over "crysound" -- a renamed FMOD -- which ships in this tree only
    #      as crysound.lib, a Windows binary, alongside ogg/vorbis .lib files
    #      in the same state. 68 undefined symbols, none of them with source.
    #
    # The web port needs a Web Audio implementation of ISoundSystem regardless,
    # in the same way CryInput needed WebInput instead of DirectInput. Leaving
    # the FMOD wrapper out now is the start of that, not a gap in it.
    CryRenderNULL
    CryVendor
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(Headless PRIVATE
        "$<LINK_GROUP:RESCAN,${_cry_modules}>")
else()
    # LINK_GROUP arrived in 3.24. Older CMake gets the flags spelled out.
    target_link_libraries(Headless PRIVATE
        -Wl,--start-group ${_cry_modules} -Wl,--end-group)
endif()

find_package(Threads REQUIRED)
target_link_libraries(Headless PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)

message(STATUS "cry: executable Headless")
