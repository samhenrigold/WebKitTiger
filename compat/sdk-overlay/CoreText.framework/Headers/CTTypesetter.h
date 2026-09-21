/* TIGER SDK OVERLAY: <CoreText/CTTypesetter.h> — see CTDefines.h. */
#ifndef __CTTYPESETTER__
#define __CTTYPESETTER__
#include <CoreText/CTDefines.h>
#include <CoreText/CTLine.h>
typedef const struct __CTTypesetter* CTTypesetterRef;
typedef const UniChar* (*CTUniCharProviderCallback)(CFIndex stringIndex, CFIndex* charCount, CFDictionaryRef* attributes, void* refCon);
typedef void (*CTUniCharDisposeCallback)(const UniChar* chars, void* refCon);
/* Ours: Tiger's CoreText has no such key. */
CT_EXTERN const CFStringRef kCTTypesetterOptionForcedEmbeddingLevel;
CT_EXTERN CFTypeID CTTypesetterGetTypeID(void);
CT_EXTERN CTTypesetterRef CTTypesetterCreateWithAttributedString(CFAttributedStringRef);
CT_EXTERN CTTypesetterRef CTTypesetterCreateWithUniCharProvider(CTUniCharProviderCallback, CTUniCharDisposeCallback, void* refCon);
CT_EXTERN CTLineRef CTTypesetterCreateLine(CTTypesetterRef, CFRange stringRange);
/* Tiger reads the width as a by-value double, which is what the modern
 * declaration says too, so these three need no adaptation. */
CT_EXTERN CFIndex CTTypesetterSuggestLineBreak(CTTypesetterRef, CFIndex startIndex, double width);
CT_EXTERN CFIndex CTTypesetterSuggestClusterBreak(CTTypesetterRef, CFIndex startIndex, double width);
CT_EXTERN CFIndex CTTypesetterSuggestCharacterBreak(CTTypesetterRef, CFIndex startIndex, double width);
/* Ours: Tiger's typesetter takes no options dictionary. */
CT_EXTERN CTTypesetterRef CTTypesetterCreateWithUniCharProviderAndOptions(CTUniCharProviderCallback, CTUniCharDisposeCallback, void* refCon, CFDictionaryRef options);
#endif /* __CTTYPESETTER__ */
