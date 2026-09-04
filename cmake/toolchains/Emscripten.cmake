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

# ------------------------------------------------------------------------------
# C++ exceptions
#
# Emscripten disables exception CATCHING by default -- a throw compiles, but
# reaching one at runtime aborts the module with "Exception thrown, but
# exception catching is not enabled". The engine genuinely needs them: CryPak
# opens every .pak through ZipDir, which reports a missing or corrupt archive by
# throwing, and CSystem catches it and carries on with the file missing. With
# catching off, the first absent pak kills startup instead of logging.
#
# This is the "emscripten" exception mode (JS-based), which is slower than the
# native wasm EH proposal but works on every browser. Revisit -fwasm-exceptions
# once the baseline browser set supports it.
# ------------------------------------------------------------------------------
add_compile_options(-fexceptions)
add_link_options(-fexceptions)

# ------------------------------------------------------------------------------
# Memory
# ------------------------------------------------------------------------------
add_link_options(
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=${CRY_WASM_INITIAL_MEMORY}"
    "SHELL:-s MAXIMUM_MEMORY=${CRY_WASM_MAXIMUM_MEMORY}"

    # The engine's DLL-per-module layout collapses into a single link unit;
    # nothing is dlopen'd at runtime on the web. See CryCommon/StaticModules.h.
    "SHELL:-s MAIN_MODULE=0"

    # Renderer target. WebGL2 == GLES 3.0, which is the floor for the shader
    # rewrite -- the original GL path is GL 1.x with NV register combiners and
    # Cg, none of which survives.
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"

    # Game data is far too large to preload into MEMFS; assets get streamed.
    "SHELL:-s FORCE_FILESYSTEM=1"

    # Node needs this to exit(0) normally rather than trapping; harmless in a
    # browser, where nothing calls exit anyway.
    "SHELL:-s EXIT_RUNTIME=1"
)

# ------------------------------------------------------------------------------
# Deferred: threads and Asyncify
#
# Both were switched on speculatively when this file was written, before there
# was a build to test them against. They are off now because the headless target
# does not need either, and both are expensive:
#
#   pthreads   CrySystem's streaming engine is genuinely threaded, but the web
#              port does not use it -- CSyncStreamEngine reads synchronously
#              (see CrySystem/SyncStreamEngine.h). Turning -pthread on requires
#              SharedArrayBuffer, which requires COOP/COEP headers on the page,
#              and every allocation goes through the shared-memory path. Nothing
#              yet asks for it.
#
#   ASYNCIFY   Needed to keep the engine's blocking while(!quit) main loop on
#              the browser's single-threaded event loop. The headless target
#              initialises and releases without ever entering that loop, so it
#              costs a large size and speed penalty for nothing. It becomes
#              relevant when the render loop starts -- and the better answer
#              there may be restructuring around emscripten_set_main_loop
#              rather than Asyncify at all.
#
# Both are one line each when the time comes:
#   add_compile_options(-pthread); add_link_options(-pthread)
#   add_link_options("SHELL:-s ASYNCIFY=1")
# ------------------------------------------------------------------------------

# pthreads on the web need SharedArrayBuffer, which needs these response
# headers on the page serving the build:
#   Cross-Origin-Opener-Policy: same-origin
#   Cross-Origin-Embedder-Policy: require-corp
# Without them the module loads but every thread creation fails at runtime.
