////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port -- browser input backend. See WebInput.h.
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "WebInput.h"

#include <string.h>
#include <stdio.h>

#if defined(__EMSCRIPTEN__)
#	include <emscripten/emscripten.h>
#	include <emscripten/html5.h>
#endif

//////////////////////////////////////////////////////////////////////////
// DOM code -> XKEY mapping
//
// The table is keyed on KeyboardEvent.code (the physical key) rather than
// .key (the produced character). Sorted only for readability; lookup is a
// linear scan, which is fine -- it runs once per key event, not per frame.
//////////////////////////////////////////////////////////////////////////
namespace
{
	struct SKeyMapEntry { const char* domCode; int xkey; };

	const SKeyMapEntry g_keyMap[] =
	{
		// Letters. "KeyW" is the physical W position on every layout, which is
		// what a movement binding actually means.
		{"KeyA", XKEY_A}, {"KeyB", XKEY_B}, {"KeyC", XKEY_C}, {"KeyD", XKEY_D},
		{"KeyE", XKEY_E}, {"KeyF", XKEY_F}, {"KeyG", XKEY_G}, {"KeyH", XKEY_H},
		{"KeyI", XKEY_I}, {"KeyJ", XKEY_J}, {"KeyK", XKEY_K}, {"KeyL", XKEY_L},
		{"KeyM", XKEY_M}, {"KeyN", XKEY_N}, {"KeyO", XKEY_O}, {"KeyP", XKEY_P},
		{"KeyQ", XKEY_Q}, {"KeyR", XKEY_R}, {"KeyS", XKEY_S}, {"KeyT", XKEY_T},
		{"KeyU", XKEY_U}, {"KeyV", XKEY_V}, {"KeyW", XKEY_W}, {"KeyX", XKEY_X},
		{"KeyY", XKEY_Y}, {"KeyZ", XKEY_Z},

		// Digit row
		{"Digit0", XKEY_0}, {"Digit1", XKEY_1}, {"Digit2", XKEY_2},
		{"Digit3", XKEY_3}, {"Digit4", XKEY_4}, {"Digit5", XKEY_5},
		{"Digit6", XKEY_6}, {"Digit7", XKEY_7}, {"Digit8", XKEY_8},
		{"Digit9", XKEY_9},

		// Punctuation
		{"Backquote", XKEY_TILDE},      {"Minus", XKEY_MINUS},
		{"Equal", XKEY_EQUALS},         {"BracketLeft", XKEY_LBRACKET},
		{"BracketRight", XKEY_RBRACKET},{"Backslash", XKEY_BACKSLASH},
		{"Semicolon", XKEY_SEMICOLON},  {"Quote", XKEY_APOSTROPHE},
		{"Comma", XKEY_COMMA},          {"Period", XKEY_PERIOD},
		{"Slash", XKEY_SLASH},          {"IntlBackslash", XKEY_OEM_102},

		// Editing and navigation
		{"Escape", XKEY_ESCAPE},        {"Space", XKEY_SPACE},
		{"Enter", XKEY_RETURN},         {"Backspace", XKEY_BACKSPACE},
		{"Tab", XKEY_TAB},              {"Delete", XKEY_DELETE},
		{"Insert", XKEY_INSERT},        {"Home", XKEY_HOME},
		{"End", XKEY_END},              {"PageUp", XKEY_PAGE_UP},
		{"PageDown", XKEY_PAGE_DOWN},

		{"ArrowUp", XKEY_UP},           {"ArrowDown", XKEY_DOWN},
		{"ArrowLeft", XKEY_LEFT},       {"ArrowRight", XKEY_RIGHT},

		// Locks and system keys
		{"CapsLock", XKEY_CAPSLOCK},    {"NumLock", XKEY_NUMLOCK},
		{"ScrollLock", XKEY_SCROLLLOCK},{"Pause", XKEY_PAUSE},
		{"PrintScreen", XKEY_PRINT},

		// Modifiers. The browser distinguishes left and right, and so does the
		// engine, so they map one-to-one rather than collapsing to XKEY_SHIFT.
		{"ShiftLeft", XKEY_LSHIFT},     {"ShiftRight", XKEY_RSHIFT},
		{"ControlLeft", XKEY_LCONTROL}, {"ControlRight", XKEY_RCONTROL},
		{"AltLeft", XKEY_LALT},         {"AltRight", XKEY_RALT},
		{"MetaLeft", XKEY_WIN_LWINDOW}, {"MetaRight", XKEY_WIN_RWINDOW},
		{"ContextMenu", XKEY_WIN_APPS},

		// Function keys
		{"F1", XKEY_F1},   {"F2", XKEY_F2},   {"F3", XKEY_F3},   {"F4", XKEY_F4},
		{"F5", XKEY_F5},   {"F6", XKEY_F6},   {"F7", XKEY_F7},   {"F8", XKEY_F8},
		{"F9", XKEY_F9},   {"F10", XKEY_F10}, {"F11", XKEY_F11}, {"F12", XKEY_F12},
		{"F13", XKEY_F13}, {"F14", XKEY_F14}, {"F15", XKEY_F15},

		// Numeric keypad
		{"Numpad0", XKEY_NUMPAD0}, {"Numpad1", XKEY_NUMPAD1},
		{"Numpad2", XKEY_NUMPAD2}, {"Numpad3", XKEY_NUMPAD3},
		{"Numpad4", XKEY_NUMPAD4}, {"Numpad5", XKEY_NUMPAD5},
		{"Numpad6", XKEY_NUMPAD6}, {"Numpad7", XKEY_NUMPAD7},
		{"Numpad8", XKEY_NUMPAD8}, {"Numpad9", XKEY_NUMPAD9},
		{"NumpadAdd", XKEY_ADD},           {"NumpadSubtract", XKEY_SUBTRACT},
		{"NumpadMultiply", XKEY_MULTIPLY}, {"NumpadDivide", XKEY_DIVIDE},
		{"NumpadDecimal", XKEY_DECIMAL},   {"NumpadEnter", XKEY_NUMPADENTER},
	};

