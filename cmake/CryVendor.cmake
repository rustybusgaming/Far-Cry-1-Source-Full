################################################################################
# CryVendor.cmake -- the third-party C libraries vendored into the tree.
#
# expat, zlib and an MD5 implementation ship as SOURCE inside CrySystem and were
# simply never added to the build: the MSVC projects compiled them, the CMake
# port did not, so their symbols came up undefined at the first link.
#
# They are built as one target rather than three because that is how they are
# vendored -- interleaved into CrySystem's own directories -- and because they
# are C, not C++. Compiling them as C matters: zlib and expat are C89 and both
# rely on implicit conversions and struct initialisation rules that a C++
# compiler rejects.
#
# The engine's warning policy does not apply here. This is unmodified upstream
# code; warnings in it are not ours to act on, and letting them through would
# bury the ones that are.
################################################################################

include_guard(GLOBAL)

function(cry_add_vendor_libs)
    set(_zlib_dir "${PROJECT_SOURCE_DIR}/CrySystem/zlib")
    set(_expat_dir "${PROJECT_SOURCE_DIR}/CrySystem/XML/Expat")

    set(_zlib_srcs
        adler32.c compress.c crc32.c deflate.c gzio.c infblock.c infcodes.c
        inffast.c inflate.c inftrees.c infutil.c trees.c uncompr.c zutil.c)
    list(TRANSFORM _zlib_srcs PREPEND "${_zlib_dir}/")

    # xmltok_impl.c and xmltok_ns.c are #included BY xmltok.c rather than
    # compiled separately -- expat's own build does the same. Compiling them
    # standalone produces duplicate symbols.
    set(_expat_srcs xmlparse.c xmlrole.c xmltok.c)
    list(TRANSFORM _expat_srcs PREPEND "${_expat_dir}/")

    add_library(CryVendor STATIC
        ${_zlib_srcs}
        ${_expat_srcs}
        "${PROJECT_SOURCE_DIR}/CrySystem/md5.c"
    )

    target_include_directories(CryVendor PUBLIC
        "${_zlib_dir}" "${_expat_dir}" "${PROJECT_SOURCE_DIR}/CrySystem")

    # expat needs to be told its byte order and that it is not building a DLL.
    target_compile_definitions(CryVendor PRIVATE
        XML_STATIC
        BYTEORDER=1234        # little-endian: x86-64 and wasm32 both
        HAVE_MEMMOVE=1
    )

    # Upstream C, compiled as C: -w because these warnings are not ours.
    set_target_properties(CryVendor PROPERTIES C_STANDARD 99 LINKER_LANGUAGE C)
    target_compile_options(CryVendor PRIVATE -w)

    message(STATUS "cry: module CryVendor (zlib + expat + md5)")
endfunction()

# ------------------------------------------------------------------------------
# cry_add_freetype()
#
# FreeType 2 ships as complete source under CryFont/FreeType2/ and, like zlib
# and expat, was simply never added to the CMake build -- so every FT_* symbol
# came up undefined the first time CryFont was linked rather than merely
# compiled.
#
# FreeType's own build convention is one translation unit per module: each
# module directory contains an amalgamation file (truetype.c, sfnt.c, ...) that
# #includes the rest of that directory. Only the amalgamations are listed here.
# Compiling the individual files as well would define every symbol twice.
#
# The module set is NOT a judgement call: FT_Init_FreeType registers exactly
# the drivers named in include/freetype/config/ftmodule.h, and Crytek already
# edited that file, commenting out CFF, CID, PCF, BDF, PFR, Type 1 and Type 42.
# The list below is the set that file still declares -- anything less leaves
# ftinit.c with undefined module classes, anything more is dead weight. If
# ftmodule.h is ever changed, this list changes with it.
# ------------------------------------------------------------------------------
function(cry_add_freetype)
    set(_ft_dir "${PROJECT_SOURCE_DIR}/CryFont/FreeType2")

    set(_ft_srcs
        # Core. ftbase.c is itself an amalgamation of the base services;
        # ftsystem/ftinit/ftdebug are separate because they are the pieces a
        # port is expected to be able to replace.
        src/base/ftbase.c
        src/base/ftinit.c
        src/base/ftsystem.c
        src/base/ftdebug.c
        # Used directly by CryFont's glyph handling.
        src/base/ftglyph.c
        src/base/ftbbox.c
        # Font drivers and the tables they need.
        src/truetype/truetype.c
        src/sfnt/sfnt.c
        src/psnames/psnames.c
        src/psaux/psaux.c
        src/pshinter/pshinter.c
        src/winfonts/winfnt.c
        # Rasterisers: smooth is the antialiased one, raster the monochrome
        # fallback FT_Render_Glyph falls back to.
        src/smooth/smooth.c
        src/raster/raster.c
        # Autohinter.
        src/autohint/autohint.c
    )
    list(TRANSFORM _ft_srcs PREPEND "${_ft_dir}/")

    add_library(CryFreeType STATIC ${_ft_srcs})

    # FT2_BUILD_LIBRARY switches the headers from "consumer" mode to "building
    # the library" mode; without it the internal headers refuse to be included.
    target_compile_definitions(CryFreeType PRIVATE FT2_BUILD_LIBRARY)

    # ft2build.h includes <ftheader.h> unqualified, and upstream expects the
    # config directory on the include path so a port can shadow ftoption.h /
    # ftconfig.h with its own. CryFont already relies on this same set.
    target_include_directories(CryFreeType PUBLIC
        "${_ft_dir}/include"
        "${_ft_dir}/include/freetype/config"
        "${_ft_dir}"
    )

    set_target_properties(CryFreeType PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED OFF)

    # Upstream code: its warnings are not ours to act on, and letting them
    # through would bury the ones that are.
    target_compile_options(CryFreeType PRIVATE -w)

    message(STATUS "cry: vendored FreeType2 (${_ft_dir})")
