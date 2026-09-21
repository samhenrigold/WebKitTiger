/* TIGER: record what CoreText does, so the 64-bit font cache can be checked
 * against it in a process that has no CoreText.
 *
 * Reads the request lines tigerfontcachetest --record writes
 * (label, PostScript name, size, rtl, codepoints...) and writes one reference
 * line per case: label, name, then glyph:x pairs, x being the pen position in
 * points from the start of the line.
 *
 * Positions are accumulated from CTRunGetAdvancesPtr rather than read from
 * CTRunGetPositionsPtr because that is what spike/textpixel's raster32 compared
 * against when the seven cases were declared pixel-identical, and this file
 * exists to keep that same number honest.
 *
 * Build:
 *   toolchain/bin/tiger-clang -O1 -o build/ctreference32 spike/tigerfontcache/ctreference32.c \
 *     -ltigercompat -Fcompat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -framework CoreFoundation -framework ApplicationServices
 */

#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit(const char* label, const char* psName, double size, int rtl,
    const UniChar* text, CFIndex length)
{
    CFStringRef name = CFStringCreateWithCString(NULL, psName, kCFStringEncodingUTF8);
    CTFontRef font = name ? CTFontCreateWithName(name, size, NULL) : NULL;
    CFStringRef string;
    CFStringRef key = kCTFontAttributeName;
    CFDictionaryRef attributes;
    CFAttributedStringRef attributed;
    CTLineRef line;
    CFArrayRef runs;
    double pen = 0;
    CFIndex r;

    if (!font) {
        fprintf(stderr, "no font named %s\n", psName);
        if (name) CFRelease(name);
        return;
    }
    /* The requested name must be the name we get, or the reference would be
     * recorded for a face the 64-bit side never chose. */
    {
        CFStringRef got = CTFontCopyPostScriptName(font);
        char buffer[128] = "";
        if (got) {
            CFStringGetCString(got, buffer, sizeof(buffer), kCFStringEncodingUTF8);
            CFRelease(got);
        }
        if (strcmp(buffer, psName))
            fprintf(stderr, "asked for %s, CoreText gave %s\n", psName, buffer);
    }

    string = CFStringCreateWithCharacters(NULL, text, length);
    attributes = CFDictionaryCreate(NULL, (const void**)&key, (const void**)&font, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    attributed = CFAttributedStringCreate(NULL, string, attributes);
    line = CTLineCreateWithAttributedString(attributed);
    runs = line ? CTLineGetGlyphRuns(line) : NULL;

    printf("%s\t%s\t", label, psName);
    for (r = 0; runs && r < CFArrayGetCount(runs); ++r) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
        const CGSize* advances = CTRunGetAdvancesPtr(run);
        const CGGlyph* glyphs = CTRunGetGlyphsPtr(run);
        CFIndex n = CTRunGetGlyphCount(run), i;
        for (i = 0; i < n; ++i) {
            printf("%u:%.5f ", glyphs ? (unsigned)glyphs[i] : 0u, pen);
            if (advances)
                pen += advances[i].width;
        }
    }
    printf("\n");
    fprintf(stderr, "%-26s %-16s %d chars, width %.4f pt%s\n", label, psName,
        (int)length, pen, rtl ? "  [rtl]" : "");

    if (line) CFRelease(line);
    CFRelease(attributed); CFRelease(attributes); CFRelease(string);
    CFRelease(font); CFRelease(name);
}

int main(int argc, char** argv)
{
    FILE* input = argc > 1 ? fopen(argv[1], "r") : stdin;
    char line[8192];

    if (!input) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    while (fgets(line, sizeof(line), input)) {
        char* label = strtok(line, "\t");
        char* psName = strtok(NULL, "\t");
        char* sizeText = strtok(NULL, "\t");
        char* rtlText = strtok(NULL, "\t");
        UniChar text[256];
        CFIndex length = 0;
        char* token;

        if (!label || !psName || !sizeText || !rtlText)
            continue;
        for (token = strtok(NULL, "\t\n"); token && length < 256; token = strtok(NULL, "\t\n"))
            text[length++] = (UniChar)strtoul(token, NULL, 10);
        if (length)
            emit(label, psName, atof(sizeText), atoi(rtlText), text, length);
    }
    if (input != stdin) fclose(input);
    return 0;
}
