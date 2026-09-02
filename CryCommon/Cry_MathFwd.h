////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   Cry_MathFwd.h
//  Description: Forward declarations for the math template cluster.
//
//  The math headers -- Cry_Vector2/3, Cry_Quat, Cry_Matrix -- are mutually
//  recursive: each one names the others' templates in converting constructors
//  and operators. The cycle is broken by Cry_Math.h, which declares all of
//  them up front and only then includes the cluster.
//
//  The consequence is that the cluster is only well-formed when it is entered
//  through Cry_Math.h. Including Cry_Vector3.h directly -- which plenty of
//  engine headers do -- starts the cycle at an arbitrary point, and whichever
//  header lands first references templates that have not been declared yet.
//
//  MSVC 7.1 tolerated this: it deferred parsing of template member
//  declarations until instantiation, by which time every template was known.
//  Clang resolves names at definition time and rejects it, which is what
//  made a single construct in Cry_Vector2.h account for 88 failing TUs.
//
//  This header carries the declarations (and ILINE, which the cluster also
//  assumes) so that every member of the cluster can include it first and be
//  parseable standalone. It deliberately contains declarations only -- no
//  definitions -- so it stays cycle-free and costs nothing to include.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_COMMON_MATH_FWD_HDR_
#define _CRY_COMMON_MATH_FWD_HDR_

// Matches the definition in Cry_Math.h for the LINUX/wasm target. Repeating an
// identical object-like macro is well-defined, so this stays compatible no
// matter which of the two headers is reached first.
#if defined(LINUX)
#	ifndef ILINE
#		define ILINE inline
#	endif
#endif

template <class F> struct Vec2_tpl;
template <class F> struct Vec3_tpl;
template <class F> struct Ang3_tpl;
template <class F> struct AngleAxis_tpl;
template <class F> struct Quaternion_tpl;
template <class F> struct Plane_tpl;

template <class F> struct Matrix33diag_tpl;
template <class F, int SI, int SJ> struct Matrix33_tpl;
template <class F> struct Matrix34_tpl;
template <class F, int SI, int SJ> struct Matrix44_tpl;

#endif // _CRY_COMMON_MATH_FWD_HDR_
