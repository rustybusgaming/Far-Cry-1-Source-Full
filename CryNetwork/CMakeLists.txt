################################################################################
# CryNetwork -- the netcode: datagram sockets, the CCP reliability protocol,
# client/server session management.
#
# Winsock IS shimmed here, unlike DirectInput, and the distinction is
# deliberate: Winsock is BSD sockets with different spellings, so
# CryCommon/WinSockCompat.h is an honest mapping rather than a fiction. That
# makes the module compile and work on a NATIVE build.
#
# It does not make it work in a browser. Far Cry's netcode is UDP throughout
# and a browser cannot open a UDP socket at all -- see WebTransport.h for the
# transport seam and why WebRTC DataChannel, not WebSocket, is the destination.
#
# The UBI.com matchmaking/CD-key SDK is compiled out via NOT_USE_UBICOM_SDK:
# it ships as lib_win32/*.lib with no source and no non-Windows build, and the
# services it talks to were shut down years ago. ProjectDefines.h documents
# that define as the supported way to build without it.
################################################################################

cry_add_module(CryNetwork
    SOURCES
    CCPEndpoint.cpp
    CTPEndpoint.cpp
    CTPEndpointGNB.cpp
    Client.cpp
    ClientLocal.cpp
    ClientStateMachine.cpp
    CompressionHelper.cpp
    CryNetwork.cpp
    DatagramSocket.cpp
    DefenceWall.cpp
    NETServerSnooper.cpp
    Network.cpp
    NewUbisoftCDKey.cpp
    NewUbisoftClient.cpp
    NewUbisoftMSClient.cpp
    NewUbisoftRegServer.cpp
    PunkBusterInterface.cpp
    RConSystem.cpp
    ScriptObjectNewUbisoftClient.cpp
    Server.cpp
    ServerSlot.cpp
    ServerSnooper.cpp
    ServerStateMachine.cpp
    StaticCharCompressor.cpp
    StdAfx.cpp
    UbisoftMemory.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        CryCommon
)
