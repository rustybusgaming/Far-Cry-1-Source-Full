//////////////////////////////////////////////////////////////////////////
// Declaration of CStreamEngine: implementation of IStreamEngine interface
// 
#ifndef _CRY_SYSTEM_STREAM_ENGINE_HDR_
#define _CRY_SYSTEM_STREAM_ENGINE_HDR_

#include "RefStreamEngine.h"
// This is reference implementation
// [webport] CRefStreamEngine is built on Win32 overlapped I/O and is excluded
// from this build. CSyncStreamEngine satisfies the same IStreamEngine
// interface synchronously -- see SyncStreamEngine.h for why that is a
// legitimate implementation rather than a stub, and what the web build has to
// replace it with.
#if defined(LINUX)
#include "SyncStreamEngine.h"
typedef CSyncStreamEngine CStreamEngine;
#else
typedef CRefStreamEngine CStreamEngine;
#endif

#endif