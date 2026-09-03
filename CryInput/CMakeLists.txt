################################################################################
# CryInput -- input devices and action mapping.
#
# This is the first module where the port REPLACES a subsystem rather than
# shimming it. DirectInput 8 has no POSIX or web equivalent -- it is a COM API
# built on device enumeration, acquisition and exclusive cooperative levels --
# so WebInput.cpp implements IKeyboard and IMouse from DOM events instead, and
# XKeyboard.cpp / XMouse.cpp are not built. See CryInput/WebInput.h for the
# reasoning and CryInput/DInputCompat.h for why the DirectInput type NAMES
# still have to exist.
#
# Everything else in the module -- action maps, joystick, gamepad logic -- is
# device-independent and is reused unchanged.
#
# Not built (see tools/triage.py --excluded):
#   XKeyboard.cpp  DirectInput keyboard, replaced by CWebKeyboard
#   XMouse.cpp     DirectInput mouse, replaced by CWebMouse
#   XGamepad.cpp   Xbox-only; IGamepad is declared inside #ifdef _XBOX
################################################################################

cry_add_module(CryInput
    SOURCES
    CryInput.cpp
    Input.cpp
    Joystick.cpp
    StdAfx.cpp
    WebInput.cpp
    XActionMap.cpp
    XActionMapManager.cpp
    XDebugKeyboard.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        CryCommon
)
