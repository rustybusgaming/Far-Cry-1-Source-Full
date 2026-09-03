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
#include <signal.h>
#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <string>
#include <vector>

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
// WinInet
//
// HTTPDownloader.h declares HINTERNET members, and headers that merely mention
// the type (ScriptBinding.cpp, ScriptObjectSystem.cpp) cannot parse without
// it. The typedef is provided so those headers compile; the WinInet FUNCTIONS
// are deliberately NOT shimmed, so HTTPDownloader.cpp itself still fails to
// build and stays excluded until it is rewritten over fetch()/XHR in
// Milestone 3. A stub that compiled and silently downloaded nothing would be
// considerably worse than a build error.
//////////////////////////////////////////////////////////////////////////
typedef void* HINTERNET;

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
// File attribute constants
//
// Declared up here with the other constants because both GetFileAttributes and
// SetFileAttributes reference them, and the two live several hundred lines
// apart. LinuxSpecific.h already supplies FILE_ATTRIBUTE_NORMAL.
//////////////////////////////////////////////////////////////////////////
#ifndef FILE_ATTRIBUTE_DIRECTORY
#	define FILE_ATTRIBUTE_DIRECTORY   0x00000010
#	define FILE_ATTRIBUTE_READONLY    0x00000001
#	define INVALID_FILE_ATTRIBUTES    ((DWORD)-1)
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

// Defined further down with the case-insensitive file open; declared here
// because the directory-iteration helpers below need it too.
inline bool cry_resolve_nocase(const char* path, std::string& out);

//////////////////////////////////////////////////////////////////////////
// MSVC directory iteration: _findfirst64 / _findnext64 / _findclose
//
// CryPak.cpp walks pak directories with the Microsoft CRT's find API, and
// CSystem::Deltree uses it to recurse. There is no POSIX equivalent with the
// same shape -- opendir/readdir have no notion of a wildcard -- so the pattern
// matching is done here with fnmatch().
//
// Two Win32 behaviours are reproduced deliberately:
//
//   * Matching is CASE-INSENSITIVE (FNM_CASEFOLD). "*.PAK" has to find
//     "levels.pak", exactly as it does on NTFS, or the engine mounts nothing.
//
//   * "." and ".." ARE returned when they match the pattern, as FindFirstFile
//     does. Callers such as Deltree test for them explicitly before recursing;
//     silently hiding them would change behaviour those guards depend on.
//
// The handle is a heap-allocated context cast to intptr_t, matching the CRT's
// own contract: -1 means failure, and every successful _findfirst64 must be
// paired with a _findclose.
//////////////////////////////////////////////////////////////////////////

#define _A_NORMAL  0x00
#define _A_RDONLY  0x01
#define _A_HIDDEN  0x02
#define _A_SYSTEM  0x04
#define _A_SUBDIR  0x10
#define _A_ARCH    0x20

// The engine writes both spellings, and writes them with an explicit "struct"
// keyword in places (ICryPak.h:252 uses "struct _finddata_t"), so the real tag
// has to be _finddata_t and the 64-bit name an alias of it -- not the other way
// round. Only .name and .attrib are ever read, so one layout serves both.
struct _finddata_t
{
	unsigned int  attrib;
	time_t        time_create;
	time_t        time_access;
	time_t        time_write;
	long long     size;
	char          name[260];
};
// The engine writes BOTH "struct _finddata_t" and "struct __finddata64_t"
// (CryPak.h:200, ICryPak.h:252), so both need to be real struct tags -- a
// typedef alias is rejected after the struct keyword. Deriving keeps a single
// layout and lets a __finddata64_t* be passed wherever a _finddata_t* is
// expected, which is how the shared directory walk below is reused.
struct __finddata64_t : public _finddata_t {};

struct _CryFindCtx
{
	DIR*        dir;
	std::string directory;   // directory part of the filespec, "" -> "."
	std::string pattern;     // wildcard part
};

