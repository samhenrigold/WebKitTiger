/* TIGER SDK OVERLAY: <CoreText/CTFontDescriptor.h> — see CTDefines.h. */
#ifndef __CTFONTDESCRIPTOR__
#define __CTFONTDESCRIPTOR__

#include <CoreText/CTDefines.h>
#include <CoreText/CTFontTraits.h>

typedef const struct __CTFontDescriptor* CTFontDescriptorRef;

typedef uint32_t CTFontDescriptorOptions;
typedef uint32_t CTFontOptions;
typedef uint32_t CTFontTextStylePlatform;

enum {
    kCTFontDescriptorOptionSystemUIFont         = (1 << 1),
    kCTFontOptionsSystemUIFont                  = (1 << 1),
    kCTFontOptionsPreferSystemFont              = (1 << 2),
    kCTFontDescriptorOptionPreferAppleSystemFont = kCTFontOptionsPreferSystemFont,
    kCTFontDescriptorOptionThisIsNotARealOption = 0xFFFFFFFF
};

enum { kCTFontDescriptorMatchingOptionIncludeHiddenFonts = (1 << 16) };

enum {
    kCTFontTextStylePlatformDefault      = (CTFontTextStylePlatform)-1,
    kCTFontTextStylePlatformPhone        = 0,
    kCTFontTextStylePlatformWatch        = 1,
    kCTFontTextStylePlatformTV           = 2,
    kCTFontTextStylePlatformMac          = 3,
    kCTFontTextStylePlatformMacTouchBar  = 4,
    kCTFontTextStylePlatformVision       = 5,
    kCTFontTextStylePlatformVisionLegacy = 6
};

CT_EXTERN CFTypeID CTFontDescriptorGetTypeID(void);

/* --- attribute keys Tiger exports --------------------------------------- */
CT_EXTERN const CFStringRef kCTFontNameAttribute;
CT_EXTERN const CFStringRef kCTFontDisplayNameAttribute;
CT_EXTERN const CFStringRef kCTFontFamilyNameAttribute;
CT_EXTERN const CFStringRef kCTFontStyleNameAttribute;
CT_EXTERN const CFStringRef kCTFontTraitsAttribute;
CT_EXTERN const CFStringRef kCTFontSizeAttribute;
CT_EXTERN const CFStringRef kCTFontMatrixAttribute;
CT_EXTERN const CFStringRef kCTFontCascadeListAttribute;
CT_EXTERN const CFStringRef kCTFontCharacterSetAttribute;
CT_EXTERN const CFStringRef kCTFontLocalesAttribute;
CT_EXTERN const CFStringRef kCTFontFormatAttribute;
CT_EXTERN const CFStringRef kCTFontMacintoshEncodingsAttribute;
CT_EXTERN const CFStringRef kCTFontFeaturesAttribute;
CT_EXTERN const CFStringRef kCTFontFeatureSettingsAttribute;
CT_EXTERN const CFStringRef kCTFontFixedAdvanceAttribute;
CT_EXTERN const CFStringRef kCTFontVariationAttribute;
CT_EXTERN const CFStringRef kCTFontFileURLAttribute;

/* --- attribute keys from libtigercompat --------------------------------- */
CT_EXTERN const CFStringRef kCTFontURLAttribute;
CT_EXTERN const CFStringRef kCTFontVariationAxesAttribute;
CT_EXTERN const CFStringRef kCTFontReferenceURLAttribute;
CT_EXTERN const CFStringRef kCTFontPostScriptNameAttribute;
CT_EXTERN const CFStringRef kCTFontOpticalSizeAttribute;
CT_EXTERN const CFStringRef kCTFontUserInstalledAttribute;
CT_EXTERN const CFStringRef kCTFontEnabledAttribute;
CT_EXTERN const CFStringRef kCTFontFallbackOptionAttribute;
CT_EXTERN const CFStringRef kCTFontDescriptorLanguageAttribute;
CT_EXTERN const CFStringRef kCTFontDescriptorTextStyleAttribute;
CT_EXTERN const CFStringRef kCTFontDescriptorTextStyleEmphasized;
CT_EXTERN const CFStringRef kCTFontCSSWeightAttribute;
CT_EXTERN const CFStringRef kCTFontCSSWidthAttribute;
CT_EXTERN const CFStringRef kCTFontSizeCategoryAttribute;
CT_EXTERN const CFStringRef kCTFontTrackAttribute;
CT_EXTERN const CFStringRef kCTFontUnscaledTrackingAttribute;
CT_EXTERN const CFStringRef kCTFontIgnoreLegibilityWeightAttribute;
CT_EXTERN const CFStringRef kCTFontOrientationAttribute;
CT_EXTERN const CFStringRef kCTFontPaletteAttribute;
CT_EXTERN const CFStringRef kCTFontPaletteColorsAttribute;
CT_EXTERN const CFStringRef kCTFontOpenTypeFeatureTag;
CT_EXTERN const CFStringRef kCTFontOpenTypeFeatureValue;
CT_EXTERN const CFStringRef kCTFontUIFontDesignDefault;
CT_EXTERN const CFStringRef kCTFontUIFontDesignSerif;
CT_EXTERN const CFStringRef kCTFontUIFontDesignMonospaced;
CT_EXTERN const CFStringRef kCTFontUIFontDesignRounded;
CT_EXTERN const CFStringRef kCTFontCSSFamilySerif;
CT_EXTERN const CFStringRef kCTFontCSSFamilySansSerif;
CT_EXTERN const CFStringRef kCTFontCSSFamilyCursive;
CT_EXTERN const CFStringRef kCTFontCSSFamilyFantasy;
CT_EXTERN const CFStringRef kCTFontCSSFamilyMonospace;
CT_EXTERN const CFStringRef kCTFontCSSFamilySystemUI;
CT_EXTERN const CFStringRef kCTFontContentSizeCategoryL;
CT_EXTERN const CFStringRef kCTFontContentSizeCategoryXXXL;

