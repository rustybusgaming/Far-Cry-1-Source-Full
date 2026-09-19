#ifndef _CRY_GLES_STATE_H_
#define _CRY_GLES_STATE_H_

/*!
	GLESState -- applying the engine's GS_* render state to GL.

	THE GAP THIS CLOSES

	CRenderer::EF_SetState does one thing: m_CurState = st. That is Crytek's
	own null-renderer implementation, which this backend inherits, and nothing
	downstream of it ever touched GL. So every call the engine makes --
	CRESky asking for GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA, CryFont
	asking for alpha blending to draw text, anything asking not to write depth
	-- was recorded and silently dropped.

	The visible symptom is that everything draws opaque. Text has black boxes
	around it, the sky is a wall, and nothing that should be see-through is.
	It looks like a blending bug in whatever drew it, which is the wrong place
	to look.

	WHY THE DECODER IS NOT HERE

	Common/CryStateGen.cpp decodes the word, and it is shared with the WebGPU
	backend and unit-tested natively without a GL context. This file is only the
	half that cannot be tested that way: turning neutral enums into GL calls,
	one line each.

	That split is the same one the shader generators use, and for the same
	reason -- it puts every decision somewhere it can be checked.

	REDUNDANT CALLS

	The engine sets state per draw, and most draws in a row want the same
	state. Each GL call here crosses into JavaScript, so the last applied word
	is remembered and an unchanged one costs a single comparison.
*/

#if defined(__EMSCRIPTEN__)

//! Apply one GS_* word. Cheap when it has not changed since the last call.
void GLESState_Apply(int nRenderState);

//! Forget what was last applied, so the next Apply sets everything.
//!
//! Needed wherever something else has touched the same GL state behind this
//! file's back -- the frame's own setup, the conformance run -- or the cache
//! would claim a state is already current when the context disagrees.
void GLESState_Invalidate();

//! The word most recently applied. For logging and for the tests.
int GLESState_Current();

#endif //__EMSCRIPTEN__

#endif //_CRY_GLES_STATE_H_
