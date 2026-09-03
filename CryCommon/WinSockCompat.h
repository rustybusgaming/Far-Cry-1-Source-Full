////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WinSockCompat.h
//  Description: Winsock -> BSD sockets, for the LINUX and wasm builds.
//
//  WHY THIS ONE IS A SHIM AND DirectInput WAS NOT
//
//  Winsock is BSD sockets with a different spelling. socket(), bind(),
//  sendto(), recvfrom() and setsockopt() are the same calls with the same
//  semantics; what differs is a handful of names and three genuine behavioural
//  details, all of which are handled below. That makes a compatibility layer
//  the honest choice here, where for DirectInput -- a COM API with no
//  counterpart at all -- it would have been a fiction.
//
//  The three real differences:
//
//    1. Error reporting. Winsock returns errors through WSAGetLastError();
//       BSD sockets use errno. The WSAE* constants are already mapped onto
//       their errno equivalents in LinuxSpecific.h.
//
//    2. Socket lifetime. closesocket() is close(); ioctlsocket(FIONBIO) is
//       fcntl(O_NONBLOCK). A SOCKET is an int, not an opaque HANDLE, so
//       INVALID_SOCKET is -1 rather than ~0 -- and the difference matters,
//       because "sock < 0" and "sock == INVALID_SOCKET" are both used in this
//       codebase and only agree when INVALID_SOCKET is -1.
//
//    3. Startup. WSAStartup()/WSACleanup() initialise the Winsock DLL. There
//       is nothing to initialise on POSIX, so they succeed and do nothing.
//
//  ===================================================================
//  WASM: THERE ARE NO UDP SOCKETS IN A BROWSER
//  ===================================================================
//
//  This header makes the code COMPILE and makes it WORK on a native Linux
//  build. It does not, and cannot, make it work in a browser.
//
//  Far Cry's netcode is built on UDP datagrams (SOCK_DGRAM throughout
//  CryNetwork/DatagramSocket.cpp). A browser cannot open a UDP socket, or a
//  raw TCP socket, at all -- no API exposes one, for good reason. Under
//  Emscripten the BSD socket calls below are emulated over WebSockets, which
//  changes the delivery contract fundamentally:
//
//      UDP        unreliable, unordered, message-oriented, no connection
//      WebSocket  reliable, ORDERED, message-oriented, connection-based
//
//  Reliability and message framing are fine -- gaining them costs nothing.
//  Ordering is not. Game netcode built on UDP assumes a dropped or delayed
//  packet is skipped; over an ordered transport it instead blocks everything
//  behind it (head-of-line blocking), so one lost packet stalls the whole
//  stream until it is retransmitted. Under packet loss that turns a brief
//  glitch into a visible freeze.
//
//  It also needs a relay: Emscripten's WebSocket emulation talks to a proxy
//  that terminates the WebSocket and forwards real UDP to the game server.
//  Nothing in this repository provides one.
//
//  The transport that actually matches UDP is a WebRTC DataChannel in
//  unreliable/unordered mode ({ordered:false, maxRetransmits:0}), which is
//  genuinely datagram-like. It costs a signalling server and an ICE exchange.
//  See CryNetwork/WebTransport.h for the seam that makes that swap possible.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_WINSOCK_COMPAT_H_
#define _CRY_WEBPORT_WINSOCK_COMPAT_H_

#if !defined(LINUX)
#	error "WinSockCompat.h is the POSIX path; on Windows use <winsock2.h>"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
// Types
//
// SOCKET, INVALID_SOCKET and SOCKET_ERROR are already defined by
// LinuxSpecific.h (as int, -1 and -1); they are not redefined here.
//////////////////////////////////////////////////////////////////////////
typedef struct sockaddr     SOCKADDR;
typedef struct sockaddr*    LPSOCKADDR;
typedef struct sockaddr_in  SOCKADDR_IN;
typedef struct hostent      HOSTENT;
typedef struct hostent*     LPHOSTENT;

#ifndef WSADESCRIPTION_LEN
#	define WSADESCRIPTION_LEN 256
#	define WSASYS_STATUS_LEN  128
#endif

typedef struct WSAData {
	WORD  wVersion;
	WORD  wHighVersion;
	char  szDescription[WSADESCRIPTION_LEN + 1];
	char  szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char* lpVendorInfo;
} WSADATA, *LPWSADATA;

#ifndef MAKEWORD
#	define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))
#endif

//////////////////////////////////////////////////////////////////////////
// Startup and shutdown
//
// Winsock requires the DLL be initialised before any socket call and
// unloaded afterwards. POSIX has no such state, so both succeed trivially.
// The WSADATA block is still filled in: callers read wVersion from it.
//////////////////////////////////////////////////////////////////////////
inline int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
{
	if (lpWSAData)
	{
		memset(lpWSAData, 0, sizeof(*lpWSAData));
		lpWSAData->wVersion     = wVersionRequested;
		lpWSAData->wHighVersion = wVersionRequested;
		strcpy(lpWSAData->szDescription, "CryEngine web port - BSD sockets");
		strcpy(lpWSAData->szSystemStatus, "Running");
	}
	return 0;   // Winsock returns 0 on success, not a boolean
}

inline int WSACleanup() { return 0; }

//////////////////////////////////////////////////////////////////////////
// Error reporting
//
// The WSAE* constants are aliased to their errno equivalents in
// LinuxSpecific.h, so returning errno directly makes existing comparisons
// such as "err == WSAEWOULDBLOCK" work unchanged.
//////////////////////////////////////////////////////////////////////////
inline int  WSAGetLastError()      { return errno; }
inline void WSASetLastError(int e) { errno = e; }

//////////////////////////////////////////////////////////////////////////
// Socket lifetime and options
//////////////////////////////////////////////////////////////////////////
inline int closesocket(int s) { return close(s); }

// Winsock's ioctlsocket handles exactly one command the engine uses: FIONBIO,
// to toggle non-blocking mode. POSIX spells that with fcntl, and getting it
// wrong would make the whole netcode block on every recvfrom.
#ifndef FIONBIO
#	define FIONBIO 0x5421
#endif

inline int ioctlsocket(int s, long cmd, unsigned long* argp)
{
	if (cmd != FIONBIO || !argp)
		return -1;

	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) return -1;

	flags = (*argp != 0) ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
	return fcntl(s, F_SETFL, flags) < 0 ? -1 : 0;
}

#endif // _CRY_WEBPORT_WINSOCK_COMPAT_H_
