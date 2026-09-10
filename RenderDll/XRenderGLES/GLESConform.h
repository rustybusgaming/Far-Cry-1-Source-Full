#ifndef _CRY_GLES_CONFORM_H_
#define _CRY_GLES_CONFORM_H_

/*!
	GLESConform -- running generated shaders through the real driver.

	The unit tests read the emitted source and check what it says. This
	compiles it, draws with it, and reads the pixel back, so a shader that does
	not compile fails and one that computes the wrong thing fails too.

	See the .cpp for what each case covers and why the expected colours are
	what they are.
*/

#if defined(__EMSCRIPTEN__)

//! Run every case. Returns true if all passed; fills in the counts either way.
//! Needs a current GL context and leaves the default framebuffer bound.
bool GLESConform_Run(int& nPassed, int& nTotal);

#endif //__EMSCRIPTEN__

#endif //_CRY_GLES_CONFORM_H_
