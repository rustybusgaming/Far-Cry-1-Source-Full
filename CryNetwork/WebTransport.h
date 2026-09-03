////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WebTransport.h
//  Description: The seam where Far Cry's UDP netcode meets the browser.
//
//  THE PROBLEM
//
//  CryNetwork is UDP throughout: CDatagramSocket creates SOCK_DGRAM sockets and
//  the protocol above it (CCPEndpoint) implements its own sequencing, acks and
//  retransmission on the assumption that the transport underneath is
//  unreliable, unordered and message-oriented.
//
//  A browser cannot open a UDP socket. It cannot open a raw TCP socket either.
//  This is not a gap to be shimmed -- it is a deliberate property of the web
//  security model, and no amount of compatibility-layer work changes it.
//
//  WHAT THE BROWSER ACTUALLY OFFERS
//
//    WebSocket           reliable, ORDERED, message-oriented, over TCP
//    WebRTC DataChannel  configurable: can be unreliable AND unordered
//    WebTransport (API)  datagram support, but limited browser availability
//
//  Emscripten's default is to emulate BSD sockets over WebSockets. That gets
//  the code running with no changes here, and it is the right first step -- but
//  the semantic mismatch is real and should be understood before it is blamed
//  for mysterious stalls:
//
//    Reliability   UDP may drop; WebSocket never does. Gaining reliability
//                  costs nothing -- CCPEndpoint's retransmit logic simply never
//                  fires. Harmless.
//
//    Framing       Both are message-oriented, so datagram boundaries survive.
//                  Harmless. (This is why WebSocket beats raw TCP here: TCP
//                  would require re-framing every datagram by hand.)
//
//    ORDERING      This is the one that hurts. UDP delivers packet N+1 even if
//                  N is lost; the game skips the gap and moves on. An ordered
//                  transport holds N+1 back until N has been retransmitted --
//                  head-of-line blocking. Under loss, a protocol designed to
//                  degrade gracefully instead freezes, and the freeze lasts a
//                  full round-trip. The worse the connection, the worse the
//                  mismatch, which is precisely backwards.
//
//  A WebSocket also needs a RELAY: the browser connects to a proxy that
//  terminates the WebSocket and forwards real UDP to the game server, because
//  a stock Far Cry dedicated server speaks UDP and nothing else. Nothing in
//  this repository provides that relay.
//
//  WHAT THIS INTERFACE IS FOR
//
//  The transport choice is a decision the port should be able to change without
//  touching CryNetwork. IWebTransport is that seam: an unreliable, unordered,
//  message-oriented channel -- the contract CryNetwork already expects -- with
//  the browser mechanism behind it left open.
//
//    CWebSocketTransport   simple, works today, ordered (accept the stalls)
//    CDataChannelTransport {ordered:false, maxRetransmits:0} -- genuinely
//                          datagram-like, and the correct destination. Costs a
//                          signalling server and an ICE exchange.
//
//  Implementations are Milestone 3 work. The interface is here now so that
//  CryNetwork's socket layer can be pointed at it when they land, rather than
//  the transport choice being scattered across the module.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_WEBTRANSPORT_H_
#define _CRY_WEBPORT_WEBTRANSPORT_H_

#include <platform.h>

//! Maximum payload of a single datagram. Chosen to sit under the common
//! Ethernet MTU (1500) minus IP+UDP headers, so that a DataChannel transport
//! does not have to fragment -- SCTP fragmentation would reintroduce exactly
//! the head-of-line coupling the unordered mode exists to avoid.
const int WEB_TRANSPORT_MTU = 1200;

struct IWebTransportSink
{
	virtual ~IWebTransportSink() {}

	//! One complete datagram arrived. Called from the browser's event loop, so
	//! it must not block: queue and return.
	virtual void OnDatagram(const unsigned char* pData, int nLength) = 0;

	virtual void OnConnected() = 0;
	virtual void OnDisconnected(const char* szReason) = 0;
};

struct IWebTransport
{
	virtual ~IWebTransport() {}

	//! Begin connecting. Returns false only on an argument that cannot work at
	//! all; a connection FAILURE arrives later through OnDisconnected, because
	//! nothing in the browser connects synchronously.
	virtual bool Connect(const char* szUrl, IWebTransportSink* pSink) = 0;

	virtual void Disconnect() = 0;

	//! Queue one datagram. Returns false if it exceeds WEB_TRANSPORT_MTU or the
	//! channel is not open. Like sendto(), a true return means "handed to the
	//! transport", never "delivered".
	virtual bool Send(const unsigned char* pData, int nLength) = 0;

	//! Pump the transport. Callbacks fire from here, not from arbitrary points
	//! in the browser event loop, so CryNetwork sees them at a frame boundary
	//! exactly as it saw recvfrom() results.
	virtual void Update() = 0;

	virtual bool IsConnected() const = 0;

	//! Whether this transport preserves UDP's delivery semantics. A WebSocket
	//! transport returns false: the netcode still works, but is subject to
	//! head-of-line blocking and should say so in a connection log rather than
	//! leaving the stalls unexplained.
	virtual bool IsUnordered() const = 0;
};

#endif // _CRY_WEBPORT_WEBTRANSPORT_H_
