////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   DInputCompat.h
//  Description: Placeholder DirectInput handle types for non-Windows builds.
//
//  READ THIS BEFORE ADDING ANYTHING HERE.
//
//  This is NOT a DirectInput emulation layer, and it must never become one.
//  The web port replaces DirectInput outright -- see WebInput.h -- rather than
//  reimplementing a COM API whose only client is this engine.
//
//  What this header exists for is narrower. CryInput's device classes declare
//
//      bool Init(CInput*, ISystem*, LPDIRECTINPUT8&, HINSTANCE, HWND);
//
//  and CInput stores an LPDIRECTINPUT8 member. Those declarations sit in
//  XKeyboard.h / XMouse.h / Input.h, which every other file in the module
//  includes -- so six translation units that never touch an input device at
//  all (the action maps, the gamepad logic, the joystick) failed to compile
//  purely because a type name in a declaration they never call was unknown.
//
//  So this declares those names as incomplete, opaque types. That is enough
//  for the declarations to parse and deliberately NOT enough to call anything:
//  any code that actually tries to use DirectInput fails to compile, loudly,
//  which is exactly what should happen. The two files that really do use it,
//  XKeyboard.cpp and XMouse.cpp, are excluded from the build and replaced by
//  CWebKeyboard and CWebMouse.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_DINPUT_COMPAT_H_
#define _CRY_WEBPORT_DINPUT_COMPAT_H_

#if !defined(LINUX)
#	error "DInputCompat.h is for the non-Windows build; use the real <dinput.h>"
#endif

// Incomplete on purpose: declared, never defined. A pointer to an incomplete
// type can be stored and passed; it cannot be dereferenced, and no member can
// be called on it. Any real DirectInput use is therefore a compile error
// rather than a silent no-op.
struct IDirectInput8A;
struct IDirectInputDevice8A;

typedef struct IDirectInput8A*        LPDIRECTINPUT8;
typedef struct IDirectInputDevice8A*  LPDIRECTINPUTDEVICE8;

#endif // _CRY_WEBPORT_DINPUT_COMPAT_H_
