////////////////////////////////////////////////////////////////////////////
//
//  Regression tests for CryCommon/WinBase.h
//
//  WinBase.h is a reconstruction of a header Crytek never shipped, so unlike
//  the rest of the port there is no original to diff against. Two of its
//  functions carry real logic that the engine's asset loading depends on, and
//  a subtle mistake in either would not surface as a compile error -- it would
//  surface much later as "the game loads no textures", which is a miserable
//  thing to debug through a wasm build.
//
//  So they get tested directly.
//
//  Built as part of the normal CMake build; run with ctest.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

static int g_fails = 0;

static void check(bool ok, const char* what)
{
	printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok) ++g_fails;
}

//////////////////////////////////////////////////////////////////////////
// comparePathNames -- returns 0 when the paths match, like strncmp.
// Windows path matching is both case-insensitive and separator-insensitive,
// and the pak data relies on both.
//////////////////////////////////////////////////////////////////////////
static void test_comparePathNames()
{
	printf("comparePathNames:\n");
	check(comparePathNames("Textures/Sky.dds", "textures/sky.dds", 16) == 0,
	      "case-insensitive");
	check(comparePathNames("Textures\\Sky.dds", "Textures/Sky.dds", 16) == 0,
	      "backslash equals forward slash");
	check(comparePathNames("TEXTURES\\SKY.DDS", "textures/sky.dds", 16) == 0,
	      "mixed case and separators together");
	check(comparePathNames("Levels/Pier", "Levels/Volcano", 11) != 0,
	      "genuinely different paths compare unequal");
	check(comparePathNames("abc", "abd", 3) != 0,
	      "difference in the final character is caught");
	check(comparePathNames("abc", "abc", 10) == 0,
	      "stops at NUL even when len overruns it");
	check(comparePathNames("ab", "abcdef", 2) == 0,
	      "compares only the requested prefix length");
}

//////////////////////////////////////////////////////////////////////////
// fopen_nocase -- the reason the engine can find its assets at all on a
// case-sensitive filesystem. Far Cry's data references the same file with
// inconsistent casing and Win32 separators.
//////////////////////////////////////////////////////////////////////////
static void write_file(const char* path, const char* text)
{
	FILE* f = fopen(path, "wb");
	if (f) { fputs(text, f); fclose(f); }
}

static bool opens_ok(const char* path)
{
	FILE* f = fopen_nocase(path, "rb");
	if (!f) return false;
	char buf[16] = {0};
	size_t n = fread(buf, 1, 7, f);
	fclose(f);
	return n == 7 && strncmp(buf, "SKYDATA", 7) == 0;
}

static void test_fopen_nocase()
{
	printf("fopen_nocase:\n");

	// Build a small tree with deliberately mixed-case names.
	mkdir("wbtest", 0755);
	mkdir("wbtest/Textures", 0755);
	mkdir("wbtest/Textures/Sky", 0755);
	write_file("wbtest/Textures/Sky/Clouds.dds", "SKYDATA");

	check(opens_ok("wbtest/Textures/Sky/Clouds.dds"),   "exact path");
	check(opens_ok("wbtest/textures/sky/clouds.dds"),   "all lowercase");
	check(opens_ok("wbtest/TEXTURES/SKY/CLOUDS.DDS"),   "all uppercase");
	check(opens_ok("wbtest\\Textures\\Sky\\Clouds.dds"),"Win32 separators");
	check(opens_ok("wbtest/TeXtUrEs\\sKy/ClOuDs.DdS"),  "mixed case and separators");

	check(fopen_nocase("wbtest/Textures/Sky/Missing.dds", "rb") == NULL,
	      "absent file still fails");
	check(fopen_nocase("wbtest/Nope/Nowhere.dds", "rb") == NULL,
	      "absent directory still fails");

	// A write-mode open must succeed for a file that does not exist yet,
	// otherwise the engine can never create logs or save games.
	FILE* w = fopen_nocase("wbtest/NewFile.txt", "wb");
	check(w != NULL, "write mode creates a new file");
	if (w) fclose(w);

	remove("wbtest/NewFile.txt");
	remove("wbtest/Textures/Sky/Clouds.dds");
	rmdir("wbtest/Textures/Sky");
	rmdir("wbtest/Textures");
	rmdir("wbtest");
}

//////////////////////////////////////////////////////////////////////////
// The synchronisation primitives are shims over pthreads; check the parts
// where Win32 semantics differ from the POSIX default.
//////////////////////////////////////////////////////////////////////////
static void test_critical_section()
{
	printf("CRITICAL_SECTION:\n");

	CRITICAL_SECTION cs;
	InitializeCriticalSection(&cs);

	// Win32 critical sections are RECURSIVE. A plain pthread mutex is not,
	// and would self-deadlock right here.
	EnterCriticalSection(&cs);
	EnterCriticalSection(&cs);
	LeaveCriticalSection(&cs);
	LeaveCriticalSection(&cs);
	check(true, "recursive acquisition does not deadlock");

	DeleteCriticalSection(&cs);

	// The engine enters a few sections before their owner is constructed;
	// EnterCriticalSection initialises on first use rather than faulting.
	CRITICAL_SECTION zeroed;
	memset(&zeroed, 0, sizeof(zeroed));
	EnterCriticalSection(&zeroed);
	LeaveCriticalSection(&zeroed);
	check(true, "entering a zero-initialised section self-initialises");
	DeleteCriticalSection(&zeroed);
}

static void test_interlocked()
{
	printf("Interlocked*:\n");
	LONG v = 5;
	// Win32 convention: Increment/Decrement return the NEW value.
	check(InterlockedIncrement(&v) == 6, "Increment returns the new value");
	check(InterlockedDecrement(&v) == 5, "Decrement returns the new value");
	// Exchange returns the OLD value.
	check(InterlockedExchange(&v, 42) == 5, "Exchange returns the old value");
	check(v == 42, "Exchange stored the new value");
	check(InterlockedCompareExchange(&v, 7, 42) == 42, "CompareExchange returns old");
	check(v == 7, "CompareExchange swapped on a match");
	check(InterlockedCompareExchange(&v, 99, 1234) == 7, "CompareExchange no match");
	check(v == 7, "CompareExchange left the value alone on mismatch");
}

int main()
{
	test_comparePathNames();
	test_fopen_nocase();
	test_critical_section();
	test_interlocked();

	printf("\n%s (%d failure%s)\n", g_fails ? "FAILED" : "all passed",
	       g_fails, g_fails == 1 ? "" : "s");
	return g_fails != 0;
}
