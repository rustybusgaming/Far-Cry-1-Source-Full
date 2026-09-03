////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   HTTPDownloaderStub.cpp
//  Description: CHTTPDownloader for builds without WinInet.
//
//  HTTPDownloader.cpp is built on WinInet (InternetOpen / InternetOpenUrl /
//  InternetReadFile) driven by a worker thread. It is excluded from this build
//  because WinInet has no POSIX equivalent, and on the web the whole approach
//  is wrong anyway -- a browser cannot open arbitrary cross-origin URLs, and
//  the natural replacement is fetch(), which is asynchronous and CORS-bound.
//
//  CDownloadManager still constructs one, so the symbols have to exist. The
//  important property of this file is that it FAILS VISIBLY: Create() returns
//  failure and logs a warning naming the missing capability. A downloader that
//  silently reported success and produced no bytes would surface much later as
//  an unexplained missing file.
//
//  Replacing this properly is Milestone 3 work, alongside the WebTransport
//  seam in CryNetwork: an async fetch pipeline whose completion is delivered
//  through the same callbacks, plus a decision about which origins the build
//  is allowed to reach.
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "HTTPDownloader.h"
#include "DownloadManager.h"

#include <ILog.h>

CHTTPDownloader::CHTTPDownloader()
{
	// The real constructor sets up the WinInet handles and the worker thread.
	// Members are left as the class initialises them; nothing here starts.
}

// The destructor is virtual, and a class's vtable is emitted in the
// translation unit that defines its first non-inline virtual function. Without
// this definition the vtable is never emitted and the link fails on it alone,
// even with every other method present.
CHTTPDownloader::~CHTTPDownloader()
{
}

int CHTTPDownloader::Create(ISystem* pISystem, CDownloadManager* /*pParent*/)
{
	if (pISystem && pISystem->GetILog())
	{
		pISystem->GetILog()->LogWarning(
			"HTTP downloader unavailable: this build has no WinInet, and the web "
			"replacement (fetch) is not implemented yet. Downloads will not run.");
	}
	return 0;   // failure -- callers already handle a downloader that cannot start
}

void CHTTPDownloader::Release()
{
	delete this;
}

void CHTTPDownloader::OnError()    {}
void CHTTPDownloader::OnComplete() {}
void CHTTPDownloader::OnCancel()   {}
