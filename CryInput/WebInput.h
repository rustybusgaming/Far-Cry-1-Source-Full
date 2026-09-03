////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WebInput.h
//  Description: Browser-backed replacements for CXKeyboard and CXMouse.
//
//  WHY A REPLACEMENT RATHER THAN A SHIM
//
//  CryInput's device layer is DirectInput 8. Every other Win32 dependency in
//  this port could be shimmed because the call had a POSIX equivalent with the
//  same shape. DirectInput has none: it is a COM API built on device
//  enumeration, acquisition, exclusive cooperative levels and buffered object
//  data, and none of that maps onto anything a browser exposes. Emulating it
//  would mean writing a fake COM object whose only client is this engine.
//
//  So this is a genuine replacement, not a compatibility layer. The DirectInput
//  coupling is confined to two files -- XKeyboard.cpp and XMouse.cpp -- and
//  everything else in CryInput (XActionMap, XActionMapManager, XGamepad,
//  Joystick, XDebugKeyboard) is device-independent logic that is reused
//  unchanged. These classes implement the same IKeyboard and IMouse interfaces
//  from CryCommon/IInput.h, so nothing above them can tell the difference.
//
//  DESIGN
//
//  The browser delivers input asynchronously via DOM events; the engine polls
//  per frame. The two are bridged by a queue: event callbacks push into it, and
//  Update() drains it into the current/previous state arrays at the frame
//  boundary. That reproduces DirectInput's buffered-data model, and it is why
//  the original's edge semantics survive exactly:
//
//      KeyDown     = current
//      KeyPressed  = current && !previous     (rising edge)
//      KeyReleased = !current && previous     (falling edge)
//
//  The event SOURCE is deliberately separable from the state machine. Under
//  Emscripten the source is emscripten_set_keydown_callback and friends; in a
//  native build there is none, and events are injected directly. That is not a
//  testing convenience bolted on afterwards -- it is what makes the input
//  state machine testable at all, since none of it can run in a browser under
//  a unit test.
//
//  MOUSE CAPTURE
//
//  IMouse::SetExclusive(true) means "capture the pointer and report relative
//  motion" -- a first-person shooter cannot work otherwise. The browser
//  equivalent is the Pointer Lock API. It carries a constraint DirectInput did
//  not: pointer lock can only be requested from inside a user-gesture event
//  handler (a click or keypress), so a request made at an arbitrary time is
//  rejected by the browser. See CWebMouse::SetExclusive.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_WEBINPUT_H_
#define _CRY_WEBPORT_WEBINPUT_H_

#include <platform.h>
#include <IInput.h>
#include <vector>

//////////////////////////////////////////////////////////////////////////
// Key mapping
//
// Browser keyboard events carry three identifiers. "key" is the produced
// character and changes with layout and modifiers; "keyCode" is deprecated and
// inconsistent between browsers; "code" names the PHYSICAL key and is stable
// across layouts. A game binds movement to the physical W-A-S-D positions, so
// "code" is the only correct choice -- with "key", a player on AZERTY would
// find their movement keys silently rebound.
//////////////////////////////////////////////////////////////////////////
namespace WebInput
{
	//! Translate a DOM KeyboardEvent.code to an XKEY_* value.
	//! Returns 0 for keys the engine has no code for.
	int  XKeyFromDomCode(const char* domCode);

	//! Translate a DOM MouseEvent.button to an XKEY_MOUSE* value.
	int  XKeyFromDomButton(int domButton);
}

//////////////////////////////////////////////////////////////////////////
// CWebKeyboard
//////////////////////////////////////////////////////////////////////////
class CWebKeyboard : public IKeyboard
{
public:
	CWebKeyboard();
	virtual ~CWebKeyboard();

	bool Init(ISystem* pSystem);

	// --- event injection (called by the Emscripten callbacks, and by tests)
	void OnKeyEvent(const char* domCode, bool bDown);
	void OnKeyEventXKey(int xkey, bool bDown);

	//! Promote current state to previous and apply everything queued since the
	//! last call. Must be called once per frame, before any KeyPressed query.
	void Update();

	// --- IKeyboard
	virtual void ShutDown();
	virtual bool KeyDown(int p_key);
	virtual bool KeyPressed(int p_key);
	virtual bool KeyReleased(int p_key);
	virtual void ClearKey(int p_key);
	virtual int  GetKeyPressedCode();
	virtual const char* GetKeyPressedName();
	virtual int  GetKeyDownCode();
	virtual const char* GetKeyDownName();
	virtual void SetExclusive(bool value, void* hwnd = 0);
	virtual void WaitForKey();
	virtual void ClearKeyState();

