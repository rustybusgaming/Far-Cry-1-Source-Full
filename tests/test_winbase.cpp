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

//////////////////////////////////////////////////////////////////////////
// Path adaptation -- the helpers CryPak.cpp needs on the LINUX seam.
//////////////////////////////////////////////////////////////////////////
static void test_adaptFilenameToLinux()
{
	printf("adaptFilenameToLinux:\n");

	string a("Game\\Levels\\Pier.pak");
	adaptFilenameToLinux(a);
	check(strcmp(a.c_str(), "Game/Levels/Pier.pak") == 0,
	      "backslashes become forward slashes");

	string b("C:\\FarCry\\Game\\x.pak");
	adaptFilenameToLinux(b);
	check(strcmp(b.c_str(), "/FarCry/Game/x.pak") == 0,
	      "drive letter is stripped");

	string c("Game\\\\Levels//Pier.pak");
	adaptFilenameToLinux(c);
	check(strcmp(c.c_str(), "Game/Levels/Pier.pak") == 0,
	      "duplicate separators collapse");

	string d("already/posix/path.pak");
	adaptFilenameToLinux(d);
	check(strcmp(d.c_str(), "already/posix/path.pak") == 0,
	      "a POSIX path is left unchanged");
}

static void test_fullpath()
{
	printf("_fullpath:\n");
	char buf[4096];

	check(_fullpath(buf, "/a/b/../c/./d", sizeof(buf)) != NULL &&
	      strcmp(buf, "/a/c/d") == 0, "resolves . and .. lexically");

	check(_fullpath(buf, "/a/b\\c", sizeof(buf)) != NULL &&
	      strcmp(buf, "/a/b/c") == 0, "accepts Win32 separators");

	// Unlike realpath(), _fullpath must work on a path that does not exist.
	check(_fullpath(buf, "/no/such/file/anywhere.dat", sizeof(buf)) != NULL &&
	      strcmp(buf, "/no/such/file/anywhere.dat") == 0,
	      "does not require the path to exist");

	check(_fullpath(buf, "relative/path", sizeof(buf)) != NULL &&
	      buf[0] == '/', "a relative path is made absolute");

	check(_fullpath(buf, "/a/b", 3) == NULL, "refuses to overflow the buffer");
}

static void test_file_attributes()
{
	printf("GetFileAttributes / getFilenameNoCase:\n");

	mkdir("gatest", 0755);
	write_file("gatest/Thing.dat", "SKYDATA");

	DWORD d = GetFileAttributes("gatest");
	check(d != INVALID_FILE_ATTRIBUTES && (d & FILE_ATTRIBUTE_DIRECTORY),
	      "directory reports FILE_ATTRIBUTE_DIRECTORY");

	DWORD f = GetFileAttributes("gatest/Thing.dat");
	check(f != INVALID_FILE_ATTRIBUTES && !(f & FILE_ATTRIBUTE_DIRECTORY),
	      "regular file does not report DIRECTORY");

	check(GetFileAttributes("gatest/THING.DAT") != INVALID_FILE_ATTRIBUTES,
	      "resolves through a case mismatch");

	check(GetFileAttributes("gatest/absent.dat") == INVALID_FILE_ATTRIBUTES,
	      "absent file reports INVALID_FILE_ATTRIBUTES");

	string out;
	check(getFilenameNoCase("gatest/THING.DAT", out),
	      "getFilenameNoCase finds a mis-cased file");
	check(strcmp(out.c_str(), "gatest/Thing.dat") == 0,
	      "and reports the real on-disk spelling");

	string out2;
	check(!getFilenameNoCase("gatest/nope.dat", out2),
	      "getFilenameNoCase fails on an absent file");

	remove("gatest/Thing.dat");
	rmdir("gatest");
}

//////////////////////////////////////////////////////////////////////////
// CRT shims and the remaining Linux-layer text helpers.
//////////////////////////////////////////////////////////////////////////
static void test_crt_shims()
{
	printf("CRT shims:\n");

	char b1[32]; strcpy(b1, "MiXeD Case");
	check(strcmp(strlwr(b1), "mixed case") == 0, "strlwr lowercases in place");
	strcpy(b1, "MiXeD Case");
	check(strcmp(strupr(b1), "MIXED CASE") == 0, "strupr uppercases in place");

	check(memicmp("Hello", "hELLO", 5) == 0,   "memicmp ignores case");
	check(memicmp("Hella", "hELLO", 5) != 0,   "memicmp still detects a difference");

	char n[36];
	check(strcmp(itoa(12345, n, 10), "12345") == 0,   "itoa base 10");
	check(strcmp(itoa(-42, n, 10), "-42") == 0,       "itoa negative");
	check(strcmp(itoa(0, n, 10), "0") == 0,           "itoa zero");
	check(strcmp(itoa(255, n, 16), "ff") == 0,        "itoa base 16");
	check(strcmp(itoa(5, n, 2), "101") == 0,          "itoa base 2");

	check(Int32x32To64(100000, 100000) == 10000000000LL,
	      "Int32x32To64 does not overflow through 32 bits");
}