	const int g_keyMapCount = (int)(sizeof(g_keyMap) / sizeof(g_keyMap[0]));
}

int WebInput::XKeyFromDomCode(const char* domCode)
{
	if (!domCode) return 0;
	for (int i = 0; i < g_keyMapCount; ++i)
		if (strcmp(g_keyMap[i].domCode, domCode) == 0)
			return g_keyMap[i].xkey;
	return 0;   // unmapped: browsers emit codes the engine has no name for
}

int WebInput::XKeyFromDomButton(int domButton)
{
	// DOM MouseEvent.button: 0 left, 1 middle, 2 right.
	switch (domButton)
	{
	case 0:  return XKEY_MOUSE1;
	case 1:  return XKEY_MOUSE3;   // middle -- note the DOM order differs
	case 2:  return XKEY_MOUSE2;   // from the engine's button numbering
	case 3:  return XKEY_MOUSE4;
	case 4:  return XKEY_MOUSE5;
	default: return 0;
	}
}

//////////////////////////////////////////////////////////////////////////
// CWebKeyboard
//////////////////////////////////////////////////////////////////////////
CWebKeyboard::CWebKeyboard()
	: m_pSystem(0), m_bExclusive(false), m_bInitialized(false), m_modifiers(0)
{
	memset(m_state, 0, sizeof(m_state));
	memset(m_prevState, 0, sizeof(m_prevState));
}

CWebKeyboard::~CWebKeyboard() { ShutDown(); }

void CWebKeyboard::ShutDown() { ClearKeyState(); }

bool CWebKeyboard::Init(ISystem* pSystem)
{
	m_pSystem = pSystem;
	ClearKeyState();
	m_bInitialized = true;
	return true;
}

