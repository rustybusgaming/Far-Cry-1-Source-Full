////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   validator.h
//  Description: The physics VALIDATOR_* macros for the LINUX/wasm build.
//
//  WHY THIS FILE EXISTS
//
//  CryPhysics/utils.h contains:
//
//      #if defined LINUX
//          #include "validator.h"
//      #else
//          ... the macro definitions ...
//      #endif
//
//  Like WinBase.h and Splash.h, this header belonged to Crytek's Linux build
//  and was never part of the released source drop, leaving the LINUX branch
//  dangling -- all 34 CryPhysics translation units failed on it.
//
//  And like Splash.h, no guesswork was required: the #else branch beside the
//  include IS the definition, and it is reproduced here verbatim. Only the
//  leading indentation has been removed.
//
//  These macros build the per-structure validate() method the physics entities
//  call through ENTITY_VALIDATE. They are only active when PHYSICS_EXPORTS is
//  defined -- i.e. when building CryPhysics itself. Every other module gets the
//  no-op stubs from Cry_Math.h instead; see the note in LinuxSpecific.h about
//  why PHYSICS_EXPORTS must not be defined platform-wide.
//
////////////////////////////////////////////////////////////////////////////

#ifndef _CRY_WEBPORT_PHYSICS_VALIDATOR_H_
#define _CRY_WEBPORT_PHYSICS_VALIDATOR_H_

//////////////////////////////////////////////////////////////////////////
// is_valid()
//
// utils.h guards these overloads with "#if !defined(LINUX)" and expects them
// from this header instead -- which is why the real validator.h had to exist
// on Linux at all, rather than the macros simply being repeated. The VALIDATOR
// macros below call is_valid(), so it has to be declared before them, and
// before physinterface.h expands them into its structs.
//
// Taken verbatim from the non-LINUX branch of utils.h (lines 229-235); only
// the leading indentation is removed.
//////////////////////////////////////////////////////////////////////////
// [webport] Order changed from the original: the scalar overloads are declared
// BEFORE the template. The template body calls is_valid(op|op), where op|op is
// a dot product yielding a float -- a non-dependent call that must be visible
// at the point of DEFINITION under two-phase lookup. In utils.h the template
// came first, which MSVC 7.1 accepted by deferring the lookup to instantiation.
// Only the order differs; the definitions are unchanged.
inline bool is_valid(float op) { return op*op>=0 && op*op<1E30f; }
inline bool is_valid(int op) { return true; }
inline bool is_valid(unsigned int op) { return true; }

template<class dtype> bool is_valid(const dtype &op) { return is_valid(op|op); }

//////////////////////////////////////////////////////////////////////////
// The validator macros
//////////////////////////////////////////////////////////////////////////
#define VALIDATOR_LOG(pLog,str) pLog->Log(str) //OutputDebugString(str)
#define VALIDATORS_START bool validate( const char *strSource, ILog *pLog, const vectorf &pt,\
	IPhysicsStreamer *pStreamer, void *param0, int param1, int param2 ) { bool res=true; char errmsg[256];
#define VALIDATOR(member) if (!is_unused(member) && !is_valid(member)) { \
	res=false; sprintf(errmsg,"\002%s: (%.50s @ %.1f,%.1f,%.1f) Validation Error: %s is invalid",strSource,\
		pStreamer->GetForeignName(param0,param1,param2),pt.x,pt.y,pt.z,#member); \
	VALIDATOR_LOG(pLog,errmsg); } 
#define VALIDATOR_NORM(member) if (!is_unused(member) && !(is_valid(member) && fabs_tpl((member|member)-1.0f)<0.01f)) { \
	res=false; sprintf(errmsg,"\002%s: (%.50s @ %.1f,%.1f,%.1f) Validation Error: %s is invalid or unnormalized",\
	strSource,pStreamer->GetForeignName(param0,param1,param2),pt.x,pt.y,pt.z,#member); VALIDATOR_LOG(pLog,errmsg); }
#define VALIDATOR_NORM_MSG(member,msg,member1) if (!is_unused(member) && !(is_valid(member) && fabs_tpl((member|member)-1.0f)<0.01f)) { \
	res=false; sprintf(errmsg,"\002%s: (%.50s @ %.1f,%.1f,%.1f) Validation Error: %s is invalid or unnormalized %s",\
	strSource,pStreamer->GetForeignName(param0,param1,param2),pt.x,pt.y,pt.z,#member,msg); \
	if (!is_unused(member1)) sprintf(errmsg+strlen(errmsg)," "#member1": %.1f,%.1f,%.1f",member1.x,member1.y,member1.z); \
	VALIDATOR_LOG(pLog,errmsg); }
#define VALIDATOR_RANGE(member,minval,maxval) if (!is_unused(member) && !(is_valid(member) && member>=minval && member<=maxval)) { \
	res=false; sprintf(errmsg,"\002%s: (%.50s @ %.1f,%.1f,%.1f) Validation Error: %s is invalid or out of range",\
	strSource,pStreamer->GetForeignName(param0,param1,param2),pt.x,pt.y,pt.z,#member); VALIDATOR_LOG(pLog,errmsg); }
#define VALIDATOR_RANGE2(member,minval,maxval) if (!is_unused(member) && !(is_valid(member) && member*member>=minval*minval && \
		member*member<=maxval*maxval)) { \
	res=false; sprintf(errmsg,"\002%s: (%.50s @ %.1f,%.1f,%.1f) Validation Error: %s is invalid or out of range",\
	strSource,pStreamer->GetForeignName(param0,param1,param2),pt.x,pt.y,pt.z,#member); VALIDATOR_LOG(pLog,errmsg); }
#define VALIDATORS_END return res; }

#define ENTITY_VALIDATE(strSource,pStructure) if (!pStructure->validate(strSource,m_pWorld->m_pLog,m_pos,\
	m_pWorld->m_pPhysicsStreamer,m_pForeignData,m_iForeignData,m_iForeignFlags)) { \
	if (m_pWorld->m_vars.bBreakOnValidation) DoBreak return 0; }
#define ENTITY_VALIDATE_ERRCODE(strSource,pStructure,iErrCode) if (!pStructure->validate(strSource,m_pWorld->m_pLog,m_pos, \
	m_pWorld->m_pPhysicsStreamer,m_pForeignData,m_iForeignData,m_iForeignFlags)) { \
	if (m_pWorld->m_vars.bBreakOnValidation) DoBreak return iErrCode; }

#endif // _CRY_WEBPORT_PHYSICS_VALIDATOR_H_
