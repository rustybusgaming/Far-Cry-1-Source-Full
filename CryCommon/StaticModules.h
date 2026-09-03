#ifndef CRYSTATICMODULES_H__
#define CRYSTATICMODULES_H__

/*!
	StaticModules -- resolving CryEngine's module factories without a dynamic loader.

	WHY THIS EXISTS

	CryEngine ships one shared library per module and wires them together at
	runtime: CSystem::LoadDLL() opens CryScriptSystem.dll, asks it for the
	symbol "CreateScriptSystem", and calls it. That indirection is the whole
	reason a module boundary exists at all in this engine.

	WebAssembly has no equivalent. Emscripten's dlopen support requires the
	side modules to be built as separate wasm binaries with a shared memory
	layout, loaded asynchronously -- which cannot satisfy a synchronous
	CryLoadLibrary() call in the middle of engine startup. The whole engine
	must be one link unit.

	Once everything is in one link unit, the factories are ordinary symbols
	the linker has already resolved. This file replaces the *lookup* with a
	compiled-in table while leaving every call site in CrySystem untouched:
	SystemInit.cpp still does LoadDLL() then CryGetProcAddress(), and still
	handles a missing module or a missing entry point exactly as before.

	WHAT THIS IS NOT

	It is not a stub. Every function the table returns is the module's real
	factory, compiled from Crytek's own source. The only thing that changed
	is how its address is found.

	A module may legitimately be *known but export nothing* -- see the
	CryAISystem entry in StaticModules.cpp for the one case in this tree.
*/

#ifdef __cplusplus
extern "C" {
#endif

//! Resolve a module by the name CSystem::LoadDLL() was given.
//!
//! Matching ignores directory, extension and case, so "cryscriptsystem.so",
//! "CryScriptSystem.dll" and "CryScriptSystem" all name the same module --
//! the engine spells these inconsistently across platforms and call sites.
//!
//! Returns an opaque non-NULL handle, or NULL if the name is not a module of
//! this engine. NULL is the correct answer for the Windows system libraries
//! some code paths still ask for (ddraw.dll, kernel32.dll, VTuneApi.dll);
//! those call sites already test the result.
void* CryStaticLoadModule(const char* szLibName);

//! Look up an exported factory in a handle returned by CryStaticLoadModule().
//! Returns NULL if this module does not export that name.
void* CryStaticGetProcAddress(void* hModule, const char* szProcName);

//! Present so the CryFreeLibrary() call sites keep compiling. There is
//! nothing to unload: the code is part of the executable.
int CryStaticFreeModule(void* hModule);

#ifdef __cplusplus
}
#endif

#endif //CRYSTATICMODULES_H__
