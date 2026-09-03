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
    # wasm32 is a 32-bit target, but it is emphatically not x86: LINUX32 in
    # platform.h pulls in Linux32Specific.h and defines _CPU_X86, which gates
    # inline assembly. We take the 64-bit header (pointer-size agnostic, no
    # _CPU_X86) and correct the word size separately.
    set(CRY_PLATFORM_DEFINES LINUX LINUX64 _LINUX CRY_WASM)
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

    # CryCommon/crysound.h declares "#define CS_VERSION 3.61f" and types its
    # file callbacks as unsigned int. CrySoundSystem selects its callback
    # signatures with CS_VERSION_361 / CS_VERSION_372, and nothing defined
    # either -- so it fell into an #else branch declaring them with UINT_PTR,
    # which does not match the header on any 64-bit target. This states the
    # version the bundled header actually is.
    CS_VERSION_361

    _CRY_WEBPORT               # our own guard for web-port-specific divergence

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
