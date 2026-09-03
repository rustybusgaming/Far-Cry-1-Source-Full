//////////////////////////////////////////////////////////////////////
//
//  CryFont Source Code
//
//  File: ICryFont.cpp
//  Description: Create the font interface.
//
//  History:
//  - August 17, 2001: Created by Alberto Demichelis
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#ifndef _XBOX
_ACCESS_POOL;
#endif //_XBOX

#include "CryFont.h"

ISystem *gISystem = 0;
//! Get the system interface 
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
ISystem	*GetISystem()
{
	return gISystem;
}
#endif // !_CRY_STATIC_MODULES

///////////////////////////////////////////////
extern "C" ICryFont* CreateCryFontInterface(ISystem *pSystem)
{
	gISystem = pSystem;
	return new CCryFont(pSystem);
}

///////////////////////////////////////////////
// [webport] DllMain is the Win32 DLL entry point; there are no DLLs on Linux
// and none at all in wasm, where every module links into one unit.
#if !defined(_XBOX) && !defined(LINUX)
#ifndef PS2
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}
#endif
#endif