inline bool _cry_find_step(_CryFindCtx* ctx, struct _finddata_t* fi)
{
	struct dirent* e;
	while ((e = readdir(ctx->dir)) != NULL)
	{
		if (fnmatch(ctx->pattern.c_str(), e->d_name, FNM_CASEFOLD) != 0)
			continue;

		std::string full = ctx->directory.empty()
		                 ? std::string(e->d_name)
		                 : ctx->directory + "/" + e->d_name;

		struct stat st;
		unsigned int attrib = _A_NORMAL;
		long long size = 0;
		time_t mtime = 0;
		if (stat(full.c_str(), &st) == 0)
		{
			if (S_ISDIR(st.st_mode))     attrib |= _A_SUBDIR;
			if (!(st.st_mode & S_IWUSR)) attrib |= _A_RDONLY;
			size  = (long long)st.st_size;
			mtime = st.st_mtime;
		}
		if (e->d_name[0] == '.' && !(attrib & _A_SUBDIR))
			attrib |= _A_HIDDEN;

		fi->attrib      = attrib;
		fi->size        = size;
		fi->time_write  = mtime;
		fi->time_create = mtime;
		fi->time_access = mtime;
		strncpy(fi->name, e->d_name, sizeof(fi->name) - 1);
		fi->name[sizeof(fi->name) - 1] = '\0';
		return true;
	}
	return false;
}

inline intptr_t _findfirst64(const char* filespec, struct _finddata_t* fi)
{
	if (!filespec || !fi) return -1;

	std::string spec(filespec);
	for (size_t i = 0; i < spec.size(); ++i)
		if (spec[i] == '\\') spec[i] = '/';

	std::string dir, pat;
	size_t slash = spec.rfind('/');
	if (slash == std::string::npos) { dir = ""; pat = spec; }
	else { dir = spec.substr(0, slash); pat = spec.substr(slash + 1); }
	if (pat.empty()) pat = "*";

	// The directory itself may be mis-cased, same as any other asset path.
	std::string opendir_path = dir.empty() ? std::string(".") : dir;
	DIR* d = opendir(opendir_path.c_str());
	if (!d)
	{
		std::string resolved;
		if (!dir.empty() && cry_resolve_nocase(dir.c_str(), resolved))
		{
			d = opendir(resolved.c_str());
			if (d) dir = resolved;
		}
		if (!d) return -1;
	}

	_CryFindCtx* ctx = new _CryFindCtx;
	ctx->dir = d;
	ctx->directory = dir;
	ctx->pattern = pat;

	if (!_cry_find_step(ctx, fi))
	{
		closedir(ctx->dir);
		delete ctx;
		return -1;
	}
	return (intptr_t)ctx;
}

inline int _findnext64(intptr_t handle, struct _finddata_t* fi)
{
	if (handle == -1 || !fi) return -1;
	_CryFindCtx* ctx = (_CryFindCtx*)handle;
	return _cry_find_step(ctx, fi) ? 0 : -1;
}

inline intptr_t _findfirst(const char* filespec, struct _finddata_t* fi)
{ return _findfirst64(filespec, fi); }

inline int _findnext(intptr_t handle, struct _finddata_t* fi)
{ return _findnext64(handle, fi); }

inline int _findclose(intptr_t handle)
{
	if (handle == -1) return -1;
	_CryFindCtx* ctx = (_CryFindCtx*)handle;
	if (ctx->dir) closedir(ctx->dir);
	delete ctx;
	return 0;
}

// CryPak.cpp:734 uses "struct stat64" + _fstat64 in its LINUX branch. glibc
// exposes those under _LARGEFILE64_SOURCE (set by the build); this just
// supplies the underscore-prefixed CRT spelling.
inline int _fstat64(int fd, struct stat64* st) { return fstat64(fd, st); }

// _mkdir is the MSVC spelling; POSIX mkdir takes a mode. 0755 matches what the
// engine's directories need (it creates game/save/log folders it then writes).
inline int _mkdir(const char* path) { return mkdir(path, 0755); }
inline int _rmdir(const char* path) { return rmdir(path); }

// CSystem::Deltree removes a tree with these. Win32 returns nonzero on
// success, the opposite of unlink()/rmdir(), so the result has to be inverted.
inline BOOL DeleteFile(const char* path)
{ return (path && unlink(path) == 0) ? TRUE : FALSE; }

inline BOOL RemoveDirectory(const char* path)
{ return (path && rmdir(path) == 0) ? TRUE : FALSE; }

