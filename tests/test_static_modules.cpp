////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   test_static_modules.cpp
//  Description: Tests for the compiled-in module registry.
//
//  The registry replaces dlopen()/dlsym() for the single-link-unit build, so
//  a mistake in it does not fail to compile -- it fails at engine startup as
//  a missing module, which reads exactly like a module that was never ported.
//  These tests keep that distinction sharp.
//
////////////////////////////////////////////////////////////////////////////

#include <StaticModules.h>

#include <stdio.h>
#include <string.h>

static int g_nFailures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_nFailures; \
		} \
	} while (0)

//////////////////////////////////////////////////////////////////////////
//! The engine spells module names inconsistently -- "cryphysics.so" from a
//! macro, "cryanimation.so" as a literal, "XRenderNULL.dll" from the Windows
//! branch -- and CryLoadLibrary would have appended "_debug" in a debug build.
//! All of those have to reach the same module.
//////////////////////////////////////////////////////////////////////////
static void TestNameNormalisation()
{
	void* pExpected = CryStaticLoadModule("cryphysics.so");
	CHECK(pExpected != 0);

	CHECK(CryStaticLoadModule("CryPhysics.dll")     == pExpected);
	CHECK(CryStaticLoadModule("cryphysics")         == pExpected);
	CHECK(CryStaticLoadModule("CRYPHYSICS.SO")      == pExpected);
	CHECK(CryStaticLoadModule("cryphysics_debug.so")== pExpected);
	CHECK(CryStaticLoadModule("./cryphysics.so")    == pExpected);
	CHECK(CryStaticLoadModule("Bin32\\CryPhysics.dll") == pExpected);
}

//////////////////////////////////////////////////////////////////////////
//! Every module CSystem::Init asks for, and the exact symbol it asks for.
//! The names are quoted from SystemInit.cpp -- if one drifts, the engine
//! silently loses that subsystem, so they are pinned here.
//////////////////////////////////////////////////////////////////////////
static void TestEveryFactoryResolves()
{
	static const char* const kModules[][2] =
	{
		{ "cryscriptsystem.so",	"CreateScriptSystem"		},
		{ "cryinput.so",		"CreateInput"				},
		{ "crynetwork.so",		"CreateNetwork"				},
		{ "cryentitysystem.so",	"CreateEntitySystem"		},
		{ "cryfont.so",			"CreateCryFontInterface"	},
		{ "cryphysics.so",		"CreatePhysicalWorld"		},
		{ "cry3dengine.so",		"CreateCry3DEngine"			},
		{ "cryanimation.so",	"CreateCharManager"			},
		{ "crymovie.so",		"CreateMovieSystem"			},
		{ "xrendernull.so",		"PackageRenderConstructor"	},
	};

	const int n = (int)(sizeof(kModules) / sizeof(kModules[0]));
	for (int i = 0; i < n; ++i)
	{
		void* hModule = CryStaticLoadModule(kModules[i][0]);
		if (!hModule)
		{
			printf("FAIL module not registered: %s\n", kModules[i][0]);
			++g_nFailures;
			continue;
		}

		if (!CryStaticGetProcAddress(hModule, kModules[i][1]))
		{
			printf("FAIL %s does not export %s\n", kModules[i][0], kModules[i][1]);
			++g_nFailures;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//! CryAISystem is registered but exports nothing, because the public source
//! drop contains no CAISystem to point at. That combination is load-bearing:
//! an unregistered module makes CSystem::LoadDLL call Quit() and kill the
//! engine, whereas a registered one with no entry point takes the tolerant
//! path in InitAISystem() and merely logs. Both halves are asserted.
//////////////////////////////////////////////////////////////////////////
static void TestAISystemLoadsButExportsNothing()
{
	void* hAI = CryStaticLoadModule("cryaisystem.so");
	CHECK(hAI != 0);
	CHECK(CryStaticGetProcAddress(hAI, "CreateAISystem") == 0);
}

//////////////////////////////////////////////////////////////////////////
//! Things that must NOT resolve. Several code paths still ask for Windows
//! system libraries; they test the result, so NULL is the right answer and
//! a stray match would be worse than no match.
//////////////////////////////////////////////////////////////////////////
static void TestUnknownModulesReturnNull()
{
	CHECK(CryStaticLoadModule("ddraw.dll")    == 0);
	CHECK(CryStaticLoadModule("kernel32.dll") == 0);
	CHECK(CryStaticLoadModule("VTuneApi.dll") == 0);
	CHECK(CryStaticLoadModule("")             == 0);
	CHECK(CryStaticLoadModule(0)              == 0);

	// CrySoundSystem is deliberately absent: it wraps proprietary FMOD that
	// ships only as a Windows .lib, and CSystem::InitSound is compiled out on
	// this platform anyway.
	CHECK(CryStaticLoadModule("crysoundsystem.so") == 0);

	// A real module asked for a symbol it does not export.
	void* hInput = CryStaticLoadModule("cryinput.so");
	CHECK(hInput != 0);
	CHECK(CryStaticGetProcAddress(hInput, "CreateRenderer") == 0);
	CHECK(CryStaticGetProcAddress(hInput, "") == 0);
	CHECK(CryStaticGetProcAddress(hInput, 0) == 0);
	CHECK(CryStaticGetProcAddress(0, "CreateInput") == 0);
}

//////////////////////////////////////////////////////////////////////////
//! Handles are compared for identity by nothing in the engine, but they must
//! be stable: CSystem stores them in m_dll for the lifetime of the process
//! and calls CryGetProcAddress on them long after loading.
//////////////////////////////////////////////////////////////////////////
static void TestHandlesAreStable()
{
	void* a = CryStaticLoadModule("cry3dengine.so");
	void* b = CryStaticLoadModule("cry3dengine.so");
	CHECK(a != 0);
	CHECK(a == b);
	CHECK(CryStaticGetProcAddress(a, "CreateCry3DEngine") ==
	      CryStaticGetProcAddress(b, "CreateCry3DEngine"));

	// Distinct modules must not alias.
	CHECK(CryStaticLoadModule("cryinput.so") != CryStaticLoadModule("crynetwork.so"));

	// Freeing is a no-op but must report success, as FreeLibrary() does.
	CHECK(CryStaticFreeModule(a) != 0);
	CHECK(CryStaticGetProcAddress(a, "CreateCry3DEngine") != 0);
}

int main()
{
	TestNameNormalisation();
	TestEveryFactoryResolves();
	TestAISystemLoadsButExportsNothing();
	TestUnknownModulesReturnNull();
	TestHandlesAreStable();

	if (g_nFailures)
	{
		printf("\n%d failure(s)\n", g_nFailures);
		return 1;
	}
	printf("static_modules: all tests passed\n");
	return 0;
}
