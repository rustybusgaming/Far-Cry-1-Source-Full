################################################################################
# Emscripten.cmake -- wasm target settings for the CryEngine web port.
#
# USAGE
#
#   Emscripten ships its own CMake toolchain file (Emscripten.cmake in the SDK)
#   and the supported way to use it is the emcmake wrapper:
#
#       source /path/to/emsdk/emsdk_env.sh
#       emcmake cmake -S . -B build-wasm -G Ninja
#       cmake --build build-wasm
#
#   This file is NOT a replacement for the SDK's toolchain file -- it holds the
#   engine-specific wasm settings and is included automatically from
#   CryPlatform.cmake once EMSCRIPTEN is set. Keeping the two separate means an
#   SDK upgrade does not clobber our settings.
#
# STATUS
#
#   Milestone 1 targets clang natively, not wasm. That is deliberate: the first
#   job is to get 800k lines of MSVC-era C++ through a conforming front end, and
#   native clang gives the same diagnostics as emcc for a fraction of the build
#   time and with a working debugger. Everything fixed for clang is fixed for
#   emcc. This file exists so the wasm target is ready when the code is.
#
################################################################################

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "Emscripten.cmake included without EMSCRIPTEN set. Configure with "
        "emcmake cmake ... so the SDK's own toolchain file runs first.")
endif()

# ------------------------------------------------------------------------------
# Memory
#
# Far Cry was built for a 256MB machine but streams large paks. wasm32 has a
# 4GB address-space ceiling and browsers get unhappy well before that, so the
# heap grows on demand rather than being reserved up front.
# ------------------------------------------------------------------------------
set(CRY_WASM_INITIAL_MEMORY 512MB CACHE STRING "Initial wasm heap")
set(CRY_WASM_MAXIMUM_MEMORY 2GB   CACHE STRING "Maximum wasm heap")

add_compile_options(
    -pthread                    # CrySystem's streaming engine is genuinely threaded
)

add_link_options(
    -pthread
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=${CRY_WASM_INITIAL_MEMORY}"
    "SHELL:-s MAXIMUM_MEMORY=${CRY_WASM_MAXIMUM_MEMORY}"

    # The engine's DLL-per-module layout collapses into a single link unit;
    # nothing is dlopen'd at runtime on the web.
    "SHELL:-s MAIN_MODULE=0"

    # Renderer target. WebGL2 == GLES 3.0, which is the floor for the shader
    # rewrite in Milestone 4 -- the original GL path is GL 1.x with NV register
    # combiners and Cg, none of which survives.
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"

    # Game data is far too large to preload into MEMFS; assets get streamed.
    "SHELL:-s FORCE_FILESYSTEM=1"

    # The engine's entry point blocks in a classic while(!quit) loop, which
    # cannot work on the browser's single-threaded event loop. Asyncify lets it
    # keep that structure while yielding; the alternative is restructuring the
    # main loop around emscripten_set_main_loop (Milestone 3).
    "SHELL:-s ASYNCIFY=1"
)

# pthreads on the web need SharedArrayBuffer, which needs these response
# headers on the page serving the build:
#   Cross-Origin-Opener-Policy: same-origin
#   Cross-Origin-Embedder-Policy: require-corp
# Without them the module loads but every thread creation fails at runtime.
