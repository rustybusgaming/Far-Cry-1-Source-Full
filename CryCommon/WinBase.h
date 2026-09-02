////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WinBase.h
//  Description: Win32 compatibility shim for the LINUX / wasm builds.
//
//  WHY THIS FILE EXISTS
//
//  platform.h ends with:
//
//      #if defined(LINUX)
//          #define RC_EXECUTABLE "rc"
//          #include <WinBase.h>
//      #endif
//
//  Crytek maintained a Linux dedicated-server build, and that build had a
//  hand-written <WinBase.h> providing the slice of the Win32 API the engine
//  actually touches. That file was never part of the released source drop, so
//  the LINUX branch of platform.h has been dangling ever since: every single
//  header that includes platform.h dies on this one missing include.
//
//  This is a reconstruction of it. It is intentionally NOT a general-purpose
//  Win32 emulation layer -- it covers exactly the surface CryEngine 1.33 uses,
//  and nothing else. Anything the engine does not call is deliberately absent
//  so that unported code fails loudly at compile time instead of silently
//  binding to a stub that does the wrong thing at runtime.
//
//  LAYERING
//
//    LinuxSpecific.h    MSVC keyword shims, base typedefs, socket errno map,
//                       CRT function renames (stricmp -> strcasecmp, ...)
//    Linux64Specific.h  fixed-width typedefs, BYTE/WORD/HWND/SIZE_T
//    WinBase.h  (here)  handles, kernel objects, threading, timing, file I/O
//
//  Everything below assumes the two headers above have already been included;
//  platform.h guarantees that ordering.
//
//  WASM NOTES
//
//  Under Emscripten the threading primitives map onto pthreads, which require
//  -pthread and a SharedArrayBuffer-capable page (COOP/COEP headers). Where a
//  primitive cannot be honoured on the web at all, it is marked CRY_WASM_TODO
//  rather than silently degraded -- see Milestone 2.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_COMMON_WINBASE_HDR_
#define _CRY_COMMON_WINBASE_HDR_

#if !defined(LINUX)
#	error "WinBase.h is the Linux/wasm shim; on Windows use the real <windows.h>"
#endif

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

//////////////////////////////////////////////////////////////////////////
// Scalar types not already covered by Linux{,64}Specific.h
//////////////////////////////////////////////////////////////////////////
typedef unsigned int        UINT;
typedef int                 INT;
typedef unsigned char*      LPBYTE;
typedef unsigned short*     LPWORD;
typedef void*               LPCVOID;
typedef char                CHAR;
typedef unsigned char       UCHAR;
typedef short               SHORT;
typedef unsigned short      USHORT;
typedef float               FLOAT;

//////////////////////////////////////////////////////////////////////////
// Handles
//
// NOTE: HANDLE, HMODULE and INVALID_HANDLE_VALUE are deliberately NOT defined
// here -- they already exist upstream and we must not shadow them:
//
//   HANDLE               LinuxSpecific.h -- CHandle<int,-1>, a typed wrapper
//                        around a POSIX file descriptor. Crytek chose an fd
//                        rather than a void* because that is what their Linux
//                        kernel-object calls actually returned.
//   INVALID_HANDLE_VALUE LinuxSpecific.h -- an enum constant, which is what
//                        lets CHandle construct from it implicitly.
//   HMODULE              CryMemoryManager.h -- #define HMODULE void*, next to
//                        its <dlfcn.h> include, since modules are dlopen()
//                        handles.
//
// The remaining handle types have no upstream definition and are supplied
// here. They stay pointer-sized so that structures embedding them keep their
// Win32 layout.
//////////////////////////////////////////////////////////////////////////
typedef void*               HINSTANCE;
typedef void*               HDC;
typedef void*               HGLRC;
typedef void*               HBITMAP;
typedef void*               HICON;
typedef void*               HCURSOR;
typedef void*               HMENU;
typedef void*               HKEY;

