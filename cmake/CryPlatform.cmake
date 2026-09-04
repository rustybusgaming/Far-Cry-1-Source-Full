################################################################################
# CryPlatform.cmake -- platform detection, defines and compiler flags.
#
# The engine's platform seam is CryCommon/platform.h, which branches on LINUX /
# LINUX32 / LINUX64. Crytek shipped a Linux dedicated-server build, so that
# branch already exists and disables the closed-source middleware
# (Bink, DivX, PunkBuster, UBI.com) via CryCommon/ProjectDefines.h.
#
# We reuse that seam rather than inventing a new one: the web port is, to a
# first approximation, "the Linux build, retargeted to wasm".
################################################################################

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Platform identification
# ------------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(CRY_PLATFORM "wasm32" CACHE INTERNAL "")
    # wasm32 is a 32-bit target, so it takes the LINUX32 model, not LINUX64.
    #
    # This matters beyond tidiness. Linux64Specific.h types DWORD_PTR as uint64
    # while LONG_PTR stays "long" -- fine on x86-64 where both are 8 bytes, but
    # on wasm32 pointers are 4 bytes and the two disagree with each other and
    # with the pointers they are supposed to hold. Linux32Specific.h is
    # internally consistent for a 32-bit target: intptr_t/uintptr_t as
    # int/unsigned int and DWORD_PTR as DWORD.
    #
    # It also fixes CHandle. LinuxSpecific.h adds a CHandle(typeof(__null))
    # constructor under LINUX64 so NULL can be passed as an invalid handle;
    # that only works where typeof(__null) differs from the handle type. On
    # x86-64 it is "long" and does; on wasm32 it is "int", which is exactly
    # HandleType for CHandle<int,-1>, so the overload collided with
    # CHandle(HandleType) and the header would not compile.
    set(CRY_PLATFORM_DEFINES LINUX LINUX32 _LINUX CRY_WASM)

    # The engine-specific wasm settings: exceptions, heap sizing, WebGL level.
    # Kept in its own file so an SDK upgrade cannot clobber them, and included
    # here rather than from the top level so it cannot be forgotten.
    include("${CMAKE_CURRENT_LIST_DIR}/toolchains/Emscripten.cmake")
else()
    set(CRY_PLATFORM "native-${CMAKE_SYSTEM_PROCESSOR}" CACHE INTERNAL "")
    set(CRY_PLATFORM_DEFINES LINUX LINUX64 _LINUX)
endif()

# ------------------------------------------------------------------------------
# Engine-wide defines
# ------------------------------------------------------------------------------
set(CRY_COMMON_DEFINES
    ${CRY_PLATFORM_DEFINES}

    # Closed-source middleware with no source in this tree. These are already
    # honoured by ProjectDefines.h on LINUX, but we state them explicitly so the
    # dependency is visible at the build level rather than buried in a header.
    NOT_USE_BINK_SDK            # BinkSDK/binkw32.dll -- binary only
    NOT_USE_DIVX_SDK
    NOT_USE_PUNKBUSTER_SDK      # PunkBuster/ -- binary only
    EXCLUDE_UBICOM_CLIENT_SDK   # Ubisoft.com/ -- needs curl + live services

    # The UBI.com matchmaking/CD-key SDK ships as lib_win32/*.lib binaries with
    # no source and no non-Windows build, and the online services it talks to
    # were shut down years ago. ProjectDefines.h documents NOT_USE_UBICOM_SDK
    # as the supported way to build without it; this is precisely that case.
    NOT_USE_UBICOM_SDK

    # The ASE (All-Seeing Eye) server-query SDK: ASEQuerySDK.lib, a Windows
    # binary with no source, used to advertise a dedicated server to the ASE
    # server browser. ProjectDefines.h documents NOT_USE_ASE_SDK as the
    # supported way to build without it.
    #
    # The native build never needed this stated, but only by accident: the
    # guards in CryNetwork/Server.cpp read
    #
    #     #if !defined(WIN64) && !defined(LINUX64) && !defined(NOT_USE_ASE_SDK)
    #
    # so defining LINUX64 happened to exclude it. wasm is LINUX32, which does
    # not, and the three ASEQuery_* symbols came up undefined at the link. The
    # exclusion is now stated for the reason it is actually true -- there is no
    # source for this library on any platform -- rather than as a side effect
    # of the word size.
    NOT_USE_ASE_SDK

    # CryCommon/crysound.h declares "#define CS_VERSION 3.61f" and types its
    # file callbacks as unsigned int. CrySoundSystem selects its callback
    # signatures with CS_VERSION_361 / CS_VERSION_372, and nothing defined
    # either -- so it fell into an #else branch declaring them with UINT_PTR,
    # which does not match the header on any 64-bit target. This states the
    # version the bundled header actually is.
    CS_VERSION_361

    _CRY_WEBPORT               # our own guard for web-port-specific divergence

    # Resolve the per-module factories from a compiled-in table instead of
    # dlopen()/dlsym(). Required for wasm, which has no synchronous dlopen,
    # and correct for the native headless build too -- the engine's dlopen
    # path reads a MODULE_PATH environment variable nothing ever sets.
    # See CryCommon/StaticModules.h.
    _CRY_STATIC_MODULES

    # CryPak.cpp's LINUX branch uses struct stat64 / _fstat64 explicitly.
    # glibc only declares the *64 forms under _LARGEFILE64_SOURCE, and
    # _FILE_OFFSET_BITS=64 makes the plain forms 64-bit as well so the two
    # agree. Both are no-ops on Emscripten, where off_t is already 64-bit.
    _LARGEFILE64_SOURCE
    _FILE_OFFSET_BITS=64
)

