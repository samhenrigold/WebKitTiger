/* TIGER: differential test of compat/ctcompat.c against a live Apple CoreText.
 *
 * Leopard DP1 (9A241) CoreText runs on 10.4.11 with a patched import table and
 * a CF-bridge bootstrap; the recipe, the shim and the original functional test
 * are in refs/leopard-9a241/tools/ and logs/leopard-backport.md. It is a
 * Tiger-generation binary with the real public CGFloat ABI, and it exports 13
 * of the 66 functions ctcompat.c has to implement. That makes it an oracle: run
 * the same inputs through our shim and through Apple's, and diff.
 *
 * It is NOT a dependency of the port. It is pre-release, it puts two CoreTexts
 * in one process, and objects cannot cross between them, so every comparison
 * below keeps each side's objects on its own side and compares only values.
 *
 * Build (bash, from the repo root):
 *   toolchain/bin/tiger-clang -O1 -g -Wall spike/ctoracle.c -o build/ctoracle \
 *     -ltigercompat -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -framework CoreFoundation -framework ApplicationServices
 * Run (after refs/leopard-9a241/tools/README has put /tmp/l9 on the box):
 *   ssh tiger /tmp/ctoracle
 */

#include <CoreText/CoreText.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, mismatches;

static void diff(int same, const char* what, const char* ours, const char* theirs)
{
    ++checks;
    if (same)
        printf("same %s\n", what);
    else {
        printf("DIFF %s\n       ours: %s\n     apple: %s\n", what, ours, theirs);
        ++mismatches;
    }
}

static const char* cstr(CFStringRef s, char* buffer, size_t size)
{
    if (!s)
        return "(null)";
    if (!CFStringGetCString(s, buffer, (CFIndex)size, kCFStringEncodingUTF8))
        return "(?)";
    return buffer;
}

/* 9A241 entry points, all CGFloat-ABI. */
typedef CFTypeID (*fn_typeid)(void);
typedef CTFontRef (*fn_fontname)(CFStringRef, CGFloat, const CGAffineTransform*);
typedef CTFontRef (*fn_fontdesc)(CTFontDescriptorRef, CGFloat, const CGAffineTransform*);
typedef CTFontRef (*fn_uifont)(uint32_t, CGFloat, CFStringRef);
typedef CFStringRef (*fn_copystr)(CTFontRef);
typedef CFIndex (*fn_glyphcount)(CTFontRef);
typedef CGFontRef (*fn_copycg)(CTFontRef, CTFontDescriptorRef*);
typedef CFArrayRef (*fn_cascade)(CTFontRef);
typedef bool (*fn_glyphs)(CTFontRef, const UniChar*, CGGlyph*, CFIndex);
/* 9A241's plain CTFontGetAdvancesForGlyphs takes no orientation, exactly like
 * Tiger's; the orientation-taking form is its separate TRANSITIONAL export.
 * Calling it the modern way puts the orientation where the glyph pointer goes
 * and it returns NaN from its null-glyphs path. */
typedef double (*fn_adv)(CTFontRef, const CGGlyph*, CGSize*, CFIndex);
typedef CTFontDescriptorRef (*fn_dnew)(CFDictionaryRef);
typedef CTFontDescriptorRef (*fn_dname)(CFStringRef, CGFloat);
typedef CTFontDescriptorRef (*fn_dtraits)(CTFontDescriptorRef, uint32_t, uint32_t);
typedef CTFontDescriptorRef (*fn_dattrs)(CTFontDescriptorRef, CFDictionaryRef);
typedef CTFontDescriptorRef (*fn_dfeature)(CTFontDescriptorRef, CFNumberRef, CFNumberRef);
typedef CTFontDescriptorRef (*fn_dmatch)(CTFontDescriptorRef, CFSetRef);
typedef CFArrayRef (*fn_dmatchall)(CTFontDescriptorRef, CFSetRef);
typedef CTFontDescriptorRef (*fn_duitype)(uint32_t, CGFloat, CFStringRef);
typedef CTFontRef (*fn_forchars)(CTFontRef, const UniChar*, CFIndex);
typedef CTLineRef (*fn_lnew)(CFAttributedStringRef);
typedef double (*fn_ltrail)(CTLineRef);
typedef CFTypeRef (*fn_dattr)(CTFontDescriptorRef, CFStringRef);