//////////////////////////////////////////////////////////////////////////
// Common Win32 POD structures
//
// Layout-compatible with the Win32 originals: several of these are memcpy'd
// or serialised by the engine, so field order and width matter.
//////////////////////////////////////////////////////////////////////////
typedef struct tagPOINT { LONG x, y; } POINT, *LPPOINT;
typedef struct tagRECT  { LONG left, top, right, bottom; } RECT, *LPRECT;

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
	WORD wYear, wMonth, wDayOfWeek, wDay;
	WORD wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
	unsigned int   Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char  Data4[8];
} GUID;
#endif

//////////////////////////////////////////////////////////////////////////
// HRESULT helpers
//////////////////////////////////////////////////////////////////////////
#ifndef S_OK
#	define S_OK           ((HRESULT)0x00000000L)
#	define S_FALSE        ((HRESULT)0x00000001L)
#	define E_FAIL         ((HRESULT)0x80004005L)
#	define E_OUTOFMEMORY  ((HRESULT)0x8007000EL)
#	define E_INVALIDARG   ((HRESULT)0x80070057L)
#	define E_NOINTERFACE  ((HRESULT)0x80004002L)
#endif
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) <  0)

//////////////////////////////////////////////////////////////////////////
// Critical sections -> pthread recursive mutexes
//
// Win32 CRITICAL_SECTION is recursive; a plain pthread mutex is not. Getting
// this wrong self-deadlocks the engine on the first re-entrant lock, so the
// recursive attribute is mandatory, not an optimisation.
//////////////////////////////////////////////////////////////////////////
typedef struct _CRITICAL_SECTION {
	pthread_mutex_t mutex;
	int             initialised;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

inline void InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&cs->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	cs->initialised = 1;
}

inline void DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
	if (cs->initialised) { pthread_mutex_destroy(&cs->mutex); cs->initialised = 0; }
}

inline void EnterCriticalSection(LPCRITICAL_SECTION cs)
{
	// The engine has a few sections that are entered before their owner's
	// constructor has run. On Win32 that happened to work; here we
	// initialise on first use rather than fault.
	if (!cs->initialised) InitializeCriticalSection(cs);
	pthread_mutex_lock(&cs->mutex);
}

inline void LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
	pthread_mutex_unlock(&cs->mutex);
}

inline BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
	if (!cs->initialised) InitializeCriticalSection(cs);
	return pthread_mutex_trylock(&cs->mutex) == 0 ? TRUE : FALSE;
}

//////////////////////////////////////////////////////////////////////////
// Interlocked operations
//
// Mapped onto the compiler's atomic builtins, which clang provides for both
// x86-64 and wasm. Note the Win32 return-value convention: Increment and
// Decrement return the NEW value, Exchange returns the OLD one.
//////////////////////////////////////////////////////////////////////////
inline LONG InterlockedIncrement(LONG volatile* p) { return __sync_add_and_fetch(p, 1); }
inline LONG InterlockedDecrement(LONG volatile* p) { return __sync_sub_and_fetch(p, 1); }
inline LONG InterlockedExchangeAdd(LONG volatile* p, LONG v) { return __sync_fetch_and_add(p, v); }
inline LONG InterlockedExchange(LONG volatile* p, LONG v) { return __sync_lock_test_and_set(p, v); }
inline LONG InterlockedCompareExchange(LONG volatile* p, LONG xchg, LONG cmp)
{ return __sync_val_compare_and_swap(p, cmp, xchg); }

//////////////////////////////////////////////////////////////////////////
// Timing
//////////////////////////////////////////////////////////////////////////
inline DWORD GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

inline void Sleep(DWORD ms)
{
	// usleep() is capped at one second per call on some libcs.
	if (ms >= 1000) { sleep(ms / 1000); ms %= 1000; }
	if (ms) usleep(ms * 1000);
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f)
{
	f->QuadPart = 1000000000LL;   // we report in nanoseconds
	return TRUE;
}

inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	c->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return TRUE;
}

inline void GetLocalTime(LPSYSTEMTIME st)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	struct tm t;
	localtime_r(&tv.tv_sec, &t);
	st->wYear   = (WORD)(t.tm_year + 1900);
	st->wMonth  = (WORD)(t.tm_mon + 1);
	st->wDayOfWeek = (WORD)t.tm_wday;
	st->wDay    = (WORD)t.tm_mday;
	st->wHour   = (WORD)t.tm_hour;
	st->wMinute = (WORD)t.tm_min;
	st->wSecond = (WORD)t.tm_sec;
	st->wMilliseconds = (WORD)(tv.tv_usec / 1000);
}

