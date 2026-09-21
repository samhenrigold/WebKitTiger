/* TIGER: the Objective-C prelude. Force-included into every Objective-C
   translation unit by toolchain/tiger.cmake, ahead of everything else.

   Two jobs:

   1. Define the annotation macros and the YES/NO spelling that the 10.4u SDK's
      own Foundation headers, and WebKit's, are parsed against. These MUST be in
      place before <Foundation/Foundation.h>, which is why this is a forced
      include rather than something WebKit sources import.

   2. Pull in <TigerCompat/NSCompat.h>, the Foundation compatibility layer, plus
      the few pieces it does not cover.

   What this cannot fix: Objective-C lightweight generics. NSArray and friends in
   the 10.4 SDK are not declared with a __covariant type parameter, and a
   category cannot add one, so "NSArray<NSString *> *" still does not parse.
   That needs the SDK's own Foundation headers changed. */
#ifndef TIGERCOMPAT_FOUNDATIONCOMPAT_H
#define TIGERCOMPAT_FOUNDATIONCOMPAT_H

#ifdef __OBJC__

#import <objc/objc.h>

/* TIGER: <objc/objc.h> defines YES as (BOOL)1. Clang's Objective-C literals
   expand @YES to "@" followed by the macro, which then parses as the boxed
   expression @(BOOL)1 and fails -- so every @YES and @NO in WebKit is a syntax
   error against this SDK. The 10.8+ SDK defines YES as __objc_yes for exactly
   this reason; do the same, before Foundation is parsed. */
#if __has_feature(objc_bool)
#undef YES
#undef NO
#define YES __objc_yes
#define NO  __objc_no
#endif

/* ---- Annotation macros from <Foundation/NSObjCRuntime.h>, 10.5 onward ---

   All compile-time annotations only: availability, nullability, Swift naming
   and ARC ownership. None changes generated code for us, so each expands to
   nothing. Defined before Foundation so the SDK's own headers see them too. */

#ifndef NS_AVAILABLE
#define NS_AVAILABLE(_mac, _ios)
#define NS_AVAILABLE_MAC(_mac)
#define NS_AVAILABLE_IOS(_ios)
#define NS_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep)
#define NS_DEPRECATED_MAC(_macIntro, _macDep)
#define NS_DEPRECATED_IOS(_iosIntro, _iosDep)
#define NS_CLASS_AVAILABLE(_mac, _ios)
#define NS_CLASS_AVAILABLE_MAC(_mac)
#define NS_CLASS_AVAILABLE_IOS(_ios)
#define NS_CLASS_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep)
#define NS_UNAVAILABLE
#define NS_ROOT_CLASS
#define NS_REQUIRES_NIL_TERMINATION
#define NS_RETURNS_INNER_POINTER
#define NS_RETURNS_RETAINED
#define NS_RETURNS_NOT_RETAINED
#define NS_CONSUMED
#define NS_CONSUMES_SELF
#define NS_RETAINED
#define NS_DESIGNATED_INITIALIZER
#define NS_REFINED_FOR_SWIFT
#define NS_SWIFT_NAME(_name)
#define NS_SWIFT_UNAVAILABLE(_msg)
#define NS_SWIFT_NOTHROW
#define NS_NOESCAPE
#define NS_FORMAT_FUNCTION(F, A)
#define NS_FORMAT_ARGUMENT(A)
#endif

/* Nullability, 10.10. clang understands the underscore forms, so map onto
   those rather than dropping them. */
#ifndef NS_ASSUME_NONNULL_BEGIN
#if __has_feature(nullability)
#define NS_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define NS_ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")
#else
#define NS_ASSUME_NONNULL_BEGIN
#define NS_ASSUME_NONNULL_END
#define nullable
#define nonnull
#define null_unspecified
#define __nullable
#define __nonnull
#define __null_unspecified
#endif
#endif

/* NS_ENUM / NS_OPTIONS, 10.8. */
#ifndef NS_ENUM
#if __has_feature(objc_fixed_enum)
#define NS_ENUM(_type, _name) enum _name : _type _name; enum _name : _type
#define NS_OPTIONS(_type, _name) enum _name : _type _name; enum _name : _type
#else
#define NS_ENUM(_type, _name) _type _name; enum
#define NS_OPTIONS(_type, _name) _type _name; enum
#endif
#endif

#import <Foundation/Foundation.h>

/* The Foundation compatibility layer proper: NSInteger, NSUUID, NSMapTable,
   fast enumeration, subscripting, blocks-based enumeration and the rest.
   Owned by the ObjC-runtime track; see compat/NSCOMPAT-SURVEY.md. */
#import <TigerCompat/NSCompat.h>

/* ---- What NSCompat.h does not cover ------------------------------------- */

/* NSFileManagerDelegate, 10.5. Tiger uses informal delegate categories, so this
   is a declaration only, for the one WTF class that adopts the protocol. */
@protocol NSFileManagerDelegate <NSObject>
@end

/* NSCompat.h declares these as methods. WebKit reaches them with dot syntax on
   an `id`, which only resolves through a declared @property, so redeclare. */
@interface NSLocale (TigerCompatProperties)
@property (nonatomic, readonly) NSString *languageCode;
@property (nonatomic, readonly) NSString *scriptCode;
@property (nonatomic, readonly) NSString *countryCode;
@end

#endif /* __OBJC__ */

#endif /* TIGERCOMPAT_FOUNDATIONCOMPAT_H */