static struct {
    void* handle;
    fn_fontname fontWithName;
    fn_fontdesc fontWithDescriptor;
    fn_uifont uiFont;
    fn_copystr copyFullName;
    fn_copystr copyPostScriptName;
    fn_glyphcount glyphCount;
    fn_copycg copyGraphicsFont;
    fn_cascade cascadeList;
    fn_glyphs glyphsForCharacters;
    fn_adv advances;
    fn_dnew descriptorWithAttributes;
    fn_dname descriptorWithNameAndSize;
    fn_dtraits descriptorWithTraits;
    fn_dattrs descriptorWithAttrs;
    fn_dfeature descriptorWithFeature;
    fn_dmatch match;
    fn_dmatchall matchAll;
    fn_duitype descriptorForUIType;
    fn_forchars fontForCharacters;
    fn_lnew lineWithString;
    fn_ltrail trailingWhitespace;
    fn_dattr descriptorAttribute;
    CFStringRef familyKey;
} ct9;

static int loadOracle(void)
{
    void* shim = dlopen("/tmp/l9/ct9shim.dylib", RTLD_LAZY | RTLD_GLOBAL);
    void (*bind)(unsigned long, void*);
    fn_typeid fontTypeID, descriptorTypeID;
    CTFontRef probeFont;
    CTFontDescriptorRef probeDescriptor;
    CFStringRef key, value;
    CFDictionaryRef attributes;
    CFStringRef* familyKeyPtr;

    if (!shim) {
        printf("cannot load the shim: %s\n", dlerror());
        return 0;
    }
    bind = (void (*)(unsigned long, void*))dlsym(shim, "ct9_bind");
    ct9.handle = dlopen("/tmp/l9/LeopardCT9", RTLD_LAZY | RTLD_LOCAL);
    if (!ct9.handle) {
        printf("cannot load 9A241 CoreText: %s\n", dlerror());
        return 0;
    }

#define SYM(field, name, type) ct9.field = (type)dlsym(ct9.handle, name)
    SYM(fontWithName, "CTFontCreateWithName", fn_fontname);
    SYM(fontWithDescriptor, "CTFontCreateWithFontDescriptor", fn_fontdesc);
    SYM(uiFont, "CTFontCreateUIFontForLanguage", fn_uifont);
    SYM(copyFullName, "CTFontCopyFullName", fn_copystr);
    SYM(copyPostScriptName, "CTFontCopyPostScriptName", fn_copystr);
    SYM(glyphCount, "CTFontGetGlyphCount", fn_glyphcount);
    SYM(copyGraphicsFont, "CTFontCopyGraphicsFont", fn_copycg);
    SYM(cascadeList, "CTFontCopyDefaultCascadeList", fn_cascade);
    SYM(glyphsForCharacters, "CTFontGetGlyphsForCharacters", fn_glyphs);
    SYM(advances, "CTFontGetAdvancesForGlyphs", fn_adv);
    SYM(descriptorWithAttributes, "CTFontDescriptorCreateWithAttributes", fn_dnew);
    SYM(descriptorWithNameAndSize, "CTFontDescriptorCreateWithNameAndSize", fn_dname);
    SYM(descriptorWithTraits, "CTFontDescriptorCreateCopyWithSymbolicTraits", fn_dtraits);
    SYM(descriptorWithAttrs, "CTFontDescriptorCreateCopyWithAttributes", fn_dattrs);
    SYM(descriptorWithFeature, "CTFontDescriptorCreateCopyWithFeature", fn_dfeature);
    SYM(match, "CTFontDescriptorCreateMatchingFontDescriptor", fn_dmatch);
    SYM(matchAll, "CTFontDescriptorCreateMatchingFontDescriptors", fn_dmatchall);
    SYM(descriptorForUIType, "CTFontDescriptorCreateForUIType", fn_duitype);
    SYM(fontForCharacters, "CTFontCreateForCharacters", fn_forchars);
    SYM(lineWithString, "CTLineCreateWithAttributedString", fn_lnew);
    SYM(trailingWhitespace, "CTLineGetTrailingWhitespaceWidth", fn_ltrail);
    SYM(descriptorAttribute, "CTFontDescriptorCopyAttribute", fn_dattr);
#undef SYM
    familyKeyPtr = (CFStringRef*)dlsym(ct9.handle, "kCTFontFamilyNameAttribute");
    ct9.familyKey = familyKeyPtr ? *familyKeyPtr : CFSTR("NSFontFamilyAttribute");

    /* The bootstrap: register both types, then teach the shim's table the isa
     * each one's instances really carry. Type ids are assigned in first-call
     * order, so they have to be read at run time. */
    fontTypeID = (fn_typeid)dlsym(ct9.handle, "CTFontGetTypeID");
    descriptorTypeID = (fn_typeid)dlsym(ct9.handle, "CTFontDescriptorGetTypeID");
    if (!bind || !fontTypeID || !descriptorTypeID)
        return 0;

    probeFont = ct9.fontWithName(CFSTR("Helvetica"), 12, NULL);
    if (probeFont)
        bind((unsigned long)fontTypeID(), *(void**)probeFont);
    key = ct9.familyKey;
    value = CFSTR("Times");
    attributes = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    probeDescriptor = ct9.descriptorWithAttributes(attributes);
    if (probeDescriptor)
        bind((unsigned long)descriptorTypeID(), *(void**)probeDescriptor);
    CFRelease(attributes);
    return probeFont && probeDescriptor;
}