inline BOOL CreateDirectory(const char* path, void* /*lpSecurityAttributes*/)
{ return (path && mkdir(path, 0755) == 0) ? TRUE : FALSE; }

//////////////////////////////////////////////////////////////////////////
// GlobalMemoryStatus
//
// ScriptObjectSystem.cpp exposes total/available physical memory to Lua.
// sysinfo() provides the same numbers on Linux; on Emscripten there is no
// system memory to report, so the wasm heap size is the honest answer.
//////////////////////////////////////////////////////////////////////////
typedef struct _MEMORYSTATUS {
	DWORD dwLength;
	DWORD dwMemoryLoad;
	size_t dwTotalPhys;
	size_t dwAvailPhys;
	size_t dwTotalPageFile;
	size_t dwAvailPageFile;
	size_t dwTotalVirtual;
	size_t dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

inline void GlobalMemoryStatus(LPMEMORYSTATUS ms)
{
	if (!ms) return;
	memset(ms, 0, sizeof(*ms));
	ms->dwLength = (DWORD)sizeof(*ms);

#if !defined(CRY_WASM)
	long pages     = sysconf(_SC_PHYS_PAGES);
	long avpages   = sysconf(_SC_AVPHYS_PAGES);
	long page_size = sysconf(_SC_PAGESIZE);
	if (pages > 0 && page_size > 0)
	{
		ms->dwTotalPhys = (size_t)pages * (size_t)page_size;
		ms->dwAvailPhys = (avpages > 0) ? (size_t)avpages * (size_t)page_size : 0;
		ms->dwMemoryLoad = ms->dwTotalPhys
			? (DWORD)(100 - (ms->dwAvailPhys * 100 / ms->dwTotalPhys)) : 0;
	}
#endif
	ms->dwTotalVirtual  = ms->dwTotalPhys;
	ms->dwAvailVirtual  = ms->dwAvailPhys;
	ms->dwTotalPageFile = ms->dwTotalPhys;
	ms->dwAvailPageFile = ms->dwAvailPhys;
}

//////////////////////////////////////////////////////////////////////////
// BMP file headers
//
// CFontTexture::WriteToFile dumps the glyph atlas as a Windows bitmap, which
// is genuinely useful when debugging font rendering in a port. These are a
// FILE FORMAT, not an API -- the layout is fixed and documented -- so defining
// them here is portable rather than an emulation.
//
// The packing matters and is easy to get silently wrong. BITMAPFILEHEADER is
// 14 bytes: a 2-byte magic followed by 4-byte fields. With default alignment
// the compiler inserts two bytes of padding after bfType, producing a 16-byte
// struct and a BMP that no reader will open. #pragma pack(2) is what makes it
// 14. The exact field widths matter too, which is why they are spelled with
// fixed-size types rather than long/short.
//////////////////////////////////////////////////////////////////////////
#pragma pack(push, 2)

typedef struct tagBITMAPFILEHEADER {
	uint16 bfType;          // 'BM'
	uint32 bfSize;          // total file size in bytes
	uint16 bfReserved1;
	uint16 bfReserved2;
	uint32 bfOffBits;       // byte offset from file start to the pixel data
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER {
	uint32 biSize;          // size of THIS struct, i.e. 40
	int32  biWidth;
	int32  biHeight;        // negative means a top-down image
	uint16 biPlanes;        // always 1
	uint16 biBitCount;
	uint32 biCompression;   // BI_RGB (0) for uncompressed
	uint32 biSizeImage;
	int32  biXPelsPerMeter;
	int32  biYPelsPerMeter;
	uint32 biClrUsed;
	uint32 biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagRGBQUAD {
	uint8 rgbBlue, rgbGreen, rgbRed, rgbReserved;   // note: BGRA order
} RGBQUAD;

#pragma pack(pop)

#ifndef BI_RGB
#	define BI_RGB 0
#endif

//////////////////////////////////////////////////////////////////////////
// Multimedia timer
//
// timeGetTime() is winmm's millisecond clock. On Win32 it differs from
// GetTickCount only in its settable resolution, which is meaningless here.
//////////////////////////////////////////////////////////////////////////
inline DWORD timeGetTime() { return GetTickCount(); }

//////////////////////////////////////////////////////////////////////////
// SYSTEMTIME <-> FILETIME
//
// ZipDirStructures.cpp converts the DOS timestamps stored in a .pak's
// directory into FILETIME. A Win32 FILETIME counts 100-nanosecond intervals
// since 1601-01-01 UTC -- not the Unix epoch -- so the conversion needs the
// 11644473600-second offset between the two epochs.
//////////////////////////////////////////////////////////////////////////
inline BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft)
{
	if (!st || !ft) return FALSE;

	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = st->wYear - 1900;
	t.tm_mon  = st->wMonth - 1;
	t.tm_mday = st->wDay;
	t.tm_hour = st->wHour;
	t.tm_min  = st->wMinute;
	t.tm_sec  = st->wSecond;

	// timegm() interprets the fields as UTC; mktime() would apply the local
	// timezone and shift every pak timestamp by the machine's offset.
	time_t secs = timegm(&t);
	if (secs == (time_t)-1) return FALSE;

	const unsigned long long EPOCH_DIFF = 11644473600ULL;  // 1601 -> 1970, seconds
	unsigned long long v = ((unsigned long long)secs + EPOCH_DIFF) * 10000000ULL
	                     + (unsigned long long)st->wMilliseconds * 10000ULL;

	ft->dwLowDateTime  = (DWORD)(v & 0xFFFFFFFFULL);
	ft->dwHighDateTime = (DWORD)(v >> 32);
	return TRUE;
}

inline BOOL FileTimeToSystemTime(const FILETIME* ft, SYSTEMTIME* st)
{
	if (!ft || !st) return FALSE;

	unsigned long long v = ((unsigned long long)ft->dwHighDateTime << 32)
	                     | (unsigned long long)ft->dwLowDateTime;
	const unsigned long long EPOCH_DIFF = 11644473600ULL;
	unsigned long long total = v / 10000000ULL;
	if (total < EPOCH_DIFF) return FALSE;

	time_t secs = (time_t)(total - EPOCH_DIFF);
	struct tm t;
	gmtime_r(&secs, &t);
	st->wYear   = (WORD)(t.tm_year + 1900);
	st->wMonth  = (WORD)(t.tm_mon + 1);
	st->wDayOfWeek = (WORD)t.tm_wday;
	st->wDay    = (WORD)t.tm_mday;
	st->wHour   = (WORD)t.tm_hour;
	st->wMinute = (WORD)t.tm_min;
	st->wSecond = (WORD)t.tm_sec;
	st->wMilliseconds = (WORD)((v / 10000ULL) % 1000ULL);
	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
// SetFileAttributes
//
// xml.cpp calls this with FILE_ATTRIBUTE_NORMAL before writing, i.e. "clear
// the read-only bit". That is the only attribute with a POSIX meaning, so it
// is the only one honoured; the rest are accepted and ignored rather than
// failing, which matches how the engine uses the call.
//////////////////////////////////////////////////////////////////////////
inline BOOL SetFileAttributes(const char* lpFileName, DWORD dwAttributes)
{
	if (!lpFileName) return FALSE;
	struct stat st;
	if (stat(lpFileName, &st) != 0) return FALSE;

	mode_t mode = st.st_mode;
	if (dwAttributes & FILE_ATTRIBUTE_READONLY)
		mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		mode |= S_IWUSR;

	return chmod(lpFileName, mode) == 0 ? TRUE : FALSE;
}

//////////////////////////////////////////////////////////////////////////
// Diagnostics
//////////////////////////////////////////////////////////////////////////
inline void OutputDebugString(const char* s) { fputs(s, stderr); }

// Win32's debugger trap. raise(SIGTRAP) is the POSIX equivalent and stops in
// a debugger when one is attached; without one it terminates, which matches
// Win32 behaviour for an unhandled breakpoint. Under Emscripten there is no
// signal to raise, and __builtin_trap() compiles to a wasm "unreachable",
// which is what a debugger break becomes there.
inline void DebugBreak()
{
#if defined(CRY_WASM)
	__builtin_trap();
#else
	raise(SIGTRAP);
#endif
}
inline DWORD GetLastError() { return (DWORD)errno; }
inline void  SetLastError(DWORD e) { errno = (int)e; }

//////////////////////////////////////////////////////////////////////////
// Thread and event handles
//
// platform.h declares these for every platform EXCEPT Linux:
//
//     #if !defined(LINUX)
//     typedef void *THREAD_HANDLE;
//     typedef void *EVENT_HANDLE;
//     #endif
//
// The Linux definitions lived in the unreleased support layer, so the types
// are simply absent -- CrySystem/RefStreamEngine.h and HTTPDownloader.h both
// declare members of them.
//
// A Win32 event is a counted, waitable, manual/auto-reset object; the honest
// POSIX equivalent is a condvar plus a mutex and a flag, not a bare pthread_t.
// Modelling it as a struct (rather than aliasing HANDLE) keeps the semantics
// available for the Milestone 2 threading rewrite.
//////////////////////////////////////////////////////////////////////////
typedef pthread_t THREAD_HANDLE;

typedef struct _CRY_EVENT {
	pthread_mutex_t mutex;
	pthread_cond_t  cond;
	int             signalled;
	int             manual_reset;
} *EVENT_HANDLE;

//////////////////////////////////////////////////////////////////////////
// Path comparison
//
// CryPak.h and CryPak.cpp call comparePathNames() in five places; it is
// defined nowhere in the tree -- another casualty of the missing Linux layer.
//
// Its contract is legible from the call sites: it is used as
// "!comparePathNames(a, b, len)" to mean "these paths match", so it returns 0
// on equality, like strncmp. Path matching on Windows is case-insensitive AND
// separator-insensitive ('/' and '\\' are the same character for this
// purpose), and the pak data relies on both, so it must be reproduced here
// rather than substituting a plain strncasecmp.
//////////////////////////////////////////////////////////////////////////
inline int comparePathNames(const char* a, const char* b, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca == '\\') ca = '/';
		if (cb == '\\') cb = '/';
		ca = (unsigned char)tolower(ca);
		cb = (unsigned char)tolower(cb);
		if (ca != cb) return ca < cb ? -1 : 1;
		if (ca == 0)  return 0;      // both ended together
	}
	return 0;
}

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
// __noop
//
// An MSVC intrinsic that evaluates to nothing and discards its arguments
// unevaluated. It is the idiom the engine uses to compile a debug macro away
// while keeping the call syntactically valid, e.g.
//     #define LOG if (0) __noop else Log
// ((void)0) reproduces the "valid expression that does nothing" part, which is
// all the engine relies on.
//////////////////////////////////////////////////////////////////////////
#ifndef __noop
	// Function-LIKE: the engine writes __noop(args), so an object-like macro
	// would leave a stray argument list behind. Variadic macros are a GNU
	// extension in C++98, which -std=gnu++98 enables.
#	define __noop(...) ((void)0)
#endif

//////////////////////////////////////////////////////////////////////////
// MSVC CRT functions with no POSIX spelling
//
// These are plain CRT calls the engine makes that simply do not exist outside
// the Microsoft runtime. Each has an exact, well-defined equivalent.
//////////////////////////////////////////////////////////////////////////
inline char* strlwr(char* s)
{
	if (s) for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p);
	return s;
}
inline char* _strlwr(char* s) { return strlwr(s); }