/* Tiger's feature and variation sub-keys. */
CT_EXTERN const CFStringRef kCTFontFeatureTypeIdentifierKey;
CT_EXTERN const CFStringRef kCTFontFeatureTypeNameKey;
CT_EXTERN const CFStringRef kCTFontFeatureTypeExclusiveKey;
CT_EXTERN const CFStringRef kCTFontFeatureTypeSelectorsKey;
CT_EXTERN const CFStringRef kCTFontFeatureSelectorIdentifierKey;
CT_EXTERN const CFStringRef kCTFontFeatureSelectorNameKey;
CT_EXTERN const CFStringRef kCTFontFeatureSelectorDefaultKey;
CT_EXTERN const CFStringRef kCTFontFeatureSelectorSettingKey;
CT_EXTERN const CFStringRef kCTFontVariationAxisIdentifierKey;
CT_EXTERN const CFStringRef kCTFontVariationAxisMinimumValueKey;
CT_EXTERN const CFStringRef kCTFontVariationAxisMaximumValueKey;
CT_EXTERN const CFStringRef kCTFontVariationAxisDefaultValueKey;
CT_EXTERN const CFStringRef kCTFontVariationAxisNameKey;

/* --- case 1: Tiger's ABI already matches -------------------------------- */
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateWithAttributes(CFDictionaryRef attributes);
CT_EXTERN CFTypeRef CTFontDescriptorCopyAttribute(CTFontDescriptorRef, CFStringRef attribute);
CT_EXTERN CFDictionaryRef CTFontDescriptorCopyAttributes(CTFontDescriptorRef);
CT_EXTERN CFTypeRef CTFontDescriptorCopyLocalizedAttribute(CTFontDescriptorRef, CFStringRef attribute, CFStringRef* language);

/* --- case 2: Tiger reads the size as a by-value double ------------------- */
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateWithNameAndSize(CFStringRef name, double size);

/* --- case 3: modern names bound to libtigercompat ----------------------- */
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateCopyWithAttributes(CTFontDescriptorRef, CFDictionaryRef attributes);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateCopyWithFeature(CTFontDescriptorRef, CFNumberRef featureTypeIdentifier, CFNumberRef featureSelectorIdentifier);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateCopyWithSymbolicTraits(CTFontDescriptorRef, CTFontSymbolicTraits value, CTFontSymbolicTraits mask);
CT_EXTERN CFArrayRef CTFontDescriptorCreateMatchingFontDescriptors(CTFontDescriptorRef, CFSetRef mandatoryAttributes);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateMatchingFontDescriptor(CTFontDescriptorRef, CFSetRef mandatoryAttributes);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateWithAttributesAndOptions(CFDictionaryRef attributes, CTFontDescriptorOptions);
CT_EXTERN CTFontDescriptorOptions CTFontDescriptorGetOptions(CTFontDescriptorRef);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateLastResort(void);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateForUIType(uint32_t uiType, CGFloat size, CFStringRef language);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateWithTextStyle(CFStringRef style, CFStringRef size, CFStringRef language);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateWithTextStyleAndAttributes(CFStringRef style, CFStringRef size, CFDictionaryRef attributes);
CT_EXTERN CGFloat CTFontDescriptorGetTextStyleSize(CFStringRef style, CFTypeRef sizeCategory, CTFontTextStylePlatform, CGFloat* weight, CGFloat* lineSpacing);
CT_EXTERN CTFontDescriptorRef CTFontDescriptorCreateForCSSFamily(CFStringRef cssFamily, CFStringRef language);
CT_EXTERN bool CTFontDescriptorIsSystemUIFont(CTFontDescriptorRef);

#endif /* __CTFONTDESCRIPTOR__ */
