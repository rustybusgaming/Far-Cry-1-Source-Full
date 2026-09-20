#ifndef _CRY_ASSET_ROOT_H_
#define _CRY_ASSET_ROOT_H_

/*!
	CryAssetRoot -- pointing the engine at a Far Cry installation.

	WHY THIS IS NOT A LOADER

	The engine already knows how to read its own data. CryPak opens .pak
	archives and loose files through ordinary stdio, using paths relative to
	the working directory: "scripts/main.lua", "languages/fonts/default.xml",
	"FCData/Localized/english.pak".

	So nothing here parses, unpacks or converts anything. All it does is put
	the engine's working directory where the data is, and say clearly whether
	the data is there.

	WHY IT MATTERS THAT THIS IS A RUNTIME CHOICE

	Far Cry's assets are several GB and are NOT redistributable. They cannot
	live in this repository, they cannot ship in a build artifact, and they
	cannot be committed by someone who owns the game -- owning a copy licenses
	playing it, not publishing it.

	Every route into the engine therefore has to start from a path the user
	supplies at run time, pointing at their own installation. That is the whole
	design constraint, and it is why this is a host concern rather than a build
	one: nothing about the data can be baked in.

	NATIVE AND BROWSER

	Natively this is a directory: "--data /path/to/FarCry". In a browser there
	is no filesystem to point at, so the user picks their folder and the host
	writes it into the Emscripten filesystem first, then sets the root to where
	it was written. The difference is confined to Web/WebAssets.cpp; everything
	below is shared, which is what makes the awkward half testable without a
	browser.
*/

//! What the engine will find once it starts.
struct SCryAssetReport
{
	bool	bRootSet;		//!< a root was given and could be entered
	bool	bHasScripts;	//!< scripts/ or a .pak that should contain it
	bool	bHasFonts;		//!< languages/fonts/default.xml
	bool	bHasFCData;		//!< FCData/
	bool	bHasLevels;		//!< Levels/ or FCData/Levels/

	SCryAssetReport()
		: bRootSet(false), bHasScripts(false), bHasFonts(false)
		, bHasFCData(false), bHasLevels(false) {}

	//! Enough for the engine to get past script loading, which is where it
	//! stops with nothing.
	bool CanBoot() const { return bRootSet && bHasScripts; }
};

//! Point the engine at szPath by making it the working directory.
//!
//! Returns false if the path cannot be entered, having said why. A null or
//! empty path is not an error: it means "no data", which is a legitimate state
//! this port has always run in.
bool CryAssetRoot_Set(const char* szPath);

//! What is actually present under the current root.
//!
//! Deliberately reports rather than decides. A missing font is survivable and
//! a missing script directory is not, but that judgement belongs to the caller
//! and to the person reading the log, not to a bool buried in here.
void CryAssetRoot_Inspect(SCryAssetReport& out);

//! Log the report in the form someone can act on: what was looked for, where,
//! and what to do about each thing that is missing.
void CryAssetRoot_LogReport(const SCryAssetReport& report);

//! Pull "--data <path>" out of an argv, returning the path or null.
//!
//! Removes nothing: the engine parses the same command line afterwards and is
//! content to ignore a switch it does not know.
const char* CryAssetRoot_ArgValue(int argc, char** argv, const char* szFlag);

#endif //_CRY_ASSET_ROOT_H_
