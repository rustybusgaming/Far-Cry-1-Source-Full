#ifndef _CRY_HOST_LOG_H_
#define _CRY_HOST_LOG_H_

/*!
	CCryHostLog -- the ILog a host supplies before the engine has its own.

	Every host that starts CryEngine needs one of these, and needs it BEFORE
	CreateSystemInterface: CSystem builds its own logger during Init, so
	anything that goes wrong before that point has nowhere to go unless the
	host passed a log in. That is exactly the window where the output matters
	most.

	It lives here rather than in one of the hosts because there are two of
	them -- Headless/ and Web/ -- and a second copy would be a second thing to
	keep correct. Its output goes to stdout and stderr, which in a browser is
	the developer console.
*/

#include <ILog.h>

#include <stdio.h>
#include <stdarg.h>


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
class CCryHostLog : public ILog
{
public:
	CCryHostLog() : m_nVerbosity(3), m_bVerbosityEnabled(true) {}

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

#endif //_CRY_HOST_LOG_H_