inline void GetSystemTime(LPSYSTEMTIME st) { GetLocalTime(st); }

//////////////////////////////////////////////////////////////////////////
// Diagnostics
//////////////////////////////////////////////////////////////////////////
inline void OutputDebugString(const char* s) { fputs(s, stderr); }
inline DWORD GetLastError() { return (DWORD)errno; }
inline void  SetLastError(DWORD e) { errno = (int)e; }

//////////////////////////////////////////////////////////////////////////
// Case-insensitive file open
//
// ILog.h calls fopen_nocase() on LINUX, but the function was never part of the
// released tree -- it lived in the same missing Linux support layer as this
// header.
//
// It is not a nicety. Far Cry's data is authored on Windows, where the
// filesystem is case-insensitive, and the asset references are correspondingly
// inconsistent: a .lua script asks for "Textures/Sky.dds", the pak entry says
// "textures/sky.dds", a level file says "TEXTURES/SKY.DDS". All three resolve
// to the same file on NTFS and to three different failures on ext4 -- and on
// Emscripten's MEMFS/IDBFS, which are case-sensitive too. Without this, the
// engine loads nothing.
//
// Strategy: try the path verbatim first (the overwhelmingly common case, and
// free), and only on failure walk the path one component at a time doing a
// case-insensitive directory scan. Write modes fall back to the literal path
// so that creating a new file still works.
//////////////////////////////////////////////////////////////////////////
#include <dirent.h>
#include <string>

inline bool cry_resolve_nocase(const char* path, std::string& out)
{
	std::string p(path ? path : "");
	if (p.empty()) return false;

	// Game data uses Win32 separators; normalise before walking.
	for (size_t i = 0; i < p.size(); ++i)
		if (p[i] == '\\') p[i] = '/';

	std::string resolved;
	size_t i = 0;
	if (p[0] == '/') { resolved = "/"; i = 1; }

	while (i < p.size())
	{
		size_t slash = p.find('/', i);
		if (slash == std::string::npos) slash = p.size();
		std::string comp = p.substr(i, slash - i);
		i = slash + 1;

		if (comp.empty() || comp == ".") continue;

		std::string candidate = resolved.empty() ? comp
		                      : (resolved == "/" ? "/" + comp : resolved + "/" + comp);

		struct stat st;
		if (comp == ".." || stat(candidate.c_str(), &st) == 0)
		{
			resolved = candidate;   // exact hit, keep going
			continue;
		}

		// Miss: scan the parent directory for a case-insensitive match.
		const char* dirname = resolved.empty() ? "." : resolved.c_str();
		DIR* d = opendir(dirname);
		if (!d) return false;

		bool found = false;
		struct dirent* e;
		while ((e = readdir(d)) != NULL)
		{
			if (strcasecmp(e->d_name, comp.c_str()) == 0)
			{
				resolved = resolved.empty() ? std::string(e->d_name)
				         : (resolved == "/" ? "/" + std::string(e->d_name)
				                            : resolved + "/" + e->d_name);
				found = true;
				break;
			}
		}
		closedir(d);
		if (!found) return false;
	}

	out = resolved;
	return true;
}

inline FILE* fopen_nocase(const char* file, const char* mode)
{
	if (FILE* f = fopen(file, mode))
		return f;

	std::string resolved;
	if (cry_resolve_nocase(file, resolved))
		if (FILE* f = fopen(resolved.c_str(), mode))
			return f;

	// Creating a file: the target legitimately does not exist yet, so retry
	// the literal path with separators normalised.
	if (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+')))
	{
		std::string p(file ? file : "");
		for (size_t i = 0; i < p.size(); ++i)
			if (p[i] == '\\') p[i] = '/';
		return fopen(p.c_str(), mode);
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
// Misc
//////////////////////////////////////////////////////////////////////////
inline DWORD GetCurrentThreadId() { return (DWORD)(uintptr_t)pthread_self(); }
inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }

#endif // _CRY_COMMON_WINBASE_HDR_