unsigned char CWebKeyboard::GetKeyState(int nKey)
{
	if (nKey <= 0 || nKey >= kMaxKeys) return 0;
	return m_state[nKey];
}

bool CWebKeyboard::GetOSKeyName(int nKey, wchar_t* szwKeyName, int iBufSize)
{
	// Win32 answered this from the keyboard layout so the UI could show the
	// key's printed label. A browser exposes no layout information: only the
	// physical code (which is what we key on) and the produced character, and
	// the latter is only known while a key is actually down. Falling back to
	// the DOM code name is honest and stable -- "BracketLeft" rather than a
	// guess at what is printed on the player's keycap.
	if (!szwKeyName || iBufSize <= 0) return false;

	const char* name = NULL;
	for (int i = 0; i < g_keyMapCount; ++i)
		if (g_keyMap[i].xkey == nKey) { name = g_keyMap[i].domCode; break; }
	if (!name) { szwKeyName[0] = 0; return false; }

	int i = 0;
	for (; name[i] && i < iBufSize - 1; ++i)
		szwKeyName[i] = (wchar_t)(unsigned char)name[i];
	szwKeyName[i] = 0;
	return true;
}

unsigned char CWebKeyboard::XKEY2ASCII(unsigned short nCode, int modifiers)
{
	// Only used to draw a key binding in the UI, never for text entry -- the
	// browser gives real typed characters through KeyboardEvent.key, which the
	// engine does not consume here.
	//
	// This is US-layout, and knowingly so: the mapping runs from a PHYSICAL key
	// to a character, and what that key actually produces depends on a layout
	// the browser does not expose. Anything else would be a guess dressed up as
	// fact. Keys with no printable form return 0.
	const bool bShift = (modifiers & 1) != 0;

	if (nCode >= XKEY_A && nCode <= XKEY_Z)
		return (unsigned char)((bShift ? 'A' : 'a') + (nCode - XKEY_A));

	if (nCode >= XKEY_0 && nCode <= XKEY_9)
	{
		static const char* kShifted = ")!@#$%^&*(";
		int digit = nCode - XKEY_0;
		return (unsigned char)(bShift ? kShifted[digit] : ('0' + digit));
	}

	if (nCode >= XKEY_NUMPAD0 && nCode <= XKEY_NUMPAD9)
		return (unsigned char)('0' + (nCode - XKEY_NUMPAD0));

	switch (nCode)
	{
	case XKEY_SPACE:      return ' ';
	case XKEY_TAB:        return '\t';
	case XKEY_RETURN:     return '\n';
	case XKEY_MINUS:      return bShift ? '_' : '-';
	case XKEY_EQUALS:     return bShift ? '+' : '=';
	case XKEY_LBRACKET:   return bShift ? '{' : '[';
	case XKEY_RBRACKET:   return bShift ? '}' : ']';
	case XKEY_BACKSLASH:  return bShift ? '|' : '\\';
	case XKEY_SEMICOLON:  return bShift ? ':' : ';';
	case XKEY_APOSTROPHE: return bShift ? '"' : '\'';
	case XKEY_COMMA:      return bShift ? '<' : ',';
	case XKEY_PERIOD:     return bShift ? '>' : '.';
	case XKEY_SLASH:      return bShift ? '?' : '/';
	case XKEY_TILDE:      return bShift ? '~' : '`';
	case XKEY_ADD:        return '+';
	case XKEY_SUBTRACT:   return '-';
	case XKEY_MULTIPLY:   return '*';
	case XKEY_DIVIDE:     return '/';
	case XKEY_DECIMAL:    return '.';
	default:              return 0;
	}
}

void CWebKeyboard::FeedVirtualKey(int nVirtualKey, long /*lParam*/, bool bDown)
{
	// On Win32 this injected a key from the window message pump, which the
	// browser has no equivalent of -- DOM events already arrive through
	// OnKeyEvent. It is kept so callers still compile, and treated as a direct
	// XKEY injection, which is the only meaning it can have here.
	OnKeyEventXKey(nVirtualKey, bDown);
}