static void test_text_helpers()
{
	printf("Linux-layer text helpers:\n");

	// Matches the #else branch it replaces: stricmp(...) == 0
	check(compareTextFileStrings("Entity", "entity") == 0, "tag compare ignores case");
	check(compareTextFileStrings("Entity", "Entry")  != 0, "different tags differ");

	string t("line one\r\nline two\n");
	RemoveCRLF(t);
	check(strcmp(t.c_str(), "line oneline two") == 0, "RemoveCRLF strips CR and LF");

	char p1[64]; strcpy(p1, "game//levels///pier.pak");
	replaceDoublePathFilename(p1);
	check(strcmp(p1, "game/levels/pier.pak") == 0, "collapses repeated separators");

	char p2[64]; strcpy(p2, "game\\\\levels\\pier.pak");
	replaceDoublePathFilename(p2);
	check(strcmp(p2, "game/levels/pier.pak") == 0, "normalises backslashes too");

	char p3[64]; strcpy(p3, "already/clean/path.pak");
	replaceDoublePathFilename(p3);
	check(strcmp(p3, "already/clean/path.pak") == 0, "leaves a clean path alone");
}

//////////////////////////////////////////////////////////////////////////
// _findfirst64 / _findnext64 / _findclose
//
// The most intricate shim in the header: it has to reproduce Win32 wildcard
// semantics, not POSIX ones. CryPak mounts every pak it finds through this,
// so a mistake here means the engine silently mounts nothing.
//////////////////////////////////////////////////////////////////////////
static int count_matches(const char* spec, unsigned* lastAttrib = NULL)
{
	__finddata64_t fd;
	intptr_t h = _findfirst64(spec, &fd);
	if (h == -1) return 0;
	int n = 0;
	do {
		++n;
		if (lastAttrib) *lastAttrib = fd.attrib;
	} while (_findnext64(h, &fd) == 0);
	_findclose(h);
	return n;
}

static void test_find_api()
{
	printf("_findfirst64 / _findnext64:\n");

	mkdir("findtest", 0755);
	mkdir("findtest/SubDir", 0755);
	write_file("findtest/Levels.pak", "SKYDATA");
	write_file("findtest/Objects.PAK", "SKYDATA");
	write_file("findtest/readme.txt", "SKYDATA");

	// Win32 wildcard matching is case-insensitive: "*.pak" must find the
	// file named "Objects.PAK" as well.
	check(count_matches("findtest/*.pak") == 2,
	      "*.pak matches both .pak and .PAK (case-insensitive)");

	check(count_matches("findtest/*.txt") == 1, "*.txt matches one file");
	check(count_matches("findtest/*.dds") == 0, "no match returns nothing");
	check(_findfirst64("findtest/*.dds", (__finddata64_t*)NULL) == -1,
	      "null finddata is rejected");

	// A directory that does not exist must fail rather than crash.
	__finddata64_t fd;
	check(_findfirst64("no_such_dir/*.pak", &fd) == -1,
	      "absent directory returns -1");

	// Directories report _A_SUBDIR.
	unsigned attrib = 0;
	check(count_matches("findtest/SubDir", &attrib) == 1 &&
	      (attrib & _A_SUBDIR), "directory entry reports _A_SUBDIR");

	unsigned fattrib = 0;
	count_matches("findtest/readme.txt", &fattrib);
	check(!(fattrib & _A_SUBDIR), "regular file does not report _A_SUBDIR");

	// Win32 separators in the spec.
	check(count_matches("findtest\\*.pak") == 2,
	      "backslash separators in the filespec work");

	// "*" returns "." and ".." exactly as FindFirstFile does; callers such as
	// Deltree rely on seeing and skipping them.
	bool sawDot = false, sawDotDot = false;
	__finddata64_t e;
	intptr_t h = _findfirst64("findtest/*", &e);
	if (h != -1) {
		do {
			if (strcmp(e.name, ".") == 0)  sawDot = true;
			if (strcmp(e.name, "..") == 0) sawDotDot = true;
		} while (_findnext64(h, &e) == 0);
		_findclose(h);
	}
	check(sawDot && sawDotDot, "'.' and '..' are returned, as on Win32");

	remove("findtest/Levels.pak");
	remove("findtest/Objects.PAK");
	remove("findtest/readme.txt");
	rmdir("findtest/SubDir");
	rmdir("findtest");
}

