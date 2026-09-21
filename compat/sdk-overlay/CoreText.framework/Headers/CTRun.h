/* TIGER SDK OVERLAY: <CoreText/CTRun.h> — see CTDefines.h. */
#ifndef __CTRUN__
#define __CTRUN__

#include <CoreText/CTDefines.h>
#include <CoreText/CTFont.h>

typedef const struct __CTRun* CTRunRef;
typedef CFIndex CTRunStatus;

enum {
    kCTRunStatusNoStatus     = 0,
    kCTRunStatusRightToLeft  = (1 << 0),
    kCTRunStatusNonMonotonic = (1 << 1),
    kCTRunStatusHasNonIdentityMatrix = (1 << 2)
};

CT_EXTERN CFTypeID CTRunGetTypeID(void);

/* --- case 1 ------------------------------------------------------------- */
CT_EXTERN CFIndex CTRunGetGlyphCount(CTRunRef);
CT_EXTERN CFDictionaryRef CTRunGetAttributes(CTRunRef);
CT_EXTERN CTRunStatus CTRunGetStatus(CTRunRef);
CT_EXTERN CFRange CTRunGetStringRange(CTRunRef);
CT_EXTERN const CGGlyph* CTRunGetGlyphsPtr(CTRunRef);
CT_EXTERN const CGSize* CTRunGetAdvancesPtr(CTRunRef);
CT_EXTERN const CFIndex* CTRunGetStringIndicesPtr(CTRunRef);
CT_EXTERN double CTRunGetTypographicBounds(CTRunRef, CFRange, CGFloat* ascent, CGFloat* descent, CGFloat* leading);
CT_EXTERN CGRect CTRunGetImageBounds(CTRunRef, CGContextRef, CFRange);

/* --- case 3a: Tiger exports these but their bodies are `xor eax, eax; ret`
 * Only the Ptr variants above return data, so the adapters copy out of those. */
CT_EXTERN void CTRunGetGlyphs(CTRunRef, CFRange, CGGlyph buffer[])
    CT_TIGER_ADAPTER(TigerCTRunGetGlyphs);
CT_EXTERN void CTRunGetAdvances(CTRunRef, CFRange, CGSize buffer[])
    CT_TIGER_ADAPTER(TigerCTRunGetAdvances);
CT_EXTERN void CTRunGetStringIndices(CTRunRef, CFRange, CFIndex buffer[])
    CT_TIGER_ADAPTER(TigerCTRunGetStringIndices);
CT_EXTERN void CTRunDraw(CTRunRef, CGContextRef, CFRange)
    CT_TIGER_ADAPTER(TigerCTRunDraw);

/* --- case 3b ------------------------------------------------------------- */
CT_EXTERN CGSize CTRunGetInitialAdvance(CTRunRef);
CT_EXTERN void CTRunGetBaseAdvancesAndOrigins(CTRunRef, CFRange, CGSize baseAdvances[], CGPoint origins[]);

#endif /* __CTRUN__ */