# ------------------------------------------------------------------------------
# Warning policy
#
# This is 2004 code written for MSVC 7.1. It trips an enormous number of modern
# diagnostics that are noise, not bugs. We silence the structural ones so that
# real errors stay visible, and keep everything else on.
# ------------------------------------------------------------------------------
set(CRY_DISABLED_WARNINGS
    -Wno-builtin-macro-redefined   # LinuxSpecific.h redefines __TIMESTAMP__
    -Wno-inline-new-delete         # CryMemoryManager.h inlines operator new
    -Wno-missing-exception-spec    # pre-C++11 throw() mismatches on new/delete
    -Wno-invalid-offsetof          # offsetof on non-POD, used pervasively
    -Wno-write-strings             # char* p = "literal"
    -Wno-deprecated-declarations
    -Wno-multichar                 # FOURCC literals
    -Wno-narrowing
    -Wno-unknown-pragmas           # #pragma warning(...), #pragma comment(...)
    -Wno-parentheses
    -Wno-dangling-else
    -Wno-logical-op-parentheses
    -Wno-unused-value
    -Wno-null-conversion

    # ProjectDefines.h already sets the middleware NOT_USE_* macros on LINUX.
    # We pass them on the command line as well so the dependency is visible at
    # build level; the identical redefinition is intentional.
    -Wno-macro-redefined

    # StdAfx.h pulls <ext/hash_map>, which libstdc++ warns is antiquated.
    # Replacing it with unordered_map is Milestone 2 work, not a Milestone 1
    # concern -- silence it until then rather than churn the headers now.
    -Wno-deprecated
)

# Diagnostics we deliberately keep as errors: these are the ones that indicate a
# genuine port problem rather than an idiom of the era.
set(CRY_PROMOTED_ERRORS
    -Werror=implicit-function-declaration
    -Werror=return-type
)

set(CRY_COMMON_FLAGS
    -fno-strict-aliasing    # the engine type-puns constantly
    -fno-delete-null-pointer-checks
    -ffp-contract=off
)

function(cry_apply_common_settings target)
    target_compile_definitions(${target} PUBLIC ${CRY_COMMON_DEFINES})
    target_compile_options(${target} PRIVATE
        ${CRY_COMMON_FLAGS}
        ${CRY_DISABLED_WARNINGS}
        ${CRY_PROMOTED_ERRORS})
    target_include_directories(${target} PUBLIC
        "${PROJECT_SOURCE_DIR}/CryCommon")
endfunction()

function(cry_print_summary)
    message(STATUS "")
    message(STATUS "-- CryEngine web port ------------------------------------")
    message(STATUS "  platform      : ${CRY_PLATFORM}")
    message(STATUS "  compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
    message(STATUS "  header gate   : ${CRY_HEADER_PARSE_GATE}")
    message(STATUS "----------------------------------------------------------")
    message(STATUS "")
endfunction()