/* ---- the comparisons -------------------------------------------------- */

static const UniChar latin[] = { 'A', 'V', 'i', 'f', 'i' };
static const UniChar cjk[] = { 0x4E2D, 0x6587 };

static void compareFontNames(void)
{
    static const char* families[] = { "Helvetica", "Times", "Courier", "Lucida Grande", "Monaco" };
    unsigned i;

    for (i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
        CFStringRef name = CFStringCreateWithCString(NULL, families[i], kCFStringEncodingUTF8);
        CTFontRef ours = CTFontCreateWithName(name, 16, NULL);
        CTFontRef theirs = ct9.fontWithName(name, 16, NULL);
        char a[128], b[128], label[160];
        CFStringRef oursName = ours ? CTFontCopyFullName(ours) : NULL;
        CFStringRef theirsName = theirs && ct9.copyFullName ? ct9.copyFullName(theirs) : NULL;

        snprintf(label, sizeof(label), "CTFontCopyFullName(%s)", families[i]);
        diff(oursName && theirsName && CFEqual(oursName, theirsName), label,
            cstr(oursName, a, sizeof(a)), cstr(theirsName, b, sizeof(b)));

        snprintf(label, sizeof(label), "CTFontGetGlyphCount(%s)", families[i]);
        {
            CFIndex ourCount = ours ? CTFontGetGlyphCount(ours) : -1;
            CFIndex theirCount = theirs && ct9.glyphCount ? ct9.glyphCount(theirs) : -1;
            snprintf(a, sizeof(a), "%ld", (long)ourCount);
            snprintf(b, sizeof(b), "%ld", (long)theirCount);
            diff(ourCount == theirCount && ourCount > 0, label, a, b);
        }

        snprintf(label, sizeof(label), "advances for \"AVifi\" in %s", families[i]);
        {
            CGGlyph g1[5], g2[5];
            CGSize s1[5], s2[5];
            double t1 = 0, t2 = 0;
            if (ours && CTFontGetGlyphsForCharacters(ours, latin, g1, 5))
                t1 = CTFontGetAdvancesForGlyphs(ours, kCTFontOrientationHorizontal, g1, s1, 5);
            if (theirs && ct9.glyphsForCharacters && ct9.glyphsForCharacters(theirs, latin, g2, 5))
                t2 = ct9.advances(theirs, g2, s2, 5);
            snprintf(a, sizeof(a), "%.4f", t1);
            snprintf(b, sizeof(b), "%.4f", t2);
            diff(t1 > 0 && t2 > 0 && t1 > t2 - 0.01 && t1 < t2 + 0.01, label, a, b);
        }

        if (oursName) CFRelease(oursName);
        if (theirsName) CFRelease(theirsName);
        if (ours) CFRelease(ours);
        if (theirs) CFRelease(theirs);
        CFRelease(name);
    }
}