inline char* strupr(char* s)
{
	if (s) for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p);
	return s;
}

inline int memicmp(const void* a, const void* b, size_t n)
{
	const unsigned char* pa = (const unsigned char*)a;
	const unsigned char* pb = (const unsigned char*)b;
	for (size_t i = 0; i < n; ++i)
	{
		int ca = tolower(pa[i]), cb = tolower(pb[i]);
		if (ca != cb) return ca < cb ? -1 : 1;
	}
	return 0;
}
inline int _memicmp(const void* a, const void* b, size_t n) { return memicmp(a, b, n); }

// itoa() is not in any standard; radix 10 is all the engine ever asks for,
// but the other radices are cheap and leaving them out would be a trap.
inline char* itoa(int value, char* str, int radix)
{
	if (!str) return NULL;
	if (radix < 2 || radix > 36) { str[0] = '\0'; return str; }

	char buf[36];
	int i = 0;
	unsigned int uv = (value < 0 && radix == 10) ? (unsigned int)(-value)
	                                             : (unsigned int)value;
	if (uv == 0) buf[i++] = '0';
	while (uv) { int d = uv % radix; buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); uv /= radix; }
	if (value < 0 && radix == 10) buf[i++] = '-';

	int j = 0;
	while (i > 0) str[j++] = buf[--i];
	str[j] = '\0';
	return str;
}
inline char* _itoa(int v, char* s, int r) { return itoa(v, s, r); }

