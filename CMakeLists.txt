################################################################################
# CryEngine 1.33 (Far Cry) -- web port
#
# Milestone 1: get the engine through a non-MSVC toolchain.
#
# The original tree is Visual Studio 2005 / Windows only. This build system is
# additive: it does not modify or replace the .vcproj/.sln files, it provides a
# parallel CMake path targeting clang (native, for fast triage) and Emscripten
# (wasm, the actual destination).
#
# Native triage build:
#   cmake -S . -B build -G Ninja
#   cmake --build build
#
# Emscripten build (needs the SDK, see cmake/toolchains/Emscripten.cmake):
#   emcmake cmake -S . -B build-wasm -G Ninja
#   cmake --build build-wasm
################################################################################
cmake_minimum_required(VERSION 3.20)

project(FarCryWebPort
    VERSION 1.33.0
    DESCRIPTION "CryEngine 1.33 web port"
    LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CryPlatform)
include(CryModule)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CRY_HEADER_PARSE_GATE
    "Compile each CryCommon header as its own TU to validate it" ON)

# ------------------------------------------------------------------------------
# Modules
#
# Ordered by dependency depth. Only the modules that have been brought up so far
# are listed; the rest of the engine is added as it is ported.
# ------------------------------------------------------------------------------
add_subdirectory(CryCommon)
add_subdirectory(CrySystem)
add_subdirectory(Cry3DEngine)
add_subdirectory(CryEntitySystem)
add_subdirectory(CryMovie)
add_subdirectory(CryScriptSystem)
add_subdirectory(CryAISystem)
add_subdirectory(CryInput)
add_subdirectory(CryNetwork)
add_subdirectory(CryPhysics)
add_subdirectory(CryAnimation)
add_subdirectory(CryFont)
add_subdirectory(CrySoundSystem)

# Tests for the port's own code (WinBase.h and friends).
enable_testing()
add_subdirectory(tests)

cry_print_summary()
