################################################################################
# Headless -- the port's first executable.
#
# Up to now every module has been a compile-only target: they built, but nothing
# proved they fit together. This target links them and runs CrySystem against
# Crytek's own null renderer.
#
# The modules reference each other in cycles, which was free when each was its
# own DLL and is not free as static archives. cry_link_module_group() handles
# that; see cmake/CryModule.cmake for why, and why wasm needs the opposite.
################################################################################

add_executable(Headless HeadlessMain.cpp)
cry_apply_common_settings(Headless)

target_include_directories(Headless PRIVATE
    ${CMAKE_SOURCE_DIR}/CryCommon
)

# Shared with tests/, which links the same set for test_static_modules.
set(CRY_HEADLESS_MODULES
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

cry_link_module_group(Headless ${CRY_HEADLESS_MODULES})

target_link_libraries(Headless PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)

message(STATUS "cry: executable Headless")

# tests/ links the same module set; see tests/CMakeLists.txt.
set(CRY_HEADLESS_MODULES "${CRY_HEADLESS_MODULES}" PARENT_SCOPE)
