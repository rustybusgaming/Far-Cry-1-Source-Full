////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   CryAssetRoot.cpp
//  Description: Pointing the engine at a Far Cry installation. See the header.
//
////////////////////////////////////////////////////////////////////////////

#include "CryAssetRoot.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

//////////////////////////////////////////////////////////////////////////

static bool Exists(const char* szPath)
{
	struct stat st;
	return szPath && stat(szPath, &st) == 0;
}

static bool IsDir(const char* szPath)
{
	struct stat st;
	return szPath && stat(szPath, &st) == 0 && S_ISDIR(st.st_mode);
}

//////////////////////////////////////////////////////////////////////////
//! Case-insensitive existence check.
//!
//! The engine does this itself -- CCryPak lowercases a path and then recovers
//! the real spelling by scanning the directory -- but that machinery is not
//! available before the engine exists, and this runs first. A Far Cry install
//! may spell it FCData, fcdata or FCDATA depending on how it was unpacked, and
//! reporting "missing" for a directory that is plainly there would send
//! someone looking in the wrong place.
//////////////////////////////////////////////////////////////////////////
static bool ExistsAnyCase(const char* szRelPath)
{
	if (Exists(szRelPath))
		return true;

	char buf[512];
	strncpy(buf, szRelPath, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;

	// All lower.
	for (char* p = buf; *p; ++p)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	if (Exists(buf))
		return true;

	// All upper.
	strncpy(buf, szRelPath, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	for (char* p = buf; *p; ++p)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');

	return Exists(buf);
}

//////////////////////////////////////////////////////////////////////////

bool CryAssetRoot_Set(const char* szPath)
{
	if (!szPath || !szPath[0])
		return true;		// no data is a state, not a failure

	if (!IsDir(szPath))
	{
		fprintf(stderr, "[assets] '%s' is not a directory\n", szPath);
		return false;
	}

	if (chdir(szPath) != 0)
	{
		fprintf(stderr, "[assets] cannot enter '%s'\n", szPath);
		return false;
	}

	char cwd[1024];
	if (getcwd(cwd, sizeof(cwd)))
		printf("[assets] root: %s\n", cwd);

	return true;
}

//////////////////////////////////////////////////////////////////////////

void CryAssetRoot_Inspect(SCryAssetReport& out)
{
	out = SCryAssetReport();

	char cwd[1024];
	out.bRootSet = getcwd(cwd, sizeof(cwd)) != 0;

	// Scripts arrive either loose or inside an archive, depending on the
	// installation. Either satisfies the engine, so either satisfies this.
	out.bHasScripts = ExistsAnyCase("scripts")
	               || ExistsAnyCase("FCData/Scripts.pak")
	               || ExistsAnyCase("Scripts.pak");

	out.bHasFonts   = ExistsAnyCase("languages/fonts/default.xml");
	out.bHasFCData  = ExistsAnyCase("FCData");
	out.bHasLevels  = ExistsAnyCase("Levels") || ExistsAnyCase("FCData/Levels");
}

//////////////////////////////////////////////////////////////////////////

void CryAssetRoot_LogReport(const SCryAssetReport& r)
{
	printf("[assets] scripts %s   fonts %s   FCData %s   Levels %s\n",
	       r.bHasScripts ? "yes" : "NO ",
	       r.bHasFonts   ? "yes" : "NO ",
	       r.bHasFCData  ? "yes" : "NO ",
	       r.bHasLevels  ? "yes" : "NO ");

	if (r.CanBoot())
		return;

	// The one thing worth being explicit about, because the engine's own
	// error for it -- "you're probably running from the wrong working folder"
	// -- is the right diagnosis on a desktop install and the wrong one here.
	printf("[assets] no game data found.\n"
	       "[assets] The engine will start and render, but it has no scripts,\n"
	       "[assets] no fonts and no level -- so there is nothing to show.\n"
	       "[assets] Point it at your own Far Cry installation: the folder\n"
	       "[assets] containing FCData, and NOT a copy placed in this\n"
	       "[assets] repository. Far Cry's assets are not redistributable;\n"
	       "[assets] owning the game licenses playing it, not publishing it.\n");
}

//////////////////////////////////////////////////////////////////////////

const char* CryAssetRoot_ArgValue(int argc, char** argv, const char* szFlag)
{
	if (!argv || !szFlag)
		return 0;

	for (int i = 1; i < argc - 1; ++i)
	{
		if (argv[i] && strcmp(argv[i], szFlag) == 0)
			return argv[i + 1];
	}

	return 0;
}
