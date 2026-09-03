// CrySoundSystem.cpp : Defines the entry point for the DLL application.
//

#include "StdAfx.h"

#ifdef USING_CRY_MEMORY_MANAGER
#ifndef _XBOX
_ACCESS_POOL;
#endif //_XBOX
#else //USING_CRY_MEMORY_MANAGER
//! Structre filled by call to CryModuleGetMemoryInfo()
struct CryModuleMemoryInfo
{
	//! Total Ammount of memory allocated.
	int allocated;
	//! Total Ammount of memory freed.
	int freed;
	//! Total number of memory allocations.
	int num_allocations;
};
// [webport] __declspec(dllexport) marks a Win32 DLL export. ELF expresses
// symbol visibility differently, and in wasm every module links into one unit,
// so there is nothing to export from.
#if defined(LINUX)
extern "C" void CryModuleGetMemoryInfo( CryModuleMemoryInfo *pMemInfo )
#else
extern "C" __declspec(dllexport) void CryModuleGetMemoryInfo( CryModuleMemoryInfo *pMemInfo )
#endif
{
// [webport] The size_t branch is wrong on any 64-bit target: crysound.h
// declares
//     CS_GetMemoryStats(unsigned int *currentalloced, unsigned int *maxalloced)
// so passing size_t* (8 bytes on LP64) hands it pointers to objects twice the
// width it will write, leaving the upper half uninitialised.
//
// It went unnoticed because the engine only ever built this 32-bit, where
// size_t and unsigned int are the same type. unsigned int matches the declared
// signature on every platform, so the conditional serves no purpose.
  unsigned int nCurrentAlloced;
	unsigned int nMaxAlloced;
  CS_GetMemoryStats(&nCurrentAlloced, &nMaxAlloced);
	pMemInfo->allocated = nMaxAlloced;
	pMemInfo->freed = 0;
	pMemInfo->num_allocations = 0;
};
#endif //USING_CRY_MEMORY_MANAGER

#include "DummySound.h"	
#include <Cry_Camera.h>
#include "SoundSystem.h"
//////////////////////////////////////////////////////////////////////////////////////////////
// dll interface	

//////////////////////////////////////////////////////////////////////////
// Pointer to Global ISystem.
static ISystem* gISystem = 0;
ISystem* GetISystem()
{
	return gISystem;
}
//////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////
extern "C" ISoundSystem* CreateSoundSystem(struct ISystem* pISystem, void* pInitData)
{
	gISystem = pISystem;
	//create a brand new sound system
#ifndef _XBOX
	CSoundSystem* pSoundSystem = new CSoundSystem(pISystem, (HWND)pInitData);

	if (!pSoundSystem || !pSoundSystem->IsOK())
	{
		//if the sound system cannot be created or initialized,
		//create the dummy sound system (NULL sound system, same as for
		//dedicated server)

		if (pSoundSystem)
			pSoundSystem->Release();
#endif		

		CDummySoundSystem *pDummySound=new CDummySoundSystem(pISystem, (HWND)pInitData);
		return pDummySound;

#ifndef _XBOX		
	}

	return pSoundSystem;
#endif
}

#ifndef __MWERKS__
// [webport] DllMain is the Win32 DLL entry point; no DLLs on Linux, none at
// all in wasm.
#if !defined(_XBOX) && !defined(LINUX)
///////////////////////////////////////////////
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call,  LPVOID lpReserved)
{
    return TRUE;
}
#endif //_XBOX
#endif

#include <CrtDebugStats.h>