void CWebKeyboard::OnKeyEvent(const char* domCode, bool bDown)
{
	int xkey = WebInput::XKeyFromDomCode(domCode);
	if (xkey) OnKeyEventXKey(xkey, bDown);
}

void CWebKeyboard::OnKeyEventXKey(int xkey, bool bDown)
{
	if (xkey <= 0 || xkey >= kMaxKeys) return;
	SEvent e; e.xkey = xkey; e.down = bDown;
	m_queue.push_back(e);
}

void CWebKeyboard::Update()
{
	// Previous state is the state as of the last frame boundary, which is what
	// the edge queries compare against.
	memcpy(m_prevState, m_state, sizeof(m_state));

	for (size_t i = 0; i < m_queue.size(); ++i)
		m_state[m_queue[i].xkey] = m_queue[i].down ? 0x80 : 0x00;
	m_queue.clear();

	// Recomputed from state rather than tracked incrementally: a modifier
	// released while the tab was unfocused never delivers a keyup, and an
	// incremental count would stay stuck down forever.
	m_modifiers = 0;
	if (KeyDown(XKEY_LSHIFT)   || KeyDown(XKEY_RSHIFT))   m_modifiers |= 1;
	if (KeyDown(XKEY_LCONTROL) || KeyDown(XKEY_RCONTROL)) m_modifiers |= 2;
	if (KeyDown(XKEY_LALT)     || KeyDown(XKEY_RALT))     m_modifiers |= 4;
}

bool CWebKeyboard::KeyDown(int p_key)
{
	if (p_key <= 0 || p_key >= kMaxKeys) return false;
	return (m_state[p_key] & 0x80) != 0;
}

bool CWebKeyboard::KeyPressed(int p_key)
{
	if (p_key <= 0 || p_key >= kMaxKeys) return false;
	return (m_state[p_key] & 0x80) != 0 && (m_prevState[p_key] & 0x80) == 0;
}

bool CWebKeyboard::KeyReleased(int p_key)
{
	if (p_key <= 0 || p_key >= kMaxKeys) return false;
	return (m_state[p_key] & 0x80) == 0 && (m_prevState[p_key] & 0x80) != 0;
}

void CWebKeyboard::ClearKey(int p_key)
{
	if (p_key <= 0 || p_key >= kMaxKeys) return;
	m_state[p_key] = 0;
	m_prevState[p_key] = 0;
}

int CWebKeyboard::GetKeyPressedCode()
{
	for (int i = 1; i < kMaxKeys; ++i)
		if (KeyPressed(i)) return i;
	return -1;
}

int CWebKeyboard::GetKeyDownCode()
{
	for (int i = 1; i < kMaxKeys; ++i)
		if (KeyDown(i)) return i;
	return -1;
}

const char* CWebKeyboard::GetKeyPressedName()
{
	int k = GetKeyPressedCode();
	if (k < 0) return NULL;
	for (int i = 0; i < g_keyMapCount; ++i)
		if (g_keyMap[i].xkey == k) return g_keyMap[i].domCode;
	return NULL;
}

const char* CWebKeyboard::GetKeyDownName()
{
	int k = GetKeyDownCode();
	if (k < 0) return NULL;
	for (int i = 0; i < g_keyMapCount; ++i)
		if (g_keyMap[i].xkey == k) return g_keyMap[i].domCode;
	return NULL;
}

void CWebKeyboard::SetExclusive(bool value, void* /*hwnd*/)
{
	// A browser has no exclusive keyboard mode: the page receives key events
	// while focused and cannot take the keyboard from the browser chrome. The
	// flag is recorded so callers see a consistent value, but there is nothing
	// to acquire. (Keyboard Lock exists for fullscreen, but only covers keys
	// like Escape and is not what the engine is asking for here.)
	m_bExclusive = value;
}

