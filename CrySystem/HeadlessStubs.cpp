////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   HeadlessStubs.cpp
//  Description: The CSystem methods whose Win32 implementations are excluded.
//
//  WHY THIS FILE EXISTS
//
//  Several CrySystem translation units are Win32-by-design and are not built
//  here (tools/triage.py --excluded lists them with reasons): SystemWin32.cpp,
//  DebugCallStack.cpp, Mailer.cpp, SourceSafeHelper.cpp. Excluding them is the
//  right call -- each needs a genuine replacement, not a shim -- but it leaves
//  their symbols undefined at link, and CSystem's own header still declares
//  them.
//
//  So this supplies them for the headless build. Every one is written to be
//  HONEST about what it can and cannot do:
//
//    * Where there is a real POSIX equivalent, it is used. Error() logs, and
//      GetUserName() reads the environment. These are not stubs.
//
//    * Where the operation is meaningless off Windows, it says so and returns
//      a failure the caller can see. GetSSFileInfo queries Visual SourceSafe;
//      there is no SourceSafe, so it returns false rather than inventing data.
//
//    * Where it is a diagnostic the port has not implemented, it logs once and
//      returns empty. The memory dumps are in this category: they walk the
//      Win32 heap with HeapWalk, which has no counterpart, and returning
//      plausible-looking numbers would be worse than returning none.
//
//  Nothing here fabricates a value that a caller could mistake for real.
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "System.h"

#include <ILog.h>
#include <IConsole.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

//////////////////////////////////////////////////////////////////////////
// Error reporting
//
// The Win32 version put up a MessageBox and then tore the process down. There
// is no message box here -- and in a browser there is no modal dialog that can
// block the engine either -- so the message goes to the log and to stderr.
//
// stderr as well as the log on purpose: an error early in startup can happen
// before the log exists, and that is exactly when the message matters most.
//////////////////////////////////////////////////////////////////////////
void CSystem::Error(const char* format, ...)
{
	char szBuffer[4096];
	va_list args;
	va_start(args, format);
	vsnprintf(szBuffer, sizeof(szBuffer), format, args);
	szBuffer[sizeof(szBuffer) - 1] = 0;
	va_end(args);

	fprintf(stderr, "CrySystem ERROR: %s\n", szBuffer);
	fflush(stderr);

	if (m_pLog)
		m_pLog->LogError("%s", szBuffer);
}

//////////////////////////////////////////////////////////////////////////
// User identity
//
// Win32 called ::GetUserName(). The POSIX equivalent is the environment, then
// getlogin(). Both can legitimately be absent -- a container often has neither
// -- so "unknown" is returned rather than an empty string a caller might
// print as a blank name.
//////////////////////////////////////////////////////////////////////////
const char* CSystem::GetUserName()
{
	static char szUserName[256] = {0};

	if (!szUserName[0])
	{
		const char* pEnv = getenv("USER");
		if (!pEnv || !*pEnv) pEnv = getenv("LOGNAME");
		if (!pEnv || !*pEnv) pEnv = getlogin();
		if (!pEnv || !*pEnv) pEnv = "unknown";

		strncpy(szUserName, pEnv, sizeof(szUserName) - 1);
		szUserName[sizeof(szUserName) - 1] = 0;
	}
	return szUserName;
}

//////////////////////////////////////////////////////////////////////////
// Thread affinity
//
// SetThreadAffinityMask pinned the main thread to one core, working around a
// Win32-era bug where rdtsc could go backwards across cores. That reason is
// gone twice over here: the timing code now uses CLOCK_MONOTONIC (see
// RenderPCH.h), and wasm has no concept of a core to pin to.
//////////////////////////////////////////////////////////////////////////
void CSystem::SetAffinity()
{
}

//////////////////////////////////////////////////////////////////////////
// Memory diagnostics
//
// These walked the Win32 heap with HeapWalk/_CrtMemDumpStatistics. There is no
// portable equivalent -- glibc's mallinfo is neither portable nor meaningful
// under Emscripten's allocator -- so they report nothing rather than
// fabricating totals that would be quietly wrong.
//////////////////////////////////////////////////////////////////////////
void CSystem::DumpWinHeaps()
{
	if (m_pLog)
		m_pLog->Log("DumpWinHeaps: not available (no Win32 heap to walk)");
}

int CSystem::DumpMMStats(bool /*log*/)
{
	if (m_pLog)
		m_pLog->Log("DumpMMStats: not available on this platform");
	return 0;
}

void CSystem::DumpMemoryUsageStatistics()
{
	if (m_pLog)
		m_pLog->Log("DumpMemoryUsageStatistics: not available on this platform");
}

void CSystem::DebugStats(bool /*checkpoint*/, bool /*leaks*/)
{
	if (m_pLog)
		m_pLog->Log("DebugStats: not available on this platform");
}

void CSystem::TickMemStats(MemStatsPurposeEnum /*nPurpose*/)
{
	// Called every frame. Deliberately silent -- logging here would flood the
	// log at frame rate, which is how a harmless gap becomes an unusable one.
}

//////////////////////////////////////////////////////////////////////////
// Visual SourceSafe
//
// SourceSafeHelper.cpp drove VSS through COM automation to show who last
// checked a file out. There is no SourceSafe and no COM; false is the honest
// answer, and callers already handle it because the query could always fail.
//////////////////////////////////////////////////////////////////////////
bool CSystem::GetSSFileInfo(const char* /*inszFileName*/, char* outszInfo,
                            const DWORD indwBufferSize)
{
	if (outszInfo && indwBufferSize > 0)
		outszInfo[0] = 0;
	return false;
}
