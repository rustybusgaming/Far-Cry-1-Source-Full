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
ISystem	*GetISystem()
{
	return gISystem;
}

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