void CWebKeyboard::WaitForKey()
{
	// Blocking is impossible on the browser's single-threaded event loop: the
	// event that would end the wait can only be delivered by returning to it.
	// The engine uses this only in startup/debug paths, so it is a no-op
	// rather than a hang.
}

void CWebKeyboard::ClearKeyState()
{
	memset(m_state, 0, sizeof(m_state));
	memset(m_prevState, 0, sizeof(m_prevState));
	m_queue.clear();
}

//////////////////////////////////////////////////////////////////////////
// CWebMouse
//////////////////////////////////////////////////////////////////////////
CWebMouse::CWebMouse()
	: m_pSystem(0),
	  m_accumDX(0), m_accumDY(0), m_accumDZ(0),
	  m_deltaX(0), m_deltaY(0), m_deltaZ(0),
	  m_vScreenX(0), m_vScreenY(0),
	  m_sensitivity(1.0f), m_sensitivityScale(1.0f), m_inertia(0.0f),
	  m_bExclusiveRequested(false), m_bPointerLocked(false), m_bInitialized(false)
{
	memset(m_buttons, 0, sizeof(m_buttons));
	memset(m_prevButtons, 0, sizeof(m_prevButtons));
	for (int i = 0; i < kMaxButtons; ++i) m_lastClickTime[i] = -1.0;
}

CWebMouse::~CWebMouse() { Shutdown(); }

void CWebMouse::Shutdown() { ClearKeyState(); }

bool CWebMouse::Init(ISystem* pSystem)
{
	m_pSystem = pSystem;
	ClearKeyState();
	m_bInitialized = true;
	return true;
}

bool CWebMouse::MouseDblClick(int p_numButton)
{
	// The browser has a native dblclick event, but it fires on its own schedule
	// and cannot be correlated with the frame-based button state the engine
	// polls. Deriving it from two rising edges inside the platform double-click
	// interval keeps it in step with MousePressed().
	if (p_numButton < 0 || p_numButton >= kMaxButtons) return false;
	if (!MousePressed(p_numButton)) return false;

	const double now = (double)GetTickCount() / 1000.0;
	const double prev = m_lastClickTime[p_numButton];
	m_lastClickTime[p_numButton] = now;
	return prev >= 0.0 && (now - prev) <= 0.5;
}

bool CWebMouse::GetOSKeyName(int nKey, wchar_t* szwKeyName, int iBufSize)
{
	if (!szwKeyName || iBufSize <= 0) return false;
	const char* name = NULL;
	switch (nKey)
	{
	case XKEY_MOUSE1: name = "Mouse1"; break;
	case XKEY_MOUSE2: name = "Mouse2"; break;
	case XKEY_MOUSE3: name = "Mouse3"; break;
	case XKEY_MOUSE4: name = "Mouse4"; break;
	case XKEY_MOUSE5: name = "Mouse5"; break;
	default: szwKeyName[0] = 0; return false;
	}
	int i = 0;
	for (; name[i] && i < iBufSize - 1; ++i)
		szwKeyName[i] = (wchar_t)(unsigned char)name[i];
	szwKeyName[i] = 0;
	return true;
}

void CWebMouse::OnMouseButton(int domButton, bool bDown)
{
	if (domButton < 0 || domButton >= kMaxButtons) return;
	SButtonEvent e; e.button = domButton; e.down = bDown;
	m_buttonQueue.push_back(e);
}

void CWebMouse::OnMouseMove(float dx, float dy, float absX, float absY)
{
	// Deltas accumulate: the browser can deliver several move events between
	// two frames, and dropping all but the last would lose motion.
	m_accumDX += dx;
	m_accumDY += dy;

	// Absolute position is only meaningful when the pointer is NOT locked --
	// under pointer lock the cursor does not move and the browser reports
	// movementX/Y only.
	if (!m_bPointerLocked)
	{
		m_vScreenX = absX;
		m_vScreenY = absY;
	}
}

