// CryPhysics.cpp : Defines the entry point for the DLL application.
//
#include "StdAfx.h"
//#include <float.h>

#ifndef GAMECUBE
#ifndef _XBOX
//#if !defined(LINUX)
_ACCESS_POOL;
//#endif//LINUX
#endif //_XBOX
#include <CrtDebugStats.h>
#endif

#include "IPhysics.h"
#include "geoman.h"
#include "bvtree.h"
#include "geometry.h"
#include "rigidbody.h"
#include "physicalplaceholder.h"
#include "physicalentity.h"
#include "physicalworld.h"

float g_costab[SINCOSTABSZ],g_sintab[SINCOSTABSZ];

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
//////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}
#endif

#ifndef _XBOX
CRYPHYSICS_API IPhysicalWorld *CreatePhysicalWorld(ISystem *pSystem)
#else
IPhysicalWorld *CreatePhysicalWorld(ISystem *pSystem)
#endif
{
	gISystem = pSystem;
	g_bHasSSE = (pSystem->GetCPUFlags() & CPUF_SSE)!=0;
	for(int i=0; i<SINCOSTABSZ; i++) {
		g_costab[i] = cosf(i*(pi*0.5f/SINCOSTABSZ));
		g_sintab[i] = sinf(i*(pi*0.5f/SINCOSTABSZ));
	}
	//_controlfp(_EM_ZERODIVIDE,_MCW_EM);
	return new CPhysicalWorld(pSystem->GetILog());
}

