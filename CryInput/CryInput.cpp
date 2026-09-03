// CryInput.cpp : Defines the entry point for the DLL application.
//

#include "StdAfx.h"

#ifndef _XBOX 
_ACCESS_POOL;
#endif //_XBOX

#include <stdio.h>
#include <ILog.h>
#include <IInput.h>
#include "Input.h"

#ifdef _DEBUG
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
//////////////////////////////////////////////////////////////////////////


// [webport] DllMain is the Win32 DLL entry point. There are no DLLs on Linux
// and none at all in wasm, where every module links into one unit.
#if !defined(_XBOX) && !defined(LINUX)
BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    return TRUE;
}
#endif //_XBOX

IInput *CreateInput( ISystem *pSystem,void* hinst, void* hwnd, bool usedinput)
{
	gISystem = pSystem;
	CInput *pInput=new CInput;
	if (!pInput->Init(pSystem,(HINSTANCE)hinst,(HWND)hwnd,usedinput))
	{
		delete pInput;
		return NULL;
	}
	return pInput;
}

#include <CrtDebugStats.h>
