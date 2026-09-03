////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   SyncStreamEngine.h
//  Description: A synchronous IStreamEngine, for the headless and web builds.
//
//  WHY THIS EXISTS
//
//  CStreamEngine is a typedef for CRefStreamEngine, which is built on Win32
//  OVERLAPPED I/O: CreateFile(FILE_FLAG_OVERLAPPED) + ReadFileEx + alertable
//  SleepEx waits + CancelIo, driven by worker threads. None of that has a
//  POSIX equivalent with the same shape, and in a browser there is no
//  synchronous file API at all -- so it is excluded from this build (see
//  tools/triage.py --excluded).
//
//  But the engine cannot load anything without a stream engine, so excluding
//  it leaves a hole rather than a clean gap. This fills it the honest way: by
//  satisfying the same interface SYNCHRONOUSLY.
//
//  WHY SYNCHRONOUS IS A LEGITIMATE IMPLEMENTATION, NOT A STUB
//
//  IStreamEngine's contract is "start a read, learn later that it finished".
//  Nothing in it promises the read is still outstanding when StartRead
//  returns. Completing it inline and reporting the stream as already finished
//  satisfies every caller: IsFinished() is true, GetBuffer() has the data, and
//  the completion callback has already fired.
//
//  What is lost is overlap, not correctness. The engine will block while
//  loading instead of continuing to render, so level loads stall rather than
//  stream in. That is the right trade for a first headless build -- it is
//  correct and simple -- and it is a poor one for the browser, where blocking
//  the main thread freezes the page.
//
//  THE WEB PATH FROM HERE
//
//  Under Emscripten this class is the seam to replace, not to extend. A
//  browser cannot read a file synchronously on the main thread at all, so the
//  real implementation is one of:
//
//    * a worker thread with a synchronous XHR or the File System Access API,
//      which keeps this exact interface and restores the overlap; or
//    * an async fetch pipeline, which needs StartRead to genuinely defer and
//      the engine's loading code to tolerate a truly pending read.
//
//  Preloading the pak files into MEMFS makes reads synchronous again and would
//  let this class work as-is, at the cost of holding several GB in memory --
//  which is why it is not the plan.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_SYNC_STREAM_ENGINE_H_
#define _CRY_WEBPORT_SYNC_STREAM_ENGINE_H_

#include <platform.h>
#include <IStreamEngine.h>
#include <vector>

class CCryPak;
struct IMiniLog;

//////////////////////////////////////////////////////////////////////////
//! One completed read. Constructed already-finished: by the time any caller
//! sees it, the data is in the buffer or the error flag is set.
//////////////////////////////////////////////////////////////////////////
class CSyncReadStream : public IReadStream
{
public:
	CSyncReadStream(DWORD_PTR dwUserData);
	virtual ~CSyncReadStream();

	//! Perform the read. Returns false and marks the stream errored on failure.
	bool ReadFile(CCryPak* pPak, const char* szFile,
	              unsigned nOffset, unsigned nSize, void* pExternalBuffer);

	// --- IReadStream
	virtual bool IsError()      { return m_bError; }
	virtual bool IsFinished()   { return true; }   // always: we never defer
	virtual unsigned int GetBytesRead(bool /*bWait*/ = false) { return m_nBytesRead; }
	virtual const void* GetBuffer() { return m_pBuffer; }
	virtual void Abort()        {}                 // nothing is ever pending
	virtual void RaisePriority(int) {}             // no queue to reorder
	virtual DWORD_PTR GetUserData() { return m_dwUserData; }
	virtual void Wait()         {}                 // already complete

private:
	void*     m_pBuffer;
	bool      m_bOwnBuffer;   //!< false when the caller supplied the buffer
	unsigned  m_nBytesRead;
	bool      m_bError;
	DWORD_PTR m_dwUserData;
};

//////////////////////////////////////////////////////////////////////////
class CSyncStreamEngine : public IStreamEngine
{
public:
	CSyncStreamEngine(CCryPak* pPak, IMiniLog* pLog);
	virtual ~CSyncStreamEngine();

	// --- IStreamEngine
	virtual IReadStreamPtr StartRead(const char* szSource, const char* szFile,
	                                 IStreamCallback* pCallback = NULL,
	                                 StreamReadParams* pParams = NULL);
	virtual unsigned GetFileSize(const char* szFile, unsigned nCryPakFlags = 0);
	virtual void Update(unsigned nFlags = 0);
	virtual unsigned Wait(unsigned nMilliseconds, unsigned nFlags = 0);
	virtual void GetMemoryStatistics(ICrySizer* pSizer);
	virtual DWORD GetStreamCompressionMask() const { return m_dwCompressionMask; }

	// --- the two setters CSystem calls directly on the concrete type
	void SetCallbackTimeQuota(int nMicroseconds);
	void SetStreamCompressionMask(const DWORD indwMask);

private:
	CCryPak*   m_pPak;
	IMiniLog*  m_pLog;
	DWORD      m_dwCompressionMask;
	unsigned   m_nReadsCompleted;   //!< diagnostics only
};

#endif // _CRY_WEBPORT_SYNC_STREAM_ENGINE_H_
