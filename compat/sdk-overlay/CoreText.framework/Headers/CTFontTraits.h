/* TIGER SDK OVERLAY: <CoreText/CTFontTraits.h> — see CTDefines.h. */
#ifndef __CTFONTTRAITS__
#define __CTFONTTRAITS__

#include <CoreText/CTDefines.h>

/* Tiger exports these trait keys. */
CT_EXTERN const CFStringRef kCTFontSymbolicTrait;
CT_EXTERN const CFStringRef kCTFontWeightTrait;
CT_EXTERN const CFStringRef kCTFontWidthTrait;
CT_EXTERN const CFStringRef kCTFontSlantTrait;

/* Ours, from libtigercompat: Tiger's CoreText has no such key. */
CT_EXTERN const CFStringRef kCTFontGradeTrait;
CT_EXTERN const CFStringRef kCTFontUIFontDesignTrait;

typedef uint32_t CTFontSymbolicTraits;

enum {
    kCTFontTraitItalic          = (1 << 0),
    kCTFontTraitBold            = (1 << 1),
    kCTFontTraitExpanded        = (1 << 5),
    kCTFontTraitCondensed       = (1 << 6),
    kCTFontTraitMonoSpace       = (1 << 10),
    kCTFontTraitVertical        = (1 << 11),
    kCTFontTraitUIOptimized     = (1 << 12),
    kCTFontTraitColorGlyphs     = (1 << 13),
    kCTFontTraitComposite       = (1 << 14),
    kCTFontTraitClassMask       = 0xF0000000
};

/* The older `kCTFontXTrait` spelling, still used in FontCoreText.cpp. */
enum {
    kCTFontItalicTrait          = kCTFontTraitItalic,
    kCTFontBoldTrait            = kCTFontTraitBold,
    kCTFontExpandedTrait        = kCTFontTraitExpanded,
    kCTFontCondensedTrait       = kCTFontTraitCondensed,
    kCTFontMonoSpaceTrait       = kCTFontTraitMonoSpace,
    kCTFontVerticalTrait        = kCTFontTraitVertical,
    kCTFontUIOptimizedTrait     = kCTFontTraitUIOptimized,
    kCTFontColorGlyphsTrait     = kCTFontTraitColorGlyphs,
    kCTFontCompositeTrait       = kCTFontTraitComposite,
    kCTFontClassMaskTrait       = kCTFontTraitClassMask
};

/* Apple's documented -1..1 scale, from libtigercompat. */
CT_EXTERN const CGFloat kCTFontWeightUltraLight;
CT_EXTERN const CGFloat kCTFontWeightThin;
CT_EXTERN const CGFloat kCTFontWeightLight;
CT_EXTERN const CGFloat kCTFontWeightRegular;
CT_EXTERN const CGFloat kCTFontWeightMedium;
CT_EXTERN const CGFloat kCTFontWeightSemibold;
CT_EXTERN const CGFloat kCTFontWeightBold;
CT_EXTERN const CGFloat kCTFontWeightHeavy;
CT_EXTERN const CGFloat kCTFontWeightBlack;

CT_EXTERN const CGFloat kCTFontWidthUltraCompressed;
CT_EXTERN const CGFloat kCTFontWidthExtraCompressed;
CT_EXTERN const CGFloat kCTFontWidthCompressed;
CT_EXTERN const CGFloat kCTFontWidthExtraCondensed;
CT_EXTERN const CGFloat kCTFontWidthCondensed;
CT_EXTERN const CGFloat kCTFontWidthSemiCondensed;
CT_EXTERN const CGFloat kCTFontWidthStandard;
CT_EXTERN const CGFloat kCTFontWidthSemiExpanded;
CT_EXTERN const CGFloat kCTFontWidthExpanded;
CT_EXTERN const CGFloat kCTFontWidthExtraExpanded;

#endif /* __CTFONTTRAITS__ */