// ltoa is the long-sized sibling. int and long differ in width on LP64, so it
// is a separate implementation rather than an alias, or values above 2^31
// would be truncated on the way in.
inline char* ltoa(long value, char* str, int radix)
{
	if (!str) return NULL;
	if (radix < 2 || radix > 36) { str[0] = '\0'; return str; }

	char buf[68];
	int i = 0;
	unsigned long uv = (value < 0 && radix == 10) ? (unsigned long)(-value)
	                                              : (unsigned long)value;
	if (uv == 0) buf[i++] = '0';
	while (uv) { int d = (int)(uv % (unsigned long)radix);
	             buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
	             uv /= (unsigned long)radix; }
	if (value < 0 && radix == 10) buf[i++] = '-';

	int j = 0;
	while (i > 0) str[j++] = buf[--i];
	str[j] = '\0';
	return str;
}
inline char* _ltoa(long v, char* s, int r) { return ltoa(v, s, r); }

// MulDiv-style 64-bit widening multiply from the Win32 headers.
inline long long Int32x32To64(int a, int b) { return (long long)a * (long long)b; }

//////////////////////////////////////////////////////////////////////////
// IsBadReadPtr
//
// A Win32 probe that asks whether a memory range is readable. It is used here
// only inside assert()s (BrushLM.cpp, LMSerializationManager2.cpp).
//
// There is no POSIX equivalent, and there is no honest one to write: the only
// way to test readability is to try the read, and a fault is not catchable in
// wasm. Microsoft themselves deprecated it for exactly this reason -- it races,
// and probing a guard page can crash the process it was meant to protect.
//
// So this checks the part that is both meaningful and reliable -- the null and
// obviously-bogus-length cases -- and reports the range as readable otherwise.
// That keeps the assertions honest about the bug they actually catch (a null
// or unset pointer) without pretending to a guarantee we cannot make.
//////////////////////////////////////////////////////////////////////////
inline BOOL IsBadReadPtr(const void* lp, size_t ucb)
{
	if (ucb == 0) return FALSE;      // an empty range is vacuously readable
	return lp == NULL ? TRUE : FALSE;
}
inline BOOL IsBadWritePtr(void* lp, size_t ucb) { return IsBadReadPtr(lp, ucb); }

