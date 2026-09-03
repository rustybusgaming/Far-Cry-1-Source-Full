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
    CPUDetect.cpp
    CompressedFile.cpp
    CryMemoryManager.cpp
    CryPak.cpp
    CrySizerImpl.cpp
    CrySizerStats.cpp
    DataProbe.cpp
    DebugCallStack.cpp
    DllMain.cpp
    DownloadManager.cpp
    FrameProfileRender.cpp
    FrameProfileSystem.cpp
    Log.cpp
    MTSafeAllocator.cpp
    Mailer.cpp
    RandGen.cpp
    ScriptBinding.cpp
    ScriptObjectAnimation.cpp
    ScriptObjectEntity.cpp
    ScriptObjectMovie.cpp
    ScriptObjectParticle.cpp
    ScriptObjectScript.cpp
    ScriptObjectSound.cpp
    ScriptObjectSystem.cpp
    ScriptSink.cpp
    SourceSafeHelper.cpp
    StdAfx.cpp
    StreamEngine.cpp
    System.cpp
    SystemCFG.cpp
    SystemInit.cpp
    SystemRender.cpp
    Timer.cpp
    XConsole.cpp
    XConsoleVariable.cpp
    XML/XMLDOMDocumentImpl.cpp
    XML/XMLDOMNodeImpl.cpp
    XML/XMLDOMNodeListImpl.cpp
    XML/_XMLDOMParserImpl.cpp
    XML/xml.cpp
    ZipDirCache.cpp
    ZipDirCacheFactory.cpp
    ZipDirCacheRW.cpp
    ZipDirFind.cpp
    ZipDirFindRW.cpp
    ZipDirList.cpp
    ZipDirStructures.cpp
    ZipDirTree.cpp
    getdxver.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/XML"
    DEPENDS
        CryCommon
)
