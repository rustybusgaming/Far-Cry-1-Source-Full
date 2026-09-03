////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   Splash.h
//  Description: Water splash effect types.
//
//  WHY THIS FILE EXISTS
//
//  IRenderer.h and I3DEngine.h both contain:
//
//      #if defined(LINUX)
//          #include "Splash.h"
//      #else
//          enum eSplashType { EST_Water, };
//      #endif
//
//  Like WinBase.h, this header belonged to Crytek's Linux build and was never
//  part of the released source drop, leaving the LINUX branch dangling.
//
//  Unlike WinBase.h, no reconstruction guesswork is needed: the #else branch
//  is the definition, and it is reproduced verbatim below. Hoisting it into a
//  shared header is presumably why the split existed at all -- two headers
//  declaring the same enum in the non-LINUX path would collide if both were
//  included, which the LINUX path avoids via this header's include guard.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_COMMON_SPLASH_HDR_
#define _CRY_COMMON_SPLASH_HDR_

enum eSplashType
{
	EST_Water,
};

#endif // _CRY_COMMON_SPLASH_HDR_