	//! Update() must run once per frame; CInput drives it.
	//! The rest of this block is the surface CInput uses beyond IKeyboard.
	bool IsInit() const              { return m_bInitialized; }
	unsigned char GetKeyState(int nKey);
	int  GetModifiers() const        { return m_modifiers; }
	bool GetOSKeyName(int nKey, wchar_t* szwKeyName, int iBufSize);
	void FeedVirtualKey(int nVirtualKey, long lParam, bool bDown);
	void SetPad(void* /*pJoy*/)      {}   // no gamepad path on the web yet

	//! Printable character for a key, used when drawing a binding in the UI.
	unsigned char XKEY2ASCII(unsigned short nCode, int modifiers);

	static const int kMaxKeys = 0x200;

private:
	struct SEvent { int xkey; bool down; };

	ISystem*            m_pSystem;
	std::vector<SEvent> m_queue;      // events since the last Update()
	unsigned char       m_state[kMaxKeys];
	unsigned char       m_prevState[kMaxKeys];
	bool                m_bExclusive;
	bool                m_bInitialized;
	int                 m_modifiers;
};

//////////////////////////////////////////////////////////////////////////
// CWebMouse
//////////////////////////////////////////////////////////////////////////
class CWebMouse : public IMouse
{
public:
	CWebMouse();
	virtual ~CWebMouse();

	bool Init(ISystem* pSystem);

	// --- event injection
	void OnMouseButton(int domButton, bool bDown);
	void OnMouseMove(float dx, float dy, float absX, float absY);
	void OnMouseWheel(float dz);
	void OnPointerLockChanged(bool bLocked);

	//! CInput passes its focus state; the web build ignores it, because a
	//! browser tab that has lost focus stops delivering events anyway.
	void Update(bool bPrevFocus) { (void)bPrevFocus; Update(); }
	void Update();

	bool IsInit() const { return m_bInitialized; }
	bool MouseDblClick(int p_numButton);
	bool GetOSKeyName(int nKey, wchar_t* szwKeyName, int iBufSize);
	void SetPad(void* /*pJoy*/) {}

	// --- IMouse
	virtual void Shutdown();
	virtual bool MouseDown(int p_numButton);
	virtual bool MousePressed(int p_numButton);
	virtual bool MouseReleased(int p_numButton);
	virtual void SetMouseWheelRotation(int value);
	virtual bool SetExclusive(bool value, void* hwnd = 0);
	virtual float GetDeltaX();
	virtual float GetDeltaY();
	virtual float GetDeltaZ();
	virtual void SetInertia(float f);
	virtual void SetVScreenX(float fX);
	virtual void SetVScreenY(float fY);
	virtual float GetVScreenX();
	virtual float GetVScreenY();
	virtual void SetSensitvity(float f);
	virtual float GetSensitvity();
	virtual void SetSensitvityScale(float f);
	virtual float GetSensitvityScale();
	virtual void ClearKeyState();

	//! True once the browser has actually granted pointer lock. SetExclusive
	//! only REQUESTS it; the grant arrives later, or never.
	bool IsPointerLocked() const { return m_bPointerLocked; }

	static const int kMaxButtons = 8;

private:
	// Button events are QUEUED rather than applied on arrival, exactly as key
	// events are. Writing straight into m_buttons looks simpler and is wrong:
	// Update() copies current state into previous first, so a button that went
	// down between frames would already be set in BOTH arrays and its rising
	// edge would never be observed. MousePressed() would then never fire.
	struct SButtonEvent { int button; bool down; };

	ISystem* m_pSystem;

	std::vector<SButtonEvent> m_buttonQueue;
	unsigned char m_buttons[kMaxButtons];
	unsigned char m_prevButtons[kMaxButtons];

	float m_accumDX, m_accumDY, m_accumDZ;   // accumulated since last Update()
	float m_deltaX,  m_deltaY,  m_deltaZ;    // reported for the current frame
	float m_vScreenX, m_vScreenY;
	float m_sensitivity, m_sensitivityScale, m_inertia;

	bool  m_bExclusiveRequested;
	bool  m_bPointerLocked;
	bool  m_bInitialized;
	double m_lastClickTime[kMaxButtons];
};

//! Register the DOM event callbacks against these two devices. Emscripten
//! builds only; CInput calls it once, after Init().
#if defined(__EMSCRIPTEN__)
void WebInput_Register(CWebKeyboard* pKeyboard, CWebMouse* pMouse);
#endif

#endif // _CRY_WEBPORT_WEBINPUT_H_
