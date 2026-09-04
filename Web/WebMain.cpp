////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WebMain.cpp
//  Description: The browser host -- CryEngine driven from the page's event
//               loop, rendering through WebGL2.
//
//  WHY THIS IS NOT Headless/HeadlessMain.cpp WITH A LOOP ADDED
//
//  Headless starts the engine, proves every subsystem comes up, and releases.
//  It never runs a frame, and that is the only reason it works: the engine's
//  frame loop is a blocking while(!quit), and a browser cannot have one.
//
//  A page has a single thread that also services layout, input and
//  compositing. Code that does not return to the event loop starves all of
//  them, and -- the part that surprises people -- nothing is ever displayed,
//  because the drawing buffer is presented when the task that drew it YIELDS.
//  There is no SwapBuffers to call. A renderer can issue a perfect frame and
//  the user will see a blank canvas for as long as the loop holds the thread.
//
//  So the loop is inverted: one frame becomes one callback, and
//  emscripten_set_main_loop asks the browser to call it before each repaint.
//  requestAnimationFrame is what that compiles to, which also means frames are
//  paced by the display and pause entirely in a background tab -- both of
//  which the engine's own timing is happy with, since it works from measured
//  deltas.
//
//  The alternative is Asyncify, which rewrites the wasm so a blocking loop can
//  yield mid-call and keeps while(!quit) intact. It costs a large size and
//  speed penalty across the whole module to avoid restructuring one function,
//  and it is not needed here: nothing in this file wants to block.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include <ISystem.h>
#include <ILog.h>
#include <IConsole.h>
#include <IRenderer.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "CryHostLog.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

//////////////////////////////////////////////////////////////////////////

struct SWebHost
{
	ISystem*		pSystem;
	IRenderer*		pRenderer;
	CCryHostLog*	pLog;
	unsigned		nFrame;
	bool			bQuit;
};

static SWebHost	g_host;
static CCryHostLog g_log;

//! The engine calls this while it is busy so a host can keep a window
//! responsive. In a browser there is nothing useful to do from inside a
//! blocking call -- the event loop is exactly what we cannot reach from here --
//! so it stays empty rather than pretending.
static void WebCheckFunc(void*)
{
}

//////////////////////////////////////////////////////////////////////////
//! One frame.
//!
//! The ordering is CGame::Run's: BeginFrame, then the world, then Update to
//! finish the frame. ISystem::Update returns false when the engine wants to
//! quit.
//!
//! Nothing draws geometry yet, so what reaches the canvas is the clear.
//! That is not a placeholder for the loop's sake -- it is the honest state of
//! the renderer, and it is visible proof that the loop, the context and the
//! present path all work, which is what this milestone is for.
//////////////////////////////////////////////////////////////////////////
static void WebFrame(void*)
{
	if (g_host.bQuit || !g_host.pSystem)
		return;

	IRenderer* pRenderer = g_host.pSystem->GetIRenderer();
	if (!pRenderer)
	{
		// Without a renderer there is nothing this loop can do, and spinning
		// once per frame forever would hide that. Stop and say so.
		printf("[web] no renderer; stopping the frame loop\n");
		g_host.bQuit = true;
		emscripten_cancel_main_loop();
		return;
	}

	pRenderer->BeginFrame();

	// ISystem::Update drives the timer, console, streaming, input and the
	// script system. It returns false when something has asked to quit.
	if (!g_host.pSystem->Update(0, 0))
	{
		printf("[web] engine requested quit after %u frames\n", g_host.nFrame);
		g_host.bQuit = true;
		emscripten_cancel_main_loop();
		return;
	}

	pRenderer->Update();

	++g_host.nFrame;

	// Publish progress where a page -- and the browser test -- can see it.
	// Reading a wasm global from JavaScript means knowing its address; a
	// global on window is what a test harness can actually check.
	EM_ASM({ window.__cryFrame = $0; }, g_host.nFrame);
}

//////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
	printf("CryEngine 1.33 web port -- browser host\n");
	printf("Renderer: XRenderGLES (WebGL2)\n\n");

	memset(&g_host, 0, sizeof(g_host));
	g_host.pLog = &g_log;

	SSystemInitParams params;
	memset(&params, 0, sizeof(params));

	params.pLog          = &g_log;
	params.sLogFileName  = "web.log";
	params.pCheckFunc    = WebCheckFunc;

	// NOT a dedicated server. CSystem::InitRenderer forces r_Driver to "NULL"
	// when this is set, which would load the null renderer and draw nothing --
	// the exact thing this host exists to move past.
	params.bDedicatedServer = false;

	// Assembled the same way Headless does it, so both hosts accept the same
	// switches. szSystemCmdLine is a fixed array inside the struct, not a
	// pointer, so it is filled rather than pointed at.
	params.szSystemCmdLine[0] = 0;
	for (int i = 1; i < argc; ++i)
	{
		strncat(params.szSystemCmdLine, argv[i],
		        sizeof(params.szSystemCmdLine) - strlen(params.szSystemCmdLine) - 2);
		strncat(params.szSystemCmdLine, " ", 1);
	}

	printf("Calling CreateSystemInterface...\n");
	g_host.pSystem = CreateSystemInterface(params);

	if (!g_host.pSystem)
	{
		printf("FAILED: CreateSystemInterface returned NULL\n");
		EM_ASM({ window.__cryFailed = true; window.__cryTestDone = true; });
		return 1;
	}

	g_host.pRenderer = g_host.pSystem->GetIRenderer();
	printf("\nSystem interface created. Renderer: %s\n",
	       g_host.pRenderer ? "present" : "MISSING");

	EM_ASM({
		window.__cryStarted  = true;
		window.__cryRenderer = $0;
	}, g_host.pRenderer ? 1 : 0);

	// Hand the loop to the browser and return. Passing 0 for the frame rate
	// asks for requestAnimationFrame, which paces to the display rather than
	// to a timer.
	//
	// simulate_infinite_loop is 0 deliberately: with it set, Emscripten throws
	// to unwind out of main and the page sees an uncaught exception, which is
	// indistinguishable from a crash in a test harness. Returning normally is
	// fine because the runtime is kept alive below.
	emscripten_set_main_loop_arg(WebFrame, 0, /*fps*/ 0, /*simulate_infinite_loop*/ 0);

	// main() returning would otherwise tear the runtime down and take the
	// loop with it -- this build links with EXIT_RUNTIME=1 for the headless
	// host's sake.
	emscripten_exit_with_live_runtime();
	return 0;
}
