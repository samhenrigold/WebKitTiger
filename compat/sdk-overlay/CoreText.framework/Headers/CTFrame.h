/* TIGER SDK OVERLAY: <CoreText/CTFrame.h> — see CTDefines.h. */
#ifndef __CTFRAME__
#define __CTFRAME__
#include <CoreText/CTDefines.h>
typedef const struct __CTFrame* CTFrameRef;
CT_EXTERN const CFStringRef kCTFrameProgressionAttributeName;
/* Ours: Tiger's CoreText has no such key. */
CT_EXTERN const CFStringRef kCTFrameMaximumNumberOfLinesAttributeName;
CT_EXTERN CFTypeID CTFrameGetTypeID(void);
CT_EXTERN CFRange CTFrameGetStringRange(CTFrameRef);
CT_EXTERN CFRange CTFrameGetVisibleStringRange(CTFrameRef);
CT_EXTERN CGPathRef CTFrameGetPath(CTFrameRef);
CT_EXTERN CFDictionaryRef CTFrameGetFrameAttributes(CTFrameRef);
CT_EXTERN CFArrayRef CTFrameGetLines(CTFrameRef);
CT_EXTERN void CTFrameDraw(CTFrameRef, CGContextRef);
/* Ours. */
CT_EXTERN void CTFrameGetLineOrigins(CTFrameRef, CFRange, CGPoint origins[]);
#endif /* __CTFRAME__ */
