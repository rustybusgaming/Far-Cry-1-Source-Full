//////////////////////////////////////////////////////////////////////
//
//	Crytek Network source code
//	
//	File: crynetwork.cpp
//  Description: dll entry point
//
//	History:
//	-July 25,2001:Created by Alberto Demichelis
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "CNP.h"
#include "Client.h"
#include "Server.h"
#include "Network.h"

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

#if !defined(XBOX)
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

#ifndef _XBOX
CRYNETWORK_API INetwork *CreateNetwork(ISystem *pSystem)
#else
INetwork *CreateNetwork(ISystem *pSystem)
#endif
{
	gISystem = pSystem;
	CNetwork *pNetwork=new CNetwork;

	if(!pNetwork->Init(gISystem->GetIScriptSystem()))
	{
		delete pNetwork;
		return NULL;
	}
	return pNetwork;
}

#include <CrtDebugStats.h>

