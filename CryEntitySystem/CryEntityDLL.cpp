// CryEntityDLL.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h"
#if !defined(_XBOX) && !defined(LINUX)
#include <windows.h>
#else
#if !defined(LINUX)
#include <xtl.h>
#endif
#endif
#include <IEntitySystem.h>
#include "EntitySystem.h"

#if defined(_DEBUG) && !defined(LINUX)
static char THIS_FILE[] = __FILE__;
#define DEBUG_CLIENTBLOCK new( _NORMAL_BLOCK, THIS_FILE, __LINE__)
#define new DEBUG_CLIENTBLOCK
#endif

//////////////////////////////////////////////////////////////////////////
// Pointer to Global ISystem.
static ISystem* gISystem = 0;
// [webport] One link unit, one GetISystem().
//
// ISystem.h declares a single global GetISystem(), and every module DLL used
// to carry its own definition of it, private to that DLL and initialised by
// that module's factory. Linked together they are duplicate symbols.
//
// Collapsing them onto CrySystem's definition is not a behaviour change: each
// copy returned the pointer to the one CSystem that CrySystem had already
// created, and the module factories run during CSystem::Init, after
// CrySystem's g_System is set. The local pointer below is left in place --
// this module's own code still assigns and reads it.
//
// See CryCommon/StaticModules.h for why there is only one link unit.
#if !defined(_CRY_STATIC_MODULES)
ISystem* GetISystem()
{
	return gISystem;
}
#endif // !_CRY_STATIC_MODULES

// Local var to turn on/off profiler.
// [webport] One link unit: the profiler switch belongs to the module that owns
// the profiler, CrySystem/FrameProfileSystem.cpp. This was a per-DLL copy.
#if !defined(_CRY_STATIC_MODULES)
bool g_bProfilerEnabled = false;
#endif
//////////////////////////////////////////////////////////////////////////

#if !defined(_XBOX)
 _ACCESS_POOL;
#if !defined(LINUX)
BOOL APIENTRY DllMain( HANDLE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
    return TRUE;
}
#endif
#endif

#if !defined(_XBOX) && !defined(LINUX) && !defined(PS2)
CRYENTITYDLL_API struct IEntitySystem * CreateEntitySystem(ISystem *pISystem)
#else
struct IEntitySystem * CreateEntitySystem(ISystem *pISystem)
#endif
{
	gISystem = pISystem;
	CEntitySystem *pEntitySystem= new CEntitySystem(pISystem);
	if(!pEntitySystem->Init(pISystem))
	{
		pEntitySystem->Release();
		return NULL;
	}
	return pEntitySystem;
}

//////////////////////////////////////////////////////////////////////////

#ifdef GERMAN_GORE_CHECK

#if !defined(_XBOX) && !defined(LINUX) && !defined(PS2)
CRYENTITYDLL_API struct IEntitySystem * CreateMainEntitySystem(ISystem *pISystem)
#else
struct IEntitySystem * CreateMainEntitySystem(ISystem *pISystem)
#endif
{
	gISystem = pISystem;
	return (IEntitySystem *) gISystem;
}

#endif
//////////////////////////////////////////////////////////////////////////


#include <CrtDebugStats.h>
