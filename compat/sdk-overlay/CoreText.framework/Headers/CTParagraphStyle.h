/* TIGER SDK OVERLAY: <CoreText/CTParagraphStyle.h> — see CTDefines.h. */
#ifndef __CTPARAGRAPHSTYLE__
#define __CTPARAGRAPHSTYLE__

#include <CoreText/CTDefines.h>

typedef const struct __CTParagraphStyle* CTParagraphStyleRef;
typedef uint8_t CTTextAlignment;
typedef uint8_t CTLineBreakMode;
typedef uint8_t CTWritingDirection;
typedef uint32_t CTParagraphStyleSpecifier;

enum {
    kCTLineBreakByWordWrapping      = 0,
    kCTLineBreakByCharWrapping      = 1,
    kCTLineBreakByClipping          = 2,
    kCTLineBreakByTruncatingHead    = 3,
    kCTLineBreakByTruncatingTail    = 4,
    kCTLineBreakByTruncatingMiddle  = 5
};

enum {
    kCTLeftTextAlignment = 0, kCTRightTextAlignment = 1, kCTCenterTextAlignment = 2,
    kCTJustifiedTextAlignment = 3, kCTNaturalTextAlignment = 4
};

enum { kCTWritingDirectionNatural = -1, kCTWritingDirectionLeftToRight = 0, kCTWritingDirectionRightToLeft = 1 };

/* Tiger's numbering is the documented 10.5 numbering: both Tiger's and
 * Leopard's CTParagraphStyleGetValueForSpecifier bound the specifier at 13. */
enum {
    kCTParagraphStyleSpecifierAlignment              = 0,
    kCTParagraphStyleSpecifierFirstLineHeadIndent    = 1,
    kCTParagraphStyleSpecifierHeadIndent             = 2,
    kCTParagraphStyleSpecifierTailIndent             = 3,
    kCTParagraphStyleSpecifierTabStops               = 4,
    kCTParagraphStyleSpecifierDefaultTabInterval     = 5,
    kCTParagraphStyleSpecifierLineBreakMode          = 6,
    kCTParagraphStyleSpecifierLineHeightMultiple     = 7,
    kCTParagraphStyleSpecifierMaximumLineHeight      = 8,
    kCTParagraphStyleSpecifierMinimumLineHeight      = 9,
    kCTParagraphStyleSpecifierLineSpacing            = 10,
    kCTParagraphStyleSpecifierParagraphSpacing       = 11,
    kCTParagraphStyleSpecifierParagraphSpacingBefore = 12,
    kCTParagraphStyleSpecifierBaseWritingDirection   = 13,
    kCTParagraphStyleSpecifierCount                  = 14
};

typedef struct CTParagraphStyleSetting {
    CTParagraphStyleSpecifier spec;
    size_t valueSize;
    const void* value;
} CTParagraphStyleSetting;

CT_EXTERN CFTypeID CTParagraphStyleGetTypeID(void);
CT_EXTERN CTParagraphStyleRef CTParagraphStyleCreate(const CTParagraphStyleSetting* settings, CFIndex settingCount);
CT_EXTERN CTParagraphStyleRef CTParagraphStyleCreateCopy(CTParagraphStyleRef);
CT_EXTERN bool CTParagraphStyleGetValueForSpecifier(CTParagraphStyleRef, CTParagraphStyleSpecifier, size_t valueBufferSize, void* valueBuffer);

#endif /* __CTPARAGRAPHSTYLE__ */
