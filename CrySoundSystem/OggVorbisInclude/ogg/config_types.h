/****************************************************************************
 *
 *  CryEngine web port
 *  ---------------------------------------------------------------------
 *  ogg/config_types.h
 *
 *  WHY THIS FILE EXISTS
 *
 *  libogg does not ship this header -- its build system GENERATES it, filling
 *  in whichever native types happen to be the right widths on the target.
 *  ogg/os_types.h reaches it through a chain of #ifdefs whose final #else is
 *
 *      #include <sys/types.h>
 *      #include <ogg/config_types.h>
 *
 *  The Windows build never got that far: it matched an earlier branch that
 *  hardcodes the typedefs with MSVC's __int16 / __int32 / __int64. Every other
 *  platform lands in the #else, so the file is simply absent from this tree.
 *
 *  Writing it from <stdint.h> is both correct and better than the hardcoded
 *  branches: the fixed-width types are exact by definition, on LP64 Linux and
 *  on wasm32 alike, where "long" would not be.
 *
 ****************************************************************************/

#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

#include <stdint.h>

typedef int16_t  ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t  ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t  ogg_int64_t;

#endif /* __CONFIG_TYPES_H__ */