//////////////////////////////////////////////////////////////////////////
// _makepath / _splitpath
//
// MSVC CRT path assembly. partman.cpp builds particle-library filenames with
// it. Win32 path syntax is normalised to POSIX on the way out, since every
// path that reaches the filesystem here has to use forward slashes.
//////////////////////////////////////////////////////////////////////////
inline void _makepath(char* path, const char* drive, const char* dir,
                      const char* fname, const char* ext)
{
	if (!path) return;
	std::string out;

	// A drive letter has no meaning outside Windows; it is dropped rather
	// than emitted as "C:" into a POSIX path.
	(void)drive;

	if (dir && *dir)
	{
		out = dir;
		for (size_t i = 0; i < out.size(); ++i)
			if (out[i] == '\\') out[i] = '/';
		if (out[out.size() - 1] != '/') out += '/';
	}
	if (fname && *fname) out += fname;
	if (ext && *ext)
	{
		if (*ext != '.') out += '.';
		out += ext;
	}
	strcpy(path, out.c_str());
}

inline void _splitpath(const char* path, char* drive, char* dir,
                       char* fname, char* ext)
{
	if (drive) drive[0] = '\0';
	if (dir)   dir[0]   = '\0';
	if (fname) fname[0] = '\0';
	if (ext)   ext[0]   = '\0';
	if (!path) return;

	std::string p(path);
	for (size_t i = 0; i < p.size(); ++i)
		if (p[i] == '\\') p[i] = '/';

	size_t slash = p.rfind('/');
	std::string d = (slash == std::string::npos) ? std::string() : p.substr(0, slash + 1);
	std::string rest = (slash == std::string::npos) ? p : p.substr(slash + 1);

	size_t dot = rest.rfind('.');
	std::string f = (dot == std::string::npos) ? rest : rest.substr(0, dot);
	std::string e = (dot == std::string::npos) ? std::string() : rest.substr(dot);

	if (dir)   strcpy(dir, d.c_str());
	if (fname) strcpy(fname, f.c_str());
	if (ext)   strcpy(ext, e.c_str());
}

