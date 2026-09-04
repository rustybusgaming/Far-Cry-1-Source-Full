////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   HeadlessMain.cpp
//  Description: Entry point for the headless engine build.
//
//  WHAT THIS IS FOR
//
//  Everything up to this point has been a COMPILE milestone: 547 translation
//  units that build but were never linked together, so nothing proved the
//  pieces actually fit. This links them and runs the engine with Crytek's own
//  null renderer -- no GL, no D3D, no Cg, no shader rewrite.
//
//  That makes it the first executable in the port, and the first thing that
//  can fail at RUNTIME rather than at compile time. It is deliberately small:
//  its job is to start CrySystem and report honestly how far it got, so that
//  the next failure is always visible.
//
//  It is also the shape the web build takes. Under Emscripten this same
//  entry point runs, with the loop handed to emscripten_set_main_loop instead
//  of spinning here -- see the note at the bottom of main().
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <ISystem.h>
#include <ILog.h>
#include <IConsole.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// The host-side ILog, shared with the browser host. See Hosts/CryHostLog.h.
#include "CryHostLog.h"

//////////////////////////////////////////////////////////////////////////
//! The engine requires pCheckFunc to be non-null -- it is the copy-protection
//! hook, called during startup. There is nothing to check here.
//////////////////////////////////////////////////////////////////////////
static void HeadlessCheckFunc(void*)
{
}

int main(int argc, char** argv)
{
	printf("CryEngine 1.33 web port -- headless build\n");
	printf("Renderer: XRenderNULL (no rasteriser)\n\n");

	CCryHostLog log;

	SSystemInitParams params;          // has a default constructor; use it
	params.pLog             = &log;
	params.sLogFileName     = "headless.log";
	params.bDedicatedServer = true;    // no window, no renderer, no input
	params.bEditor          = false;
	params.bPreview         = false;
	params.bTestMode        = false;
	params.pCheckFunc       = HeadlessCheckFunc;

	// The engine parses this itself for early commands, so it must be a valid
	// string even when empty -- several code paths construct a std::string
	// from it without checking.
	params.szSystemCmdLine[0] = 0;
	for (int i = 1; i < argc; ++i)
	{
		strncat(params.szSystemCmdLine, argv[i],
		        sizeof(params.szSystemCmdLine) - strlen(params.szSystemCmdLine) - 2);
		strncat(params.szSystemCmdLine, " ", 1);
	}

	printf("Calling CreateSystemInterface...\n");
	ISystem* pSystem = CreateSystemInterface(params);

	if (!pSystem)
	{
		fprintf(stderr, "\nFAILED: CreateSystemInterface returned NULL.\n");
		return 1;
	}

	printf("\nSystem interface created.\n");

	// A real headless server would pump here:
	//
	//     while (!quit) { pSystem->Update(); }
	//
	// That loop is exactly what cannot be written for the browser, where
	// returning to the event loop is the only way anything happens. Under
	// Emscripten this becomes emscripten_set_main_loop(Frame, 0, 1) with
	// Frame() containing one Update(). It is left out here because reaching
	// this point at all is the milestone; pumping needs the module factories
	// wired up first (see WEBPORT.md).

	pSystem->Release();
	printf("System released cleanly.\n");
	return 0;
}
