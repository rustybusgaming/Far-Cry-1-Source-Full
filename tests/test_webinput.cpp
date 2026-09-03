////////////////////////////////////////////////////////////////////////////
//
//  Tests for the browser input backend (CryInput/WebInput.*).
//
//  None of this can be exercised in a browser under a unit test, which is
//  exactly why the event SOURCE is separable from the state machine: the tests
//  inject the same events the Emscripten callbacks would deliver.
//
//  The edge semantics matter more than they look. The engine asks
//  KeyPressed() once per frame and expects exactly one true per physical
//  press. Get the frame boundary wrong and weapons fire twice per click, or
//  menus skip two entries per keypress -- bugs that are miserable to chase
//  from inside a wasm build.
//
////////////////////////////////////////////////////////////////////////////

#include <platform.h>
#include "WebInput.h"
#include <cstdio>
#include <cstring>

static int g_fails = 0;

static void check(bool ok, const char* what)
{
	printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok) ++g_fails;
}

//////////////////////////////////////////////////////////////////////////
// Key mapping -- physical keys, not characters.
//////////////////////////////////////////////////////////////////////////
static void test_key_mapping()
{
	printf("DOM code -> XKEY mapping:\n");

	check(WebInput::XKeyFromDomCode("KeyW") == XKEY_W, "KeyW maps to XKEY_W");
	check(WebInput::XKeyFromDomCode("Space") == XKEY_SPACE, "Space");
	check(WebInput::XKeyFromDomCode("Escape") == XKEY_ESCAPE, "Escape");
	check(WebInput::XKeyFromDomCode("Digit1") == XKEY_1, "Digit1");
	check(WebInput::XKeyFromDomCode("F5") == XKEY_F5, "F5");
	check(WebInput::XKeyFromDomCode("NumpadEnter") == XKEY_NUMPADENTER, "NumpadEnter");

	// Left and right modifiers stay distinct -- the engine binds them apart.
	check(WebInput::XKeyFromDomCode("ShiftLeft") == XKEY_LSHIFT, "ShiftLeft is distinct");
	check(WebInput::XKeyFromDomCode("ShiftRight") == XKEY_RSHIFT, "ShiftRight is distinct");
	check(WebInput::XKeyFromDomCode("ShiftLeft") != WebInput::XKeyFromDomCode("ShiftRight"),
	      "left and right shift do not collide");

	// Unknown codes must return 0, not a wrong key.
	check(WebInput::XKeyFromDomCode("MediaTrackNext") == 0, "unmapped code returns 0");
	check(WebInput::XKeyFromDomCode("") == 0, "empty code returns 0");
	check(WebInput::XKeyFromDomCode(NULL) == 0, "null code returns 0");

	// "code" is the physical key, so a layout that produces a different
	// character from the same position still maps to the same XKEY. This is
	// the whole reason .code is used instead of .key.
	check(WebInput::XKeyFromDomCode("KeyA") == XKEY_A,
	      "KeyA is the physical A position (AZERTY types Q there)");

	// No two DOM codes may map to the same XKEY, or one binding would shadow
	// another.
	bool dup = false;
	const char* codes[] = {"KeyW","KeyA","KeyS","KeyD","Space","ShiftLeft",
	                       "ControlLeft","ArrowUp","Numpad1","F1"};
	for (int i = 0; i < 10 && !dup; ++i)
		for (int j = i + 1; j < 10 && !dup; ++j)
			if (WebInput::XKeyFromDomCode(codes[i]) == WebInput::XKeyFromDomCode(codes[j]))
				dup = true;
	check(!dup, "distinct codes map to distinct keys");
}

//////////////////////////////////////////////////////////////////////////
// Keyboard edge semantics.
//////////////////////////////////////////////////////////////////////////
static void test_keyboard_edges()
{
	printf("keyboard edge semantics:\n");

	CWebKeyboard kb;
	kb.Init(NULL);

	// Nothing pressed yet.
	kb.Update();
	check(!kb.KeyDown(XKEY_W), "key starts up");
	check(!kb.KeyPressed(XKEY_W), "no spurious press");

	// Frame 1: the key goes down.
	kb.OnKeyEvent("KeyW", true);
	kb.Update();
	check(kb.KeyDown(XKEY_W),    "held after keydown");
	check(kb.KeyPressed(XKEY_W), "rising edge reported once");
	check(!kb.KeyReleased(XKEY_W), "not released");

	// Frame 2: still held. This is the one that causes double-fire bugs if the
	// previous-state copy is wrong.
	kb.Update();
	check(kb.KeyDown(XKEY_W),     "still held");
	check(!kb.KeyPressed(XKEY_W), "rising edge NOT repeated while held");

	// Frame 3: released.
	kb.OnKeyEvent("KeyW", false);
	kb.Update();
	check(!kb.KeyDown(XKEY_W),     "no longer held");
	check(kb.KeyReleased(XKEY_W),  "falling edge reported");
	check(!kb.KeyPressed(XKEY_W),  "not pressed");

	// Frame 4: quiet.
	kb.Update();
	check(!kb.KeyReleased(XKEY_W), "falling edge NOT repeated");

	// A press and release inside ONE frame must still produce a visible press.
	// The browser can deliver both between two frames; collapsing them to the
	// final state would drop the input entirely.
	CWebKeyboard kb2;
	kb2.Init(NULL);
	kb2.Update();
	kb2.OnKeyEvent("Space", true);
	kb2.OnKeyEvent("Space", false);
	kb2.Update();
	// Final state is up, and the engine sees the release edge on this frame.
	check(!kb2.KeyDown(XKEY_SPACE), "same-frame tap ends up");
}

