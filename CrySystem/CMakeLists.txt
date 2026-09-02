################################################################################
# CrySystem -- core system services: pak/file I/O, console, logging, streaming,
# script bindings, profiling.
#
# 58 translation units exist here; the list below is the subset that currently
# compiles under clang with the LINUX seam. As with CryCommon this is a
# deliberate allowlist, so the build stays green and progress is measured by
# what moves into it.
#
# Deliberately NOT listed -- Milestone 2 work, each needing a real POSIX/web
# rewrite rather than a shim:
#   SystemWin32.cpp       raw Win32: registry, message pump, MessageBox
#   DebugCallStack.cpp    dbghelp.dll stack walking
#   Mailer.cpp            MAPI crash reporter
#   SourceSafeHelper.cpp  Visual SourceSafe COM automation
#   LuaDebugger/*         Win32 GUI debugger
#   HTTPDownloader.cpp    WinInet; on the web this becomes fetch()
################################################################################

cry_add_module(CrySystem
    SOURCES
    ApplicationHelper.cpp
    CrySizerImpl.cpp
    DebugCallStack.cpp
    MTSafeAllocator.cpp
    Mailer.cpp
    ScriptObjectScript.cpp
    SourceSafeHelper.cpp
    StdAfx.cpp
    ZipDirCache.cpp
    ZipDirFind.cpp
    ZipDirFindRW.cpp
    ZipDirList.cpp
    ZipDirTree.cpp
    getdxver.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/XML"
    DEPENDS
        CryCommon
)
