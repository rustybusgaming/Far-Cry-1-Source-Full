#ifndef _CRY_WEB_ASSETS_H_
#define _CRY_WEB_ASSETS_H_

/*!
	WebAssets -- letting someone run the port against their own copy of the game.

	THE CONSTRAINT THIS IS BUILT AROUND

	Far Cry's data is several GB and is not redistributable. It cannot be
	committed to this repository, baked into a build, or shipped in a CI
	artifact -- and owning the game does not change that: a licence to play is
	not a licence to publish.

	So the data has to arrive at run time, from the machine of whoever is
	running it, and it must never travel anywhere. Nothing here uploads, and
	there is no server involved: the files are read in the browser, written
	into the Emscripten filesystem, and stay in that tab.

	HOW IT WORKS

	A folder picker. The user chooses their Far Cry directory, the browser
	hands us File objects with their relative paths intact, and each is written
	into MEMFS at the same path. CryAssetRoot then makes that the working
	directory and the engine reads it exactly as it would a local install --
	because to the engine it IS one.

	WHAT LIMITS IT, HONESTLY

	MEMFS is memory. A whole Far Cry installation will not fit and this makes
	no attempt to pretend otherwise: there is a budget, it is reported before
	anything is copied, and going over it is refused rather than surviving
	until an allocation fails somewhere unrelated.

	What fits is the part that matters first: scripts, fonts and configuration
	are tens of megabytes, and they are what stands between the engine booting
	and the engine getting past script loading. A full level is the next
	problem and needs a different mechanism -- reading ranges out of a File
	without copying it -- which needs synchronous reads the main thread does
	not have.

	WHY THE HOST WAITS FOR THIS

	Picking a folder is asynchronous and CreateSystemInterface is not. There is
	no point inside engine startup where a file dialog can be awaited, so the
	choice happens BEFORE the engine exists -- the same inversion the WebGPU
	device acquisition needed, for the same reason.
*/

#if defined(__EMSCRIPTEN__)

enum EWebAssetsState
{
	eWebAssets_Waiting = 0,	//!< the picker is up; nothing decided yet
	eWebAssets_Ready,		//!< files were written; Root() is where they went
	eWebAssets_Skipped,		//!< the user chose to continue without data
};

//! Put the picker up. Returns immediately; the page drives the rest.
void WebAssets_Begin();

//! Where the choice stands. The host must not create the engine while this is
//! eWebAssets_Waiting.
EWebAssetsState WebAssets_State();

//! The directory the files were written to, or "" when none were.
const char* WebAssets_Root();

//! How many bytes were written, for the log.
double WebAssets_BytesWritten();

#endif //__EMSCRIPTEN__

#endif //_CRY_WEB_ASSETS_H_