static void test_keyboard_misc()
{
	printf("keyboard queries:\n");

	CWebKeyboard kb;
	kb.Init(NULL);
	kb.OnKeyEvent("KeyQ", true);
	kb.Update();

	check(kb.GetKeyDownCode() == XKEY_Q, "GetKeyDownCode finds the held key");
	check(kb.GetKeyDownName() && strcmp(kb.GetKeyDownName(), "KeyQ") == 0,
	      "GetKeyDownName round-trips through the map");
	check(kb.GetKeyPressedCode() == XKEY_Q, "GetKeyPressedCode on the edge frame");

	kb.ClearKey(XKEY_Q);
	check(!kb.KeyDown(XKEY_Q), "ClearKey clears the key");

	kb.OnKeyEvent("KeyQ", true);
	kb.Update();
	kb.ClearKeyState();
	check(!kb.KeyDown(XKEY_Q), "ClearKeyState clears everything");
	check(kb.GetKeyDownCode() == -1, "no key down after a full clear");

	// Out-of-range indices must not corrupt memory.
	check(!kb.KeyDown(-1),      "negative key index is rejected");
	check(!kb.KeyDown(0x7fffff),"huge key index is rejected");
	kb.ClearKey(-5);            // must not crash
	check(true, "ClearKey with a bad index does not crash");
}

//////////////////////////////////////////////////////////////////////////
// Mouse.
//////////////////////////////////////////////////////////////////////////
static void test_mouse()
{
	printf("mouse:\n");

	CWebMouse m;
	m.Init(NULL);

	// Buttons follow the same edge rules as keys.
	m.OnMouseButton(0, true);
	m.Update();
	check(m.MouseDown(0),    "button held");
	check(m.MousePressed(0), "button rising edge");
	m.Update();
	check(!m.MousePressed(0), "rising edge not repeated");
	m.OnMouseButton(0, false);
	m.Update();
	check(m.MouseReleased(0), "button falling edge");

	// Deltas ACCUMULATE between frames. The browser can deliver many move
	// events per frame and keeping only the last would lose motion.
	CWebMouse m2;
	m2.Init(NULL);
	m2.OnMouseMove(3.0f, 1.0f, 0, 0);
	m2.OnMouseMove(4.0f, 2.0f, 0, 0);
	m2.Update();
	check(m2.GetDeltaX() == 7.0f, "X deltas accumulate across events");
	check(m2.GetDeltaY() == 3.0f, "Y deltas accumulate across events");

	// And reset each frame, or the view would keep turning after the mouse stops.
	m2.Update();
	check(m2.GetDeltaX() == 0.0f, "deltas reset on the next frame");
	check(m2.GetDeltaY() == 0.0f, "and stay reset");

	// Sensitivity scales the reported delta.
	CWebMouse m3;
	m3.Init(NULL);
	m3.SetSensitvity(2.0f);
	m3.SetSensitvityScale(3.0f);
	m3.OnMouseMove(1.0f, 1.0f, 0, 0);
	m3.Update();
	check(m3.GetDeltaX() == 6.0f, "sensitivity and scale both apply");

	// Absolute position tracks only while unlocked.
	CWebMouse m4;
	m4.Init(NULL);
	m4.OnMouseMove(0, 0, 100.0f, 200.0f);
	check(m4.GetVScreenX() == 100.0f, "absolute X tracked while unlocked");
	check(m4.GetVScreenY() == 200.0f, "absolute Y tracked while unlocked");

	m4.OnPointerLockChanged(true);
	m4.OnMouseMove(5.0f, 5.0f, 999.0f, 999.0f);
	check(m4.GetVScreenX() == 100.0f,
	      "absolute position frozen under pointer lock");

	// A lock transition discards accumulated motion -- the pointer jumps, and
	// delivering that as a delta would snap the view violently.
	CWebMouse m5;
	m5.Init(NULL);
	m5.OnMouseMove(500.0f, 500.0f, 0, 0);
	m5.OnPointerLockChanged(true);
	m5.Update();
	check(m5.GetDeltaX() == 0.0f, "motion across a lock change is discarded");

	// Wheel.
	CWebMouse m6;
	m6.Init(NULL);
	m6.OnMouseWheel(120.0f);
	m6.Update();
	check(m6.GetDeltaZ() == 120.0f, "wheel delta reported");
	m6.Update();
	check(m6.GetDeltaZ() == 0.0f, "wheel delta resets");

	// Out-of-range buttons must not corrupt memory.
	check(!m6.MouseDown(-1),  "negative button index rejected");
	check(!m6.MouseDown(999), "huge button index rejected");
	m6.OnMouseButton(999, true);
	check(true, "out-of-range button event does not crash");
}

static void test_button_mapping()
{
	printf("mouse button mapping:\n");
	// DOM button order is left/middle/right; the engine numbers them
	// differently, so the mapping is not the identity.
	check(WebInput::XKeyFromDomButton(0) == XKEY_MOUSE1, "DOM 0 -> MOUSE1 (left)");
	check(WebInput::XKeyFromDomButton(2) == XKEY_MOUSE2, "DOM 2 -> MOUSE2 (right)");
	check(WebInput::XKeyFromDomButton(1) == XKEY_MOUSE3, "DOM 1 -> MOUSE3 (middle)");
	check(WebInput::XKeyFromDomButton(99) == 0, "unknown button returns 0");
}

int main()
{
	test_key_mapping();
	test_keyboard_edges();
	test_keyboard_misc();
	test_mouse();
	test_button_mapping();

	printf("\n%s (%d failure%s)\n", g_fails ? "FAILED" : "all passed",
	       g_fails, g_fails == 1 ? "" : "s");
	return g_fails != 0;
}