void CWebMouse::OnMouseWheel(float dz) { m_accumDZ += dz; }

void CWebMouse::OnPointerLockChanged(bool bLocked)
{
	m_bPointerLocked = bLocked;
	// Motion accumulated across a lock transition is meaningless -- the pointer
	// jumps -- so it is discarded rather than delivered as a huge view snap.
	m_accumDX = m_accumDY = 0.0f;
}

void CWebMouse::Update()
{
	memcpy(m_prevButtons, m_buttons, sizeof(m_buttons));

	for (size_t i = 0; i < m_buttonQueue.size(); ++i)
		m_buttons[m_buttonQueue[i].button] = m_buttonQueue[i].down ? 0x80 : 0x00;
	m_buttonQueue.clear();

	const float scale = m_sensitivity * m_sensitivityScale;
	m_deltaX = m_accumDX * scale;
	m_deltaY = m_accumDY * scale;
	m_deltaZ = m_accumDZ;

	m_accumDX = m_accumDY = m_accumDZ = 0.0f;
}

bool CWebMouse::MouseDown(int p_numButton)
{
	if (p_numButton < 0 || p_numButton >= kMaxButtons) return false;
	return (m_buttons[p_numButton] & 0x80) != 0;
}

bool CWebMouse::MousePressed(int p_numButton)
{
	if (p_numButton < 0 || p_numButton >= kMaxButtons) return false;
	return (m_buttons[p_numButton] & 0x80) != 0
	    && (m_prevButtons[p_numButton] & 0x80) == 0;
}

bool CWebMouse::MouseReleased(int p_numButton)
{
	if (p_numButton < 0 || p_numButton >= kMaxButtons) return false;
	return (m_buttons[p_numButton] & 0x80) == 0
	    && (m_prevButtons[p_numButton] & 0x80) != 0;
}

void CWebMouse::SetMouseWheelRotation(int value) { m_accumDZ += (float)value; }

bool CWebMouse::SetExclusive(bool value, void* /*hwnd*/)
{
	m_bExclusiveRequested = value;

#if defined(__EMSCRIPTEN__)
	if (value)
	{
		// IMPORTANT: the browser only grants pointer lock when the request
		// originates inside a user-gesture handler (click, keydown). Called
		// from anywhere else -- engine startup, a console command, a level
		// load -- it is refused, and there is no way to force it.
		//
		// The request is issued regardless so that a call made from within a
		// gesture works; the caller must treat exclusivity as REQUESTED, not
		// obtained, and read IsPointerLocked() for the truth.
		EMSCRIPTEN_RESULT r = emscripten_request_pointerlock("#canvas", EM_TRUE);
		return r == EMSCRIPTEN_RESULT_SUCCESS;
	}
	emscripten_exit_pointerlock();
	return true;
#else
	// Native/headless build: nothing to capture.
	m_bPointerLocked = value;
	return true;
#endif
}

float CWebMouse::GetDeltaX() { return m_deltaX; }
float CWebMouse::GetDeltaY() { return m_deltaY; }
float CWebMouse::GetDeltaZ() { return m_deltaZ; }

void  CWebMouse::SetInertia(float f)          { m_inertia = f; }
void  CWebMouse::SetVScreenX(float fX)        { m_vScreenX = fX; }
void  CWebMouse::SetVScreenY(float fY)        { m_vScreenY = fY; }
float CWebMouse::GetVScreenX()                { return m_vScreenX; }
float CWebMouse::GetVScreenY()                { return m_vScreenY; }
void  CWebMouse::SetSensitvity(float f)       { m_sensitivity = f; }
float CWebMouse::GetSensitvity()              { return m_sensitivity; }
void  CWebMouse::SetSensitvityScale(float f)  { m_sensitivityScale = f; }
float CWebMouse::GetSensitvityScale()         { return m_sensitivityScale; }

