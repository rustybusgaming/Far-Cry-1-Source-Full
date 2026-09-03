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

//! Every varargs entry point does the same three lines; the macro keeps the
//! table above readable and makes it obvious that none of them differ.
#define FORWARD(type) \
	va_list a; va_start(a, format); LogV(type, format, a); va_end(a)

//////////////////////////////////////////////////////////////////////////
//! Minimal ILog. CSystem creates its own once it is up, but initialisation
//! can fail before that happens -- and that is exactly when the output
//! matters -- so one is supplied from the start.
//!
//! IMiniLog says "you only have to implement this function" of LogV, and that
//! is taken literally here: every other entry point funnels into it. The
//! engine encodes verbosity as a leading control character in the format
//! string ("\003 message" is verbosity 3), so LogV decodes and filters on
//! that the way CSystem's own log does -- otherwise -verbosity would be
//! silently ignored and startup would be unreadably noisy.
//////////////////////////////////////////////////////////////////////////
class CHeadlessLog : public ILog
{
public:
	CHeadlessLog() : m_nVerbosity(3), m_bVerbosityEnabled(true) {}

	//! The one function IMiniLog actually requires.
	virtual void LogV(const ELogType nType, const char* szFormat, va_list args)
	{
		if (!szFormat)
			return;

		// Decode the leading verbosity marker, if any: "\003 text" is a
		// verbosity-3 message and the marker is not part of the text.
		int nLevel = 0;
		const char* szText = szFormat;
		if ((unsigned char)szText[0] >= 1 && (unsigned char)szText[0] <= 9)
		{
			nLevel = (int)szText[0];
			++szText;
		}

		if (m_bVerbosityEnabled && nLevel > m_nVerbosity)
			return;

		FILE* out = (nType == eError || nType == eErrorAlways) ? stderr : stdout;

		switch (nType)
		{
		case eWarning:
		case eWarningAlways:	fputs("[warn]  ", out); break;
		case eError:
		case eErrorAlways:		fputs("[error] ", out); break;
		default:				break;
		}

		vfprintf(out, szText, args);
		fputc('\n', out);
		fflush(out);
	}

	virtual void Release() {}

	//! There is no log file: everything goes to the terminal, and under
	//! Emscripten that is the browser console. Reported honestly rather than
	//! pretending a file exists.
	virtual void SetFileName(const char* /*command*/ = NULL) {}
	virtual const char* GetFileName() { return "<stdout>"; }

	virtual void Log(const char* format, ...)					{ FORWARD(eMessage); }
	virtual void LogWarning(const char* format, ...)			{ FORWARD(eWarning); }
	virtual void LogError(const char* format, ...)				{ FORWARD(eError); }
	virtual void LogPlus(const char* format, ...)				{ FORWARD(eMessage); }
	virtual void LogToFile(const char* format, ...)				{ FORWARD(eMessage); }
	virtual void LogToFilePlus(const char* format, ...)			{ FORWARD(eMessage); }
	virtual void LogToConsole(const char* format, ...)			{ FORWARD(eMessage); }
	virtual void LogToConsolePlus(const char* format, ...)		{ FORWARD(eMessage); }
	virtual void UpdateLoadingScreen(const char* format, ...)	{ FORWARD(eMessage); }
	virtual void UpdateLoadingScreenPlus(const char* format, ...){ FORWARD(eMessage); }

	virtual void EnableVerbosity(bool bEnable)	{ m_bVerbosityEnabled = bEnable; }
	virtual void SetVerbosity(int verbosity)	{ m_nVerbosity = verbosity; }
	virtual int  GetVerbosityLevel()			{ return m_nVerbosity; }

private:
	int  m_nVerbosity;
	bool m_bVerbosityEnabled;
};

#undef FORWARD

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

	CHeadlessLog log;

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