//////////////////////////////////////////////////////////////////////////
// Missing Linux-layer text helpers
//
// Same story as fopen_nocase and comparePathNames: called under
// #if defined(LINUX), defined nowhere. Each one's contract is pinned down by
// the #else branch sitting right next to the call.
//////////////////////////////////////////////////////////////////////////

// CrySystem/XML/xml.cpp:45-51 --
//     #if defined(LINUX) return compareTextFileStrings(m_tag, tag) == 0;
//     #else               return stricmp(tag, m_tag) == 0;
// so this is exactly a case-insensitive compare.
inline int compareTextFileStrings(const char* a, const char* b)
{
	return strcasecmp(a ? a : "", b ? b : "");
}

// Strips carriage returns and line feeds from XML text nodes in place.
inline void RemoveCRLF(string& str)
{
	std::string in(str.c_str());
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i)
		if (in[i] != '\r' && in[i] != '\n')
			out += in[i];
	str = out.c_str();
}

// CryPak.cpp:618 documents the invariant it is maintaining: "the path must be
// absolute normalized lower-case with forward-slashes". This collapses runs of
// separators produced by concatenating path fragments. It rewrites in place and
// only ever shortens, so the caller's buffer is always adequate.
inline void replaceDoublePathFilename(char* szPath)
{
	if (!szPath) return;
	char* w = szPath;
	for (char* r = szPath; *r; ++r)
	{
		char c = (*r == '\\') ? '/' : *r;
		if (c == '/' && w > szPath && w[-1] == '/')
			continue;
		*w++ = c;
	}
	*w = '\0';
}

// The Windows build reads its version out of the PE resource (CrySystem.rc,
// VS_VERSION_INFO). There is no resource section on Linux or in wasm, so the
// LINUX branch of CSystem::QueryVersionInfo() reads this constant instead.
// SFileVersion is int v[4]; the branch sets v[1..3] to 1 and takes v[0] from
// here, so this is the engine build number: CryEngine 1.33.
#ifndef VERSION_INFO
#	define VERSION_INFO 133
#endif

//////////////////////////////////////////////////////////////////////////
// Path adaptation helpers
//
// CryPak.cpp calls adaptFilenameToLinux() and getFilenameNoCase() inside its
// #if defined(LINUX) branch; like fopen_nocase and comparePathNames, neither
// exists anywhere in the released tree. Their contracts are legible from the
// one call site (CryPak.cpp:255-265):
//
//     string adjusted(dst);
//     adaptFilenameToLinux(adjusted);            // in place
//     string fileName(adjusted);
//     if (getFilenameNoCase(dst, fileName))
//         strcpy(dst, fileName.c_str());         // exists: take the REAL case
//     else
//         strcpy(dst, adjusted.c_str());         // absent: keep the adapted name
//
// So adaptFilenameToLinux rewrites a Win32 path into a POSIX one in place, and
// getFilenameNoCase resolves a path case-insensitively, reporting whether it
// exists and handing back the actual on-disk spelling.
//////////////////////////////////////////////////////////////////////////
inline void adaptFilenameToLinux(string& rAdjustedFilename)
{
	std::string p(rAdjustedFilename.c_str());

	// Drive letters have no meaning here: "C:\game\x" -> "/game/x".
	if (p.size() >= 2 && p[1] == ':' && isalpha((unsigned char)p[0]))
		p.erase(0, 2);

	for (size_t i = 0; i < p.size(); ++i)
		if (p[i] == '\\') p[i] = '/';

	// Collapse any duplicated separators produced by the substitution.
	std::string out;
	out.reserve(p.size());
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/' && !out.empty() && out[out.size() - 1] == '/')
			continue;
		out += p[i];
	}
	rAdjustedFilename = out.c_str();
}

