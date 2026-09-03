////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   StaticModules.cpp
//  Description: The compiled-in replacement for dlopen()/dlsym().
//
//  See CryCommon/StaticModules.h for why the dynamic loader cannot be used.
//
//  Every factory below is declared by the module's own public header, which
//  is included here on purpose: it means the compiler checks these
//  declarations against the real ones instead of taking this file's word for
//  it. A signature that drifts is a compile error here, not a wild call at
//  runtime.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <StaticModules.h>

#include <IScriptSystem.h>
#include <IInput.h>
#include <INetwork.h>
#include <IEntitySystem.h>
#include <IFont.h>
#include <IPhysics.h>
#include <I3DEngine.h>
#include <ICryAnimation.h>
#include <IRenderer.h>
#include "CryMovie.h"

#include <string.h>
#include <ctype.h>

//! Not declared in any shared header -- the renderer's entry point is spelled
//! out at both ends (NULL_System.cpp defines it, SystemInit.cpp declares a
//! local typedef for it), so it is spelled out here too.
extern "C" IRenderer* PackageRenderConstructor(int argc, char* argv[], SCryRenderInterface* sp);

//////////////////////////////////////////////////////////////////////////

struct SCryStaticExport
{
	const char*	szName;
	void*		pAddress;
};

struct SCryStaticModule
{
	const char*					szName;		//!< module name, no extension, lower case
	const SCryStaticExport*		pExports;	//!< NULL-terminated
};

//////////////////////////////////////////////////////////////////////////
// The exports, one table per module. Each is the single factory the engine
// asks that module for.
//////////////////////////////////////////////////////////////////////////

#define CRY_EXPORT(fn)	{ #fn, (void*)&fn }
#define CRY_END			{ 0, 0 }

static const SCryStaticExport g_scriptSystemExports[]	= { CRY_EXPORT(CreateScriptSystem),			CRY_END };
static const SCryStaticExport g_inputExports[]			= { CRY_EXPORT(CreateInput),				CRY_END };
static const SCryStaticExport g_networkExports[]		= { CRY_EXPORT(CreateNetwork),				CRY_END };
static const SCryStaticExport g_entitySystemExports[]	= { CRY_EXPORT(CreateEntitySystem),			CRY_END };
static const SCryStaticExport g_fontExports[]			= { CRY_EXPORT(CreateCryFontInterface),		CRY_END };
static const SCryStaticExport g_physicsExports[]		= { CRY_EXPORT(CreatePhysicalWorld),		CRY_END };
static const SCryStaticExport g_3dEngineExports[]		= { CRY_EXPORT(CreateCry3DEngine),			CRY_END };
static const SCryStaticExport g_animationExports[]		= { CRY_EXPORT(CreateCharManager),			CRY_END };
static const SCryStaticExport g_movieExports[]			= { CRY_EXPORT(CreateMovieSystem),			CRY_END };
static const SCryStaticExport g_nullRendererExports[]	= { CRY_EXPORT(PackageRenderConstructor),	CRY_END };

//! CrySoundSystem is absent from this table entirely, unlike CryAISystem
//! below. The distinction is deliberate: CSystem::InitSound is wrapped in
//! "#if !defined(LINUX)" and is never reached here, so nothing ever asks for
//! the module -- and the module itself cannot be built, being a wrapper over
//! the proprietary "crysound" (FMOD) library that ships as a Windows .lib
//! with no source. Registering it would advertise something that does not
//! exist. See Headless/CMakeLists.txt.

//! CryAISystem is KNOWN BUT EXPORTS NOTHING, and that is not an oversight:
//! the public Far Cry source drop does not contain CAISystem, AIObject,
//! AIPlayer or the GoalOp sources at all -- neither headers nor
//! implementation -- so there is no CreateAISystem to point at. The module
//! is listed anyway rather than omitted, because the two outcomes differ:
//!
//!   omitted        -> LoadDLL() returns NULL and, with its default
//!                     bQuitIfNotFound, calls Quit() -- the engine dies.
//!   present, empty -> CryGetProcAddress() returns NULL, and
//!                     CSystem::InitAISystem() takes the tolerant path it
//!                     already has ("Cannot fins entry proc"), logs, and
//!                     continues without an AI system.
//!
//! The second is both survivable and truthful: the module really is part of
//! this binary, and it really has no entry point.
static const SCryStaticExport g_aiSystemExports[]		= { CRY_END };