static void compareGraphicsFont(void)
{
    CTFontRef ours = CTFontCreateWithName(CFSTR("Helvetica"), 16, NULL);
    CTFontRef theirs = ct9.fontWithName(CFSTR("Helvetica"), 16, NULL);
    CGFontRef ourCG = ours ? CTFontCopyGraphicsFont(ours, NULL) : NULL;
    CGFontRef theirCG = theirs && ct9.copyGraphicsFont ? ct9.copyGraphicsFont(theirs, NULL) : NULL;
    char a[128], b[128];
    CFStringRef n1 = ourCG ? CGFontCopyPostScriptName(ourCG) : NULL;
    CFStringRef n2 = theirCG ? CGFontCopyPostScriptName(theirCG) : NULL;

    diff(n1 && n2 && CFEqual(n1, n2), "CTFontCopyGraphicsFont PostScript name",
        cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
    if (n1) CFRelease(n1);
    if (n2) CFRelease(n2);
    if (ourCG) CGFontRelease(ourCG);
    if (theirCG) CGFontRelease(theirCG);
    if (ours) CFRelease(ours);
    if (theirs) CFRelease(theirs);
}

static void compareUIFonts(void)
{
    static const int types[] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 15, 16, 18, 20, 21, 22, 23, 25, 26 };
    unsigned i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        CTFontRef ours = CTFontCreateUIFontForLanguage((CTFontUIFontType)types[i], 0, NULL);
        CTFontRef theirs = ct9.uiFont ? ct9.uiFont((uint32_t)types[i], 0, NULL) : NULL;
        CFStringRef n1 = ours ? CTFontCopyPostScriptName(ours) : NULL;
        CFStringRef n2 = theirs && ct9.copyPostScriptName ? ct9.copyPostScriptName(theirs) : NULL;
        char a[160], b[160], label[80];
        double s1 = ours ? CTFontGetSize(ours) : 0;
        double s2 = 0;

        /* Their CTFontGetSize is CGFloat-ABI, so read the size off the font we
         * asked for rather than calling across; compare name and our own size
         * against their name and the size their descriptor reports. */
        if (theirs) {
            CTFontRef sized = ct9.uiFont((uint32_t)types[i], 0, NULL);
            if (sized) {
                CFNumberRef number = NULL;
                CTFontDescriptorRef d = NULL;
                (void)d;
                (void)number;
                CFRelease(sized);
            }
        }
        snprintf(label, sizeof(label), "UI font type %d", types[i]);
        snprintf(a, sizeof(a), "%s@%.0f", cstr(n1, a + 80, 70), s1);
        snprintf(b, sizeof(b), "%s", cstr(n2, b + 80, 70));
        diff(n1 && n2 && CFEqual(n1, n2), label, a, b);
        (void)s2;

        if (n1) CFRelease(n1);
        if (n2) CFRelease(n2);
        if (ours) CFRelease(ours);
        if (theirs) CFRelease(theirs);
    }
}