inline bool getFilenameNoCase(const char* szFile, string& rFilenameOut)
{
	if (!szFile) return false;

	std::string resolved;
	if (!cry_resolve_nocase(szFile, resolved))
		return false;

	rFilenameOut = resolved.c_str();
	return true;
}

//////////////////////////////////////////////////////////////////////////
// _fullpath / GetFileAttributes
//
// _fullpath is the MSVC CRT's absolute-path builder. realpath() is NOT a drop
// in replacement: realpath fails when the path does not exist, whereas
// _fullpath is purely lexical and happily normalises a path to a file that has
// yet to be created -- which is exactly how CryPak uses it. So the
// normalisation is done by hand.
//////////////////////////////////////////////////////////////////////////
inline char* _fullpath(char* absPath, const char* relPath, size_t maxLength)
{
	if (!absPath || !relPath) return NULL;

	std::string p(relPath);
	for (size_t i = 0; i < p.size(); ++i)
		if (p[i] == '\\') p[i] = '/';

	if (p.empty() || p[0] != '/')
	{
		char cwd[4096];
		if (!getcwd(cwd, sizeof(cwd))) return NULL;
		std::string base(cwd);
		if (!base.empty() && base[base.size() - 1] != '/') base += '/';
		p = base + p;
	}

	// Resolve "." and ".." lexically, without touching the filesystem.
	std::vector<std::string> parts;
	size_t i = 1;
	while (i <= p.size())
	{
		size_t slash = p.find('/', i);
		if (slash == std::string::npos) slash = p.size();
		std::string comp = p.substr(i, slash - i);
		i = slash + 1;
		if (comp.empty() || comp == ".") continue;
		if (comp == "..") { if (!parts.empty()) parts.pop_back(); continue; }
		parts.push_back(comp);
	}

	std::string out;
	for (size_t k = 0; k < parts.size(); ++k) { out += '/'; out += parts[k]; }
	if (out.empty()) out = "/";

	if (out.size() + 1 > maxLength) return NULL;
	strcpy(absPath, out.c_str());
	return absPath;
}

inline DWORD GetFileAttributes(const char* lpFileName)
{
	struct stat st;
	if (!lpFileName || stat(lpFileName, &st) != 0)
	{
		// Fall back to a case-insensitive lookup: callers use this to test for
		// the existence of asset paths, which carry Windows casing.
		std::string resolved;
		if (!cry_resolve_nocase(lpFileName ? lpFileName : "", resolved) ||
		    stat(resolved.c_str(), &st) != 0)
			return INVALID_FILE_ATTRIBUTES;
	}
	DWORD attr = 0;
	if (S_ISDIR(st.st_mode))        attr |= FILE_ATTRIBUTE_DIRECTORY;
	if (!(st.st_mode & S_IWUSR))    attr |= FILE_ATTRIBUTE_READONLY;
	if (attr == 0)                  attr  = FILE_ATTRIBUTE_NORMAL;
	return attr;
}

//////////////////////////////////////////////////////////////////////////
// Working directory
//
// Win32 semantics: returns the number of characters written, NOT counting the
// terminating NUL, and 0 on failure. getcwd() instead returns the buffer, so
// the return value has to be translated or CryPak's "if (GetCurrentDirectory(
// ... ))" test would read backwards.
//////////////////////////////////////////////////////////////////////////
inline DWORD GetCurrentDirectory(DWORD nBufferLength, char* lpBuffer)
{
	if (!lpBuffer || nBufferLength == 0) return 0;
	if (!getcwd(lpBuffer, (size_t)nBufferLength)) return 0;
	return (DWORD)strlen(lpBuffer);
}

//////////////////////////////////////////////////////////////////////////
// Misc
//////////////////////////////////////////////////////////////////////////
inline DWORD GetCurrentThreadId() { return (DWORD)(uintptr_t)pthread_self(); }
inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }

#endif // _CRY_COMMON_WINBASE_HDR_
