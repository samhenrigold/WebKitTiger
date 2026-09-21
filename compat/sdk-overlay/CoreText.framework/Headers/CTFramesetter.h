/* TIGER SDK OVERLAY: <CoreText/CTFramesetter.h> — see CTDefines.h. */
#ifndef __CTFRAMESETTER__
#define __CTFRAMESETTER__
#include <CoreText/CTDefines.h>
#include <CoreText/CTFrame.h>
#include <CoreText/CTTypesetter.h>
typedef const struct __CTFramesetter* CTFramesetterRef;
CT_EXTERN CFTypeID CTFramesetterGetTypeID(void);
CT_EXTERN CTFramesetterRef CTFramesetterCreateWithAttributedString(CFAttributedStringRef);
CT_EXTERN CTFrameRef CTFramesetterCreateFrame(CTFramesetterRef, CFRange, CGPathRef, CFDictionaryRef frameAttributes);
CT_EXTERN CTTypesetterRef CTFramesetterGetTypesetter(CTFramesetterRef);
/* Ours. */
CT_EXTERN CGSize CTFramesetterSuggestFrameSizeWithConstraints(CTFramesetterRef, CFRange, CFDictionaryRef frameAttributes, CGSize constraints, CFRange* fitRange);
#endif /* __CTFRAMESETTER__ */