endfunction()

# ------------------------------------------------------------------------------
# cry_add_lua()
#
# Lua 4.1-alpha, vendored under CryScriptSystem/LUA/ and -- like zlib, expat and
# FreeType -- never added to the CMake build, so all 60 lua_* symbols came up
# undefined at the first link.
#
# The version matters. This is 4.1 *alpha*, a development snapshot that was
# never released as such: 4.1 became Lua 5.0. Nothing later is API-compatible
# with it, so the engine's script layer cannot be retargeted at a modern Lua
# without a rewrite -- lua_setnativedata, lua_getluafuncdata, lua_newuserdatabox,
# lua_xref/lua_xgetref/lua_xunref and lua_newtype have no Lua 5 equivalents.
# Building the bundled copy is not a shortcut; it is the only option that keeps
# the script semantics the game's .lua assets were written against.
#
# Built as C++, despite the .c extensions. That is not a workaround: lua.h in
# this tree contains
#
#     #define LUA_API    extern "C"
#
# unconditionally, with no __cplusplus guard, and every one of these files
# includes it. A C compiler rejects the header outright. Seven of them also
# include the engine's platform.h and CryMemoryManager.h, which are C++ as
# well. MSVC compiles by project setting rather than by extension, so this
# never showed up as a problem upstream.
#
# The extern "C" in lua.h is also what keeps the linkage right: the definitions
# here get C linkage from the header, so they still match the plain lua_*
# symbols CryScriptSystem's C++ code refers to.
# ------------------------------------------------------------------------------
function(cry_add_lua)
    set(_lua_dir "${PROJECT_SOURCE_DIR}/CryScriptSystem/LUA")

    # The core. ltests.c is deliberately absent: it is Lua's own test harness,
    # compiled only under LUA_DEBUG, and it defines a second main().
    set(_lua_core
        lapi.c lcode.c ldebug.c ldo.c lfunc.c lgc.c llex.c lmem.c lobject.c
        lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c
        lvm.c lzio.c)
    list(TRANSFORM _lua_core PREPEND "${_lua_dir}/")

    # The standard libraries CScriptSystem::Init opens: base, string, math, io,
    # bit and debug. lauxlib.c is the shared helper the rest of them use.
    set(_lua_libs
        lauxlib.c lbaselib.c lbitlib.c ldblib.c liolib.c lmathlib.c lstrlib.c)
    list(TRANSFORM _lua_libs PREPEND "${_lua_dir}/lib/")

    add_library(CryLua STATIC
        ${_lua_core}
        ${_lua_libs}
        # Crytek's own addition to their Lua fork: a "vector" userdata type with
        # arithmetic tag methods, so scripts can do vector maths on Vec3 without
        # a table per value. It lives beside CryScriptSystem rather than under
        # LUA/, which is why it is easy to miss -- but lua.h declares
        # vl_isvector() and CScriptSystem::Init calls vl_initvectorlib(), so the
        # script system does not start without it.
        "${PROJECT_SOURCE_DIR}/CryScriptSystem/vectorlib.c"
    )

    target_include_directories(CryLua PUBLIC
        "${_lua_dir}"
        "${_lua_dir}/lib"
    )

    # Unlike zlib and expat, this copy of Lua is NOT pristine upstream: Crytek
    # patched lmem.c to allocate through CryMemoryManager.h and several files to
    # include platform.h, so the VM shares the engine's allocator rather than
    # running its own. That is why it needs CryCommon on the include path and
    # the engine's platform defines -- it is engine code that happens to be Lua,
    # not a third-party library sitting beside the engine.
    target_include_directories(CryLua PRIVATE "${PROJECT_SOURCE_DIR}/CryCommon")
    target_compile_definitions(CryLua PRIVATE ${CRY_COMMON_DEFINES})

    # Give the Lua API C linkage. Its consumers already assume it -- every
    # include of lua.h in CryScriptSystem is wrapped in extern "C"{} -- but
    # lua.h only spells that out "#ifdef PS2"; everywhere else LUA_API is a
    # bare "extern", which under a C++ compiler means C++ linkage and mangled
    # names that no caller can find. On Windows this never mattered, because
    # MSVC compiled these files as C and C linkage came for free.
    #
    # Both headers guard their definition with #ifndef precisely so a port can
    # do this, so overriding here is the intended mechanism rather than a
    # workaround.
    target_compile_definitions(CryLua PRIVATE
        "LUA_API=extern \"C\""
        "LUALIB_API=extern \"C\""
    )

    # See the header comment: these are C++ translation units that happen to be
    # named .c.
    get_target_property(_lua_srcs CryLua SOURCES)
    set_source_files_properties(${_lua_srcs} PROPERTIES LANGUAGE CXX)

    # Upstream code: its warnings are not ours to act on.
    target_compile_options(CryLua PRIVATE -w -fpermissive)

    message(STATUS "cry: vendored Lua 4.1-alpha (${_lua_dir})")
endfunction()