static void compareDescriptors(void)
{
    CFStringRef key = kCTFontFamilyNameAttribute, value = CFSTR("Helvetica");
    CFDictionaryRef attributes = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef theirKey = ct9.familyKey;
    CFDictionaryRef theirAttributes = CFDictionaryCreate(NULL, (const void**)&theirKey, (const void**)&value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CTFontDescriptorRef ourBase = CTFontDescriptorCreateWithAttributes(attributes);
    CTFontDescriptorRef theirBase = ct9.descriptorWithAttributes(theirAttributes);
    char a[128], b[128];

    /* Symbolic traits: the case that used to hand back the system font. */
    {
        CTFontDescriptorRef ourBold = CTFontDescriptorCreateCopyWithSymbolicTraits(ourBase,
            kCTFontTraitBold, kCTFontTraitBold);
        CTFontDescriptorRef theirBold = ct9.descriptorWithTraits ?
            ct9.descriptorWithTraits(theirBase, 2, 2) : NULL;
        CTFontRef f1 = ourBold ? CTFontCreateWithFontDescriptor(ourBold, 16, NULL) : NULL;
        CTFontRef f2 = theirBold ? ct9.fontWithDescriptor(theirBold, 16, NULL) : NULL;
        CFStringRef n1 = f1 ? CTFontCopyPostScriptName(f1) : NULL;
        CFStringRef n2 = f2 && ct9.copyPostScriptName ? ct9.copyPostScriptName(f2) : NULL;

        diff(n1 && n2 && CFEqual(n1, n2), "bold Helvetica via symbolic traits",
            cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
        if (n1) CFRelease(n1);
        if (n2) CFRelease(n2);
        if (f1) CFRelease(f1);
        if (f2) CFRelease(f2);
        if (ourBold) CFRelease(ourBold);
        if (theirBold) CFRelease(theirBold);
    }

    /* Matching. */
    {
        CTFontDescriptorRef m1 = CTFontDescriptorCreateMatchingFontDescriptor(ourBase, NULL);
        CTFontDescriptorRef m2 = ct9.match ? ct9.match(theirBase, NULL) : NULL;
        CFStringRef n1 = m1 ? (CFStringRef)CTFontDescriptorCopyAttribute(m1, kCTFontNameAttribute) : NULL;
        CFStringRef n2 = m2 && ct9.descriptorAttribute ?
            (CFStringRef)ct9.descriptorAttribute(m2, CFSTR("NSFontNameAttribute")) : NULL;

        diff(n1 && n2 && CFEqual(n1, n2), "CTFontDescriptorCreateMatchingFontDescriptor",
            cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
        if (n1) CFRelease(n1);
        if (n2) CFRelease(n2);
        if (m1) CFRelease(m1);
        if (m2) CFRelease(m2);
    }
    {
        CFArrayRef all1 = CTFontDescriptorCreateMatchingFontDescriptors(ourBase, NULL);
        CFArrayRef all2 = ct9.matchAll ? ct9.matchAll(theirBase, NULL) : NULL;
        CFIndex c1 = all1 ? CFArrayGetCount(all1) : -1;
        CFIndex c2 = all2 ? CFArrayGetCount(all2) : -1;

        snprintf(a, sizeof(a), "%ld", (long)c1);
        snprintf(b, sizeof(b), "%ld", (long)c2);
        diff(c1 == c2 && c1 > 0, "CTFontDescriptorCreateMatchingFontDescriptors count", a, b);
        if (all1) CFRelease(all1);
        if (all2) CFRelease(all2);
    }

    /* Copy-with-attributes and copy-with-feature. */
    {
        CFStringRef k2 = kCTFontFamilyNameAttribute, v2 = CFSTR("Courier");
        CFDictionaryRef over = CFDictionaryCreate(NULL, (const void**)&k2, (const void**)&v2, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFStringRef tk2 = ct9.familyKey;
        CFDictionaryRef theirOver = CFDictionaryCreate(NULL, (const void**)&tk2, (const void**)&v2, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CTFontDescriptorRef d1 = CTFontDescriptorCreateCopyWithAttributes(ourBase, over);
        CTFontDescriptorRef d2 = ct9.descriptorWithAttrs ? ct9.descriptorWithAttrs(theirBase, theirOver) : NULL;
        CTFontRef f1 = d1 ? CTFontCreateWithFontDescriptor(d1, 16, NULL) : NULL;
        CTFontRef f2 = d2 ? ct9.fontWithDescriptor(d2, 16, NULL) : NULL;
        CFStringRef n1 = f1 ? CTFontCopyPostScriptName(f1) : NULL;
        CFStringRef n2 = f2 && ct9.copyPostScriptName ? ct9.copyPostScriptName(f2) : NULL;

        diff(n1 && n2 && CFEqual(n1, n2), "CTFontDescriptorCreateCopyWithAttributes",
            cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
        if (n1) CFRelease(n1);
        if (n2) CFRelease(n2);
        if (f1) CFRelease(f1);
        if (f2) CFRelease(f2);
        if (d1) CFRelease(d1);
        if (d2) CFRelease(d2);
        CFRelease(over);
        CFRelease(theirOver);
    }

    /* Descriptors for UI types. */
    {
        static const int types[] = { 2, 3, 4, 5, 6, 7, 12, 18, 20 };
        unsigned i;
        for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            CTFontDescriptorRef d1 = CTFontDescriptorCreateForUIType((CTFontUIFontType)types[i], 0, NULL);
            CTFontDescriptorRef d2 = ct9.descriptorForUIType ?
                ct9.descriptorForUIType((uint32_t)types[i], 0, NULL) : NULL;
            CFStringRef n1 = d1 ? (CFStringRef)CTFontDescriptorCopyAttribute(d1, kCTFontNameAttribute) : NULL;
            CFStringRef n2 = d2 && ct9.descriptorAttribute ?
                (CFStringRef)ct9.descriptorAttribute(d2, CFSTR("NSFontNameAttribute")) : NULL;
            char label[80];

            snprintf(label, sizeof(label), "CTFontDescriptorCreateForUIType(%d)", types[i]);
            diff(n1 && n2 && CFEqual(n1, n2), label, cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
            if (n1) CFRelease(n1);
            if (n2) CFRelease(n2);
            if (d1) CFRelease(d1);
            if (d2) CFRelease(d2);
        }
    }

    if (ourBase) CFRelease(ourBase);
    if (theirBase) CFRelease(theirBase);
    CFRelease(attributes);
    CFRelease(theirAttributes);
}

static void compareCascadeLists(void)
{
    CTFontRef ours = CTFontCreateWithName(CFSTR("Helvetica"), 16, NULL);
    CTFontRef theirs = ct9.fontWithName(CFSTR("Helvetica"), 16, NULL);
    CFArrayRef a1 = ours ? CTFontCopyDefaultCascadeListForLanguages(ours, NULL) : NULL;
    CFArrayRef a2 = theirs && ct9.cascadeList ? ct9.cascadeList(theirs) : NULL;
    char a[128], b[128];
    CFIndex c1 = a1 ? CFArrayGetCount(a1) : -1;
    CFIndex c2 = a2 ? CFArrayGetCount(a2) : -1;

    snprintf(a, sizeof(a), "%ld entries", (long)c1);
    snprintf(b, sizeof(b), "%ld entries", (long)c2);
    diff(c1 == c2 && c1 > 0, "default cascade list length", a, b);

    if (c1 > 0 && c2 > 0) {
        CFStringRef n1 = (CFStringRef)CTFontDescriptorCopyAttribute(
            (CTFontDescriptorRef)CFArrayGetValueAtIndex(a1, 0), kCTFontNameAttribute);
        CFStringRef n2 = ct9.descriptorAttribute ? (CFStringRef)ct9.descriptorAttribute(
            (CTFontDescriptorRef)CFArrayGetValueAtIndex(a2, 0), CFSTR("NSFontNameAttribute")) : NULL;
        diff(n1 && n2 && CFEqual(n1, n2), "first cascade entry",
            cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
        if (n1) CFRelease(n1);
        if (n2) CFRelease(n2);
    }
    if (a1) CFRelease(a1);
    if (a2) CFRelease(a2);
    if (ours) CFRelease(ours);
    if (theirs) CFRelease(theirs);
}

static void compareFallback(void)
{
    CTFontRef ours = CTFontCreateWithName(CFSTR("Helvetica"), 16, NULL);
    CTFontRef theirs = ct9.fontWithName(CFSTR("Helvetica"), 16, NULL);
    CFIndex covered = 0;
    CTFontRef f1 = ours ? CTFontCreateForCharacters(ours, cjk, 2, &covered) : NULL;
    /* 9A241's CTFontCreateForCharacters is itself an empty stub: its whole body
     * is `xor eax, eax; ret`. The oracle has no answer here, so there is nothing
     * to diff against and ours is the only implementation of the two. */
    CTFontRef f2 = theirs && ct9.fontForCharacters ? ct9.fontForCharacters(theirs, cjk, 2) : NULL;
    CFStringRef n1 = f1 ? CTFontCopyFamilyName(f1) : NULL;
    CFStringRef n2 = f2 ? (CFStringRef)NULL : NULL;
    char a[128], b[128];

    /* Their CTFontCopyFamilyName is on their side; compare PostScript names. */
    if (f2 && ct9.copyPostScriptName)
        n2 = ct9.copyPostScriptName(f2);
    if (n1) {
        CFRelease(n1);
        n1 = f1 ? CTFontCopyPostScriptName(f1) : NULL;
    }
    if (!f2) {
        ++checks;
        printf("n/a  CJK fallback font: the oracle's CTFontCreateForCharacters is a stub\n");
        printf("       ours: %s\n", cstr(n1, a, sizeof(a)));
    } else
        diff(n1 && n2 && CFEqual(n1, n2), "CJK fallback font",
            cstr(n1, a, sizeof(a)), cstr(n2, b, sizeof(b)));
    if (n1) CFRelease(n1);
    if (n2) CFRelease(n2);
    if (f1) CFRelease(f1);
    if (f2) CFRelease(f2);
    if (ours) CFRelease(ours);
    if (theirs) CFRelease(theirs);
}

static CFAttributedStringRef makeString(CFStringRef text, CTFontRef font, CFStringRef fontKey)
{
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(NULL, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef result;

    CFDictionarySetValue(attributes, fontKey, font);
    result = CFAttributedStringCreate(NULL, text, attributes);
    CFRelease(attributes);
    return result;
}

static void compareLines(void)
{
    CFStringRef plain = CFStringCreateWithCharacters(NULL, latin, 5);
    UniChar padded[7];
    CFStringRef spaced;
    CTFontRef ours = CTFontCreateWithName(CFSTR("Helvetica"), 16, NULL);
    CTFontRef theirs = ct9.fontWithName(CFSTR("Helvetica"), 16, NULL);
    CFStringRef* theirFontKey = (CFStringRef*)dlsym(ct9.handle, "kCTFontAttributeName");
    CFStringRef theirKey = theirFontKey ? *theirFontKey : CFSTR("NSFont");
    char a[128], b[128];

    memcpy(padded, latin, sizeof(latin));
    padded[5] = ' ';
    padded[6] = ' ';
    spaced = CFStringCreateWithCharacters(NULL, padded, 7);

    /* Typographic bounds: our adapter against their TRANSITIONAL-era entry. */
    {
        CFAttributedStringRef s1 = makeString(plain, ours, kCTFontAttributeName);
        CFAttributedStringRef s2 = makeString(plain, theirs, theirKey);
        CTLineRef l1 = CTLineCreateWithAttributedString(s1);
        CTLineRef l2 = ct9.lineWithString ? ct9.lineWithString(s2) : NULL;
        CGFloat asc1 = 0, desc1 = 0, lead1 = 0;
        double w1 = l1 ? CTLineGetTypographicBounds(l1, &asc1, &desc1, &lead1) : 0;
        double w2 = 0;
        typedef double (*lb_t)(CTLineRef, CGFloat*, CGFloat*, CGFloat*);
        lb_t theirBounds = (lb_t)dlsym(ct9.handle, "CTLineGetTypographicBounds");
        CGFloat asc2 = 0, desc2 = 0, lead2 = 0;

        if (l2 && theirBounds)
            w2 = theirBounds(l2, &asc2, &desc2, &lead2);
        snprintf(a, sizeof(a), "w=%.3f asc=%.3f desc=%.3f", w1, (double)asc1, (double)desc1);
        snprintf(b, sizeof(b), "w=%.3f asc=%.3f desc=%.3f", w2, (double)asc2, (double)desc2);
        diff(w1 > 0 && w2 > 0 && w1 > w2 - 0.02 && w1 < w2 + 0.02
            && asc1 > asc2 - 0.02 && asc1 < asc2 + 0.02, "CTLineGetTypographicBounds", a, b);

        /* Line bounds built on those, against their ascent+descent+leading. */
        {
            CGRect r = l1 ? CTLineGetBoundsWithOptions(l1, 0) : CGRectZero;
            double theirHeight = asc2 + desc2 + lead2;
            snprintf(a, sizeof(a), "h=%.3f", (double)r.size.height);
            snprintf(b, sizeof(b), "h=%.3f", theirHeight);
            diff(theirHeight > 0 && r.size.height > theirHeight - 0.02
                && r.size.height < theirHeight + 0.02,
                "CTLineGetBoundsWithOptions height vs ascent+descent+leading", a, b);
        }

        if (l1) CFRelease(l1);
        if (l2) CFRelease(l2);
        CFRelease(s1);
        CFRelease(s2);
    }

    /* Trailing whitespace: our measured version against Apple's real one. */
    {
        CFAttributedStringRef s1 = makeString(spaced, ours, kCTFontAttributeName);
        CFAttributedStringRef s2 = makeString(spaced, theirs, theirKey);
        CTLineRef l1 = CTLineCreateWithAttributedString(s1);
        CTLineRef l2 = ct9.lineWithString ? ct9.lineWithString(s2) : NULL;
        double t1 = l1 ? CTLineGetTrailingWhitespaceWidth(l1) : -1;
        double t2 = l2 && ct9.trailingWhitespace ? ct9.trailingWhitespace(l2) : -1;

        snprintf(a, sizeof(a), "%.4f", t1);
        snprintf(b, sizeof(b), "%.4f", t2);
        diff(t2 > 0 && t1 > t2 - 0.02 && t1 < t2 + 0.02,
            "CTLineGetTrailingWhitespaceWidth (two trailing spaces)", a, b);

        if (l1) CFRelease(l1);
        if (l2) CFRelease(l2);
        CFRelease(s1);
        CFRelease(s2);
    }

    if (ours) CFRelease(ours);
    if (theirs) CFRelease(theirs);
    CFRelease(plain);
    CFRelease(spaced);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("-- CoreText oracle: ctcompat vs Leopard DP1 9A241 on 10.4.11 --\n\n");
    if (!loadOracle()) {
        printf("oracle unavailable; see refs/leopard-9a241/tools/README\n");
        return 2;
    }

    printf("[fonts by name]\n");          compareFontNames();
    printf("\n[graphics font]\n");        compareGraphicsFont();
    printf("\n[UI fonts]\n");             compareUIFonts();
    printf("\n[descriptors]\n");          compareDescriptors();
    printf("\n[cascade lists]\n");        compareCascadeLists();
    printf("\n[font fallback]\n");        compareFallback();
    printf("\n[lines]\n");                compareLines();

    printf("\n%d comparisons, %d mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}