//////////////////////////////////////////////////////////////////////////
// The module table. Names are lower case and extension-free; see
// NormaliseName() for why.
//////////////////////////////////////////////////////////////////////////
static const SCryStaticModule g_modules[] =
{
	{ "cryscriptsystem",	g_scriptSystemExports	},
	{ "cryinput",			g_inputExports			},
	{ "crynetwork",			g_networkExports		},
	{ "cryentitysystem",	g_entitySystemExports	},
	{ "cryfont",			g_fontExports			},
	{ "cryphysics",			g_physicsExports		},
	{ "cry3dengine",		g_3dEngineExports		},
	{ "cryanimation",		g_animationExports		},
	{ "crymovie",			g_movieExports			},
	{ "cryaisystem",		g_aiSystemExports		},
	{ "xrendernull",		g_nullRendererExports	},
};

static const int g_nModules = (int)(sizeof(g_modules) / sizeof(g_modules[0]));

//////////////////////////////////////////////////////////////////////////
//! Reduce a library name to the bare module name.
//!
//! The engine is not consistent about how it spells these. Within a single
//! file SystemInit.cpp uses "cryphysics.so" from a macro, "cryanimation.so"
//! as a literal, and "XRenderNULL.dll" from the Windows branch; CryLoadLibrary
//! also appends "_debug" to the stem in non-release builds. Normalising here
//! means the table has one entry per module rather than one per spelling.
//!
//! Returns false if the name will not fit, which for a module name means it
//! is not one of ours.
//////////////////////////////////////////////////////////////////////////
static bool NormaliseName(const char* szLibName, char* szOut, size_t nOutSize)
{
	if (!szLibName || !szOut || nOutSize == 0)
		return false;

	// Strip any directory prefix, in either separator flavour.
	const char* szStart = szLibName;
	for (const char* p = szLibName; *p; ++p)
	{
		if (*p == '/' || *p == '\\')
			szStart = p + 1;
	}

	// Copy up to the last '.', lower-cased.
	const char* szDot = strrchr(szStart, '.');
	size_t nLen = szDot ? (size_t)(szDot - szStart) : strlen(szStart);
	if (nLen == 0 || nLen >= nOutSize)
		return false;

	for (size_t i = 0; i < nLen; ++i)
		szOut[i] = (char)tolower((unsigned char)szStart[i]);
	szOut[nLen] = 0;

	// Drop the debug-build suffix CryLoadLibrary() would have appended.
	const size_t nSuffix = 6;	// "_debug"
	if (nLen > nSuffix && strcmp(szOut + nLen - nSuffix, "_debug") == 0)
		szOut[nLen - nSuffix] = 0;

	return true;
}

//////////////////////////////////////////////////////////////////////////

extern "C" void* CryStaticLoadModule(const char* szLibName)
{
	char szName[128];
	if (!NormaliseName(szLibName, szName, sizeof(szName)))
		return 0;

	for (int i = 0; i < g_nModules; ++i)
	{
		if (strcmp(g_modules[i].szName, szName) == 0)
		{
			// The handle is the table entry itself: non-NULL, stable for the
			// lifetime of the process, and directly usable by GetProcAddress.
			return (void*)&g_modules[i];
		}
	}

	// Not one of ours. Callers asking for ddraw.dll, kernel32.dll or
	// VTuneApi.dll land here and already cope with NULL.
	return 0;
}

extern "C" void* CryStaticGetProcAddress(void* hModule, const char* szProcName)
{
	if (!hModule || !szProcName)
		return 0;

	const SCryStaticModule* pModule = (const SCryStaticModule*)hModule;
	for (const SCryStaticExport* pExport = pModule->pExports; pExport->szName; ++pExport)
	{
		if (strcmp(pExport->szName, szProcName) == 0)
			return pExport->pAddress;
	}

	return 0;
}

extern "C" int CryStaticFreeModule(void* /*hModule*/)
{
	// Nothing to unload; the code is in this binary either way. Non-zero is
	// FreeLibrary()'s success value, which is what the call sites expect.
	return 1;
}