static void test_filetime()
{
	printf("SYSTEMTIME <-> FILETIME:\n");

	// The Win32 epoch is 1601-01-01, not 1970-01-01.
	SYSTEMTIME st;
	memset(&st, 0, sizeof(st));
	st.wYear = 1970; st.wMonth = 1; st.wDay = 1;

	FILETIME ft;
	check(SystemTimeToFileTime(&st, &ft) == TRUE, "converts a valid time");

	unsigned long long v = ((unsigned long long)ft.dwHighDateTime << 32)
	                     | (unsigned long long)ft.dwLowDateTime;
	check(v == 116444736000000000ULL,
	      "the Unix epoch is the documented FILETIME constant");

	// Round trip.
	SYSTEMTIME st2;
	memset(&st2, 0, sizeof(st2));
	st.wYear = 2004; st.wMonth = 3; st.wDay = 23;
	st.wHour = 14;   st.wMinute = 30; st.wSecond = 45;
	check(SystemTimeToFileTime(&st, &ft) && FileTimeToSystemTime(&ft, &st2),
	      "round trip succeeds");
	check(st2.wYear == 2004 && st2.wMonth == 3 && st2.wDay == 23 &&
	      st2.wHour == 14 && st2.wMinute == 30 && st2.wSecond == 45,
	      "round trip preserves every field");
}

//////////////////////////////////////////////////////////////////////////
// BMP file headers -- a FILE FORMAT, so the layout is not ours to choose.
//
// These sizes are the whole point of the #pragma pack(2) around them. Without
// it the compiler pads after the 2-byte bfType and the file header becomes 16
// bytes, which produces a BMP no reader will open -- and nothing else would
// catch it, because the code still compiles and still writes a file.
//////////////////////////////////////////////////////////////////////////
static void test_bmp_headers()
{
	printf("BMP headers:\n");
	check(sizeof(BITMAPFILEHEADER) == 14, "BITMAPFILEHEADER is exactly 14 bytes");
	check(sizeof(BITMAPINFOHEADER) == 40, "BITMAPINFOHEADER is exactly 40 bytes");
	check(sizeof(RGBQUAD) == 4,           "RGBQUAD is exactly 4 bytes");

	// Field offsets matter as much as the total size.
	BITMAPFILEHEADER h;
	const char* base = (const char*)&h;
	check((const char*)&h.bfSize    - base == 2,  "bfSize is at offset 2");
	check((const char*)&h.bfOffBits - base == 10, "bfOffBits is at offset 10");
}

//////////////////////////////////////////////////////////////////////////
// DEFINE_ALIGNED_DATA_* -- alignment must SURVIVE, not just parse.
//
// The macros originally put __attribute__((aligned)) after the declarator,
// which parses for a bare declaration and is a syntax error once the name has
// a constructor initialiser. Moving the attribute between type and name fixes
// the parse -- but a wrongly placed attribute can also be silently ignored,
// which compiles and then misaligns SSE loads at runtime. So the alignment is
// asserted, not assumed, in both forms the engine uses.
//////////////////////////////////////////////////////////////////////////
DEFINE_ALIGNED_DATA_STATIC( float, s_alignedInit[4], 16 );
DEFINE_ALIGNED_DATA_STATIC( float, s_alignedBare[4], 16 );

static void test_aligned_data()
{
	printf("DEFINE_ALIGNED_DATA:\n");
	check(((UINT_PTR)&s_alignedInit % 16) == 0, "static array is 16-byte aligned");
	check(((UINT_PTR)&s_alignedBare % 16) == 0, "second declaration also aligned");

	DEFINE_ALIGNED_DATA( float, local[4], 16 );
	check(((UINT_PTR)&local % 16) == 0, "non-static form is 16-byte aligned");
}

int main()
{
	test_comparePathNames();
	test_fopen_nocase();
	test_adaptFilenameToLinux();
	test_fullpath();
	test_file_attributes();
	test_crt_shims();
	test_text_helpers();
	test_find_api();
	test_filetime();
	test_bmp_headers();
	test_aligned_data();
	test_critical_section();
	test_interlocked();

	printf("\n%s (%d failure%s)\n", g_fails ? "FAILED" : "all passed",
	       g_fails, g_fails == 1 ? "" : "s");
	return g_fails != 0;
}