void CWebMouse::ClearKeyState()
{
	memset(m_buttons, 0, sizeof(m_buttons));
	memset(m_prevButtons, 0, sizeof(m_prevButtons));
	m_buttonQueue.clear();
	m_accumDX = m_accumDY = m_accumDZ = 0.0f;
	m_deltaX = m_deltaY = m_deltaZ = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Emscripten event wiring
//
// Registered once at init. The callbacks do nothing but translate and enqueue;
// all state transitions happen in Update() at the frame boundary, so an event
// arriving mid-frame cannot make a key appear pressed and released within the
// same frame.
//////////////////////////////////////////////////////////////////////////
#if defined(__EMSCRIPTEN__)

static CWebKeyboard* g_pWebKeyboard = 0;
static CWebMouse*    g_pWebMouse    = 0;

static EM_BOOL WebKeyCallback(int eventType, const EmscriptenKeyboardEvent* e, void*)
{
	if (!g_pWebKeyboard) return EM_FALSE;
	g_pWebKeyboard->OnKeyEvent(e->code, eventType == EMSCRIPTEN_EVENT_KEYDOWN);

	// Swallow keys the game has bound so the browser does not also act on them
	// (Tab moving focus out of the canvas, "/" opening quick-find, F-keys).
	// Unmapped keys are left alone so browser shortcuts keep working.
	return WebInput::XKeyFromDomCode(e->code) ? EM_TRUE : EM_FALSE;
}

static EM_BOOL WebMouseCallback(int eventType, const EmscriptenMouseEvent* e, void*)
{
	if (!g_pWebMouse) return EM_FALSE;
	switch (eventType)
	{
	case EMSCRIPTEN_EVENT_MOUSEDOWN:
		g_pWebMouse->OnMouseButton(e->button, true);  break;
	case EMSCRIPTEN_EVENT_MOUSEUP:
		g_pWebMouse->OnMouseButton(e->button, false); break;
	case EMSCRIPTEN_EVENT_MOUSEMOVE:
		g_pWebMouse->OnMouseMove((float)e->movementX, (float)e->movementY,
		                         (float)e->targetX,   (float)e->targetY);
		break;
	default: return EM_FALSE;
	}
	return EM_TRUE;
}

static EM_BOOL WebWheelCallback(int, const EmscriptenWheelEvent* e, void*)
{
	if (!g_pWebMouse) return EM_FALSE;
	g_pWebMouse->OnMouseWheel((float)-e->deltaY);
	return EM_TRUE;   // prevent the page from scrolling under the game
}

static EM_BOOL WebPointerLockCallback(int, const EmscriptenPointerlockChangeEvent* e, void*)
{
	if (g_pWebMouse) g_pWebMouse->OnPointerLockChanged(e->isActive != 0);
	return EM_TRUE;
}

void WebInput_Register(CWebKeyboard* pKeyboard, CWebMouse* pMouse)
{
	g_pWebKeyboard = pKeyboard;
	g_pWebMouse    = pMouse;

	// Keyboard events go to the window: a canvas only receives them when it
	// holds focus, and losing focus mid-game would silently stop all input.
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, EM_TRUE, WebKeyCallback);
	emscripten_set_keyup_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, EM_TRUE, WebKeyCallback);

	emscripten_set_mousedown_callback("#canvas", 0, EM_TRUE, WebMouseCallback);
	emscripten_set_mouseup_callback  ("#canvas", 0, EM_TRUE, WebMouseCallback);
	emscripten_set_mousemove_callback("#canvas", 0, EM_TRUE, WebMouseCallback);
	emscripten_set_wheel_callback    ("#canvas", 0, EM_TRUE, WebWheelCallback);

	emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, EM_TRUE,
	                                         WebPointerLockCallback);
}

#endif // __EMSCRIPTEN__
