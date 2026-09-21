/* TIGER SDK OVERLAY: <CoreText/CTLine.h> — see CTDefines.h. */
#ifndef __CTLINE__
#define __CTLINE__

#include <CoreText/CTDefines.h>
#include <CoreText/CTFont.h>

typedef const struct __CTLine* CTLineRef;
typedef CFOptionFlags CTLineBoundsOptions;
typedef uint32_t CTLineTruncationType;

enum {
    kCTLineBoundsExcludeTypographicLeading = (1 << 0),
    kCTLineBoundsExcludeTypographicShifts  = (1 << 1),
    kCTLineBoundsUseHangingPunctuation     = (1 << 2),
    kCTLineBoundsUseGlyphPathBounds        = (1 << 3),
    kCTLineBoundsUseOpticalBounds          = (1 << 4)
};

enum { kCTLineTruncationStart = 0, kCTLineTruncationEnd = 1, kCTLineTruncationMiddle = 2 };

CT_EXTERN CFTypeID CTLineGetTypeID(void);

/* --- case 1 ------------------------------------------------------------- */
CT_EXTERN CTLineRef CTLineCreateWithAttributedString(CFAttributedStringRef);
CT_EXTERN CFArrayRef CTLineGetGlyphRuns(CTLineRef);
CT_EXTERN CFIndex CTLineGetGlyphCount(CTLineRef);
CT_EXTERN CFRange CTLineGetStringRange(CTLineRef);

/* --- case 2: Tiger reads the width as a by-value double ------------------ */
CT_EXTERN CTLineRef CTLineCreateTruncatedLine(CTLineRef, double width, CTLineTruncationType, CTLineRef truncationToken);

/* Tiger matches the modern declaration here: the factor is a CGFloat and only
 * the width is a double, exactly as Apple's own header spells it. */
CT_EXTERN CTLineRef CTLineCreateJustifiedLine(CTLineRef, CGFloat justificationFactor, double justificationWidth);
CT_EXTERN double CTLineGetPenOffsetForFlush(CTLineRef, CGFloat flushFactor, double flushWidth);

/* --- case 3a: Tiger's takes a CFRange / returns a constant --------------- */
CT_EXTERN double CTLineGetTypographicBounds(CTLineRef, CGFloat* ascent, CGFloat* descent, CGFloat* leading)
    CT_TIGER_ADAPTER(TigerCTLineGetTypographicBounds);
CT_EXTERN CGRect CTLineGetImageBounds(CTLineRef, CGContextRef)
    CT_TIGER_ADAPTER(TigerCTLineGetImageBounds);
/* Tiger's CTLineDraw takes a CFRange; the modern two-argument call would pass
 * stack junk as the range and draw nothing whenever it exceeds the glyph count. */
CT_EXTERN void CTLineDraw(CTLineRef, CGContextRef)
    CT_TIGER_ADAPTER(TigerCTLineDraw);

/* --- case 3b ------------------------------------------------------------- */
CT_EXTERN CGRect CTLineGetBoundsWithOptions(CTLineRef, CTLineBoundsOptions);
CT_EXTERN double CTLineGetTrailingWhitespaceWidth(CTLineRef);
CT_EXTERN CTLineRef CTLineCreateWithUniCharProvider(const UniChar* (*provider)(CFIndex, CFIndex*, CFDictionaryRef*, void*), void (*dispose)(const UniChar*, void*), void* refCon);

#endif /* __CTLINE__ */
