////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port -- synchronous stream engine. See SyncStreamEngine.h
//  for why this exists and what it deliberately does not do.
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "SyncStreamEngine.h"
#include "CryPak.h"

#include <ICryPak.h>
#include <IMiniLog.h>
#include <CrySizer.h>

#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
// CSyncReadStream
//////////////////////////////////////////////////////////////////////////
CSyncReadStream::CSyncReadStream(DWORD_PTR dwUserData)
	: m_pBuffer(NULL), m_bOwnBuffer(false), m_nBytesRead(0),
	  m_bError(false), m_dwUserData(dwUserData)
{
}

CSyncReadStream::~CSyncReadStream()
{
	// Only free what we allocated. When the caller supplied pBuffer via
	// StreamReadParams it owns that memory and may well reuse it.
	if (m_bOwnBuffer && m_pBuffer)
		free(m_pBuffer);
}

bool CSyncReadStream::ReadFile(CCryPak* pPak, const char* szFile,
                               unsigned nOffset, unsigned nSize,
                               void* pExternalBuffer)
{
	m_bError = true;          // pessimistic: cleared only on a complete read
	if (!pPak || !szFile)
		return false;

	// Reads go through CryPak, not fopen: most assets live inside .pak
	// archives and are not visible on the filesystem at all.
	FILE* f = pPak->FOpen(szFile, "rb", 0u);   // CCryPak drops the interface default
	if (!f)
		return false;

	pPak->FSeek(f, 0, SEEK_END);
	const long nFileSize = pPak->FTell(f);
	if (nFileSize < 0) { pPak->FClose(f); return false; }

	if (nOffset > (unsigned)nFileSize) { pPak->FClose(f); return false; }

	// nSize == 0 means "to the end of the file", which is how the engine asks
	// for a whole asset.
	unsigned nToRead = nSize ? nSize : (unsigned)(nFileSize - nOffset);
	if (nOffset + nToRead > (unsigned)nFileSize)
		nToRead = (unsigned)nFileSize - nOffset;

	if (pPak->FSeek(f, (long)nOffset, SEEK_SET) != 0) { pPak->FClose(f); return false; }

	if (pExternalBuffer)
	{
		m_pBuffer    = pExternalBuffer;
		m_bOwnBuffer = false;
	}
	else
	{
		// One extra byte, always zeroed: a good deal of the engine's loading
		// code treats a streamed buffer as a C string (shader and script
		// sources above all) and would run off the end without it.
		m_pBuffer = malloc(nToRead + 1);
		if (!m_pBuffer) { pPak->FClose(f); return false; }
		m_bOwnBuffer = true;
		((char*)m_pBuffer)[nToRead] = 0;
	}

	const size_t nRead = pPak->FRead(m_pBuffer, 1, nToRead, f);
	pPak->FClose(f);

	m_nBytesRead = (unsigned)nRead;
	m_bError = (nRead != nToRead);
	return !m_bError;
}

//////////////////////////////////////////////////////////////////////////
// CSyncStreamEngine
//////////////////////////////////////////////////////////////////////////
CSyncStreamEngine::CSyncStreamEngine(CCryPak* pPak, IMiniLog* pLog)
	: m_pPak(pPak), m_pLog(pLog), m_dwCompressionMask(0), m_nReadsCompleted(0)
{
	if (m_pLog)
		m_pLog->Log("StreamEngine: synchronous (no overlapped I/O on this platform)");
}

CSyncStreamEngine::~CSyncStreamEngine()
{
}

IReadStreamPtr CSyncStreamEngine::StartRead(const char* /*szSource*/,
                                            const char* szFile,
                                            IStreamCallback* pCallback,
                                            StreamReadParams* pParams)
{
	const DWORD_PTR dwUserData = pParams ? pParams->dwUserData : 0;
	const unsigned  nOffset    = pParams ? pParams->nOffset    : 0;
	const unsigned  nSize      = pParams ? pParams->nSize      : 0;
	void*           pBuffer    = pParams ? pParams->pBuffer    : NULL;

	CSyncReadStream* pStream = new CSyncReadStream(dwUserData);
	IReadStreamPtr ptr(pStream);   // reference-counted from here on

	const bool bOk = pStream->ReadFile(m_pPak, szFile, nOffset, nSize, pBuffer);
	if (bOk)
		++m_nReadsCompleted;
	else if (m_pLog)
		m_pLog->LogWarning("StreamEngine: failed to read '%s'", szFile ? szFile : "(null)");

	// The read is already done, so the completion callback fires here rather
	// than from a later Update(). Callers that only poll IsFinished() are
	// equally happy: it is true from the moment they get the pointer.
	//
	// It is invoked AFTER ptr has taken its reference, so a callback that
	// releases the stream cannot destroy it mid-call.
	if (pCallback)
	{
		pCallback->StreamOnComplete(pStream, bOk ? 0 : 1u);
	}

	return ptr;
}

unsigned CSyncStreamEngine::GetFileSize(const char* szFile, unsigned /*nCryPakFlags*/)
{
	if (!m_pPak || !szFile)
		return 0;

	FILE* f = m_pPak->FOpen(szFile, "rb", 0u);
	if (!f)
		return 0;

	m_pPak->FSeek(f, 0, SEEK_END);
	const long nSize = m_pPak->FTell(f);
	m_pPak->FClose(f);

	return nSize > 0 ? (unsigned)nSize : 0;
}

void CSyncStreamEngine::Update(unsigned /*nFlags*/)
{
	// Nothing is ever outstanding -- every read completed inside StartRead.
}

unsigned CSyncStreamEngine::Wait(unsigned /*nMilliseconds*/, unsigned /*nFlags*/)
{
	// Returns immediately, and correctly: there is nothing to wait for.
	return 0;
}

void CSyncStreamEngine::GetMemoryStatistics(ICrySizer* pSizer)
{
	if (pSizer)
		pSizer->AddObject(this, sizeof(*this));
}

void CSyncStreamEngine::SetCallbackTimeQuota(int /*nMicroseconds*/)
{
	// The quota bounds how long callbacks may run inside Update(). Callbacks
	// here fire from StartRead instead, so there is no budget to enforce --
	// and pretending to enforce one would only hide that.
}

void CSyncStreamEngine::SetStreamCompressionMask(const DWORD indwMask)
{
	m_dwCompressionMask = indwMask;
}
