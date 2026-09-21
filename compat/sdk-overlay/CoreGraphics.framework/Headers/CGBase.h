/* CoreGraphics - CGBase.h
 * Copyright (c) 2000 Apple Computer, Inc.
 * All rights reserved.
 */

#ifndef CGBASE_H_
#define CGBASE_H_

#include <stdbool.h>
#include <stddef.h>
#include <AvailabilityMacros.h>

#ifdef __cplusplus
#  define CG_EXTERN_C_BEGIN extern "C" {
#  define CG_EXTERN_C_END   }
#else
#  define CG_EXTERN_C_BEGIN
#  define CG_EXTERN_C_END
#endif

CG_EXTERN_C_BEGIN

#if defined(__WIN32__)
#  if defined(CG_BUILDING_CG)
#    define CG_EXTERN __declspec(dllexport) extern
#  else
#    define CG_EXTERN __declspec(dllimport) extern
#  endif
#  if defined(CG_DEBUG)
#    define CG_PRIVATE_EXTERN CG_EXTERN
#  else
#    define CG_PRIVATE_EXTERN extern
#  endif
#endif

#if !defined(CG_EXTERN)
#  define CG_EXTERN extern
#endif

#if !defined(CG_PRIVATE_EXTERN)
#  define CG_PRIVATE_EXTERN __private_extern__
#endif

#if !defined(CG_OBSOLETE)
#  if defined(__GNUC__) && (__GNUC__ >= 3) && (__GNUC_MINOR__ >= 1)
#    define CG_OBSOLETE __attribute__((deprecated))
#  else
#    define CG_OBSOLETE
#  endif
#endif

#if !defined(CG_INLINE)
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    define CG_INLINE static inline
#  elif defined(__MWERKS__) || defined(__cplusplus)
#    define CG_INLINE static inline
#  elif defined(__GNUC__)
#    define CG_INLINE static __inline__
#  else
#    define CG_INLINE static    
#  endif
#endif

#if !defined(__GNUC__) && !defined(__MWERKS__)
#  define __attribute__(attribute)
#endif

CG_EXTERN_C_END


/* TIGER SDK OVERLAY: CGFloat arrived in the 10.5 SDK. On i386 CoreGraphics is
   float-based throughout, so this typedef makes a modern CGFloat prototype
   ABI-identical to the one Tiger's CoreGraphics actually exports. Apple's own
   guard macro is used so that anything else defining CGFloat defers to this.

   (Tiger's CoreText is the exception and does NOT follow CGBase: every by-value
   scalar it takes is a double. See compat/CT-SURVEY.md.) */
#include <float.h>

#ifndef CGFLOAT_DEFINED
#define CGFLOAT_DEFINED 1
typedef float CGFloat;
#define CGFLOAT_MIN     FLT_MIN
#define CGFLOAT_MAX     FLT_MAX
#define CGFLOAT_EPSILON FLT_EPSILON
#define CGFLOAT_IS_DOUBLE 0
#define CGFLOAT_DEFINED 1
#endif

#endif	/* CGBASE_H_ */
