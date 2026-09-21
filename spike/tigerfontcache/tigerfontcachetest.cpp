/* TIGER: the 64-bit font cache, checked against CoreText.
 *
 * Picks a face for each of the seven spike/textpixel cases out of
 * logs/tiger-fonts.json with WebCore's TigerFontDatabase, opens it through
 * FreeType at the path the manifest records, shapes it with HarfBuzz under the
 * two rules the split port holds -- Apple-format kern applied to the leading
 * glyph, and a scale fine enough that per-glyph rounding does not accumulate --
 * and asserts every glyph lands within 0.02 pt of where CoreText puts it.
 *
 * The CoreText side is recorded once, on the box, by ctreference32.c, because
 * this process has no CoreText: x86_64 on Mac OS X 10.4 has libSystem,
 * libstdc++ and libz and nothing else.
 *
 *   tigerfontcachetest <manifest> --record      > requests.txt
 *   ctreference32 requests.txt                  > ct-reference.txt
 *   tigerfontcachetest <manifest> ct-reference.txt
 */

#include "TigerAppleKern.h"
#include "TigerFontDatabase.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace WebCore;

static const double kTolerance = 0.02;

struct Case {
    const char* label;
    const char* family;     // empty for the face that comes from bytes
    const char* filePath;   // set only for the bytes case
    bool bold;
    bool rtl;
    float size;
    const char* ascii;      // or null, when codepoints is used
    uint32_t codepoints[8];
    unsigned codepointCount;
};

// The seven cases spike/textpixel drove across the ABI split, in its order.
static const Case cases[] = {
    { "latin kerned", "Helvetica", nullptr, false, false, 16, "Waving AV To fluffy", { }, 0 },
    { "system font", "Lucida Grande", nullptr, false, false, 13, "Waving AV To fluffy", { }, 0 },
    { "bold request", "Helvetica", nullptr, true, false, 16, "Waving AV To fluffy", { }, 0 },
    { "arabic via Helvetica", "Helvetica", nullptr, false, true, 16, nullptr, { 0x0627, 0x0628, 0x062C, 0x062F }, 4 },
    { "cjk via Helvetica", "Helvetica", nullptr, false, false, 16, nullptr, { 0x4E2D, 0x6587, 0x6E2C, 0x8A66 }, 4 },
    { "font from bytes", "", "/System/Library/Fonts/Geeza Pro.ttf", false, true, 16, nullptr, { 0x0627, 0x0628, 0x062C, 0x062F }, 4 },
    { "latin rejected by shaper", "Helvetica", nullptr, false, false, 16, "Web font AV", { }, 0 },
};
static const unsigned caseCount = sizeof(cases) / sizeof(cases[0]);

static std::vector<uint32_t> textOf(const Case& c)
{
    std::vector<uint32_t> text;
    if (c.ascii) {
        for (const char* p = c.ascii; *p; ++p)
            text.push_back(static_cast<uint32_t>(static_cast<unsigned char>(*p)));
    } else {
        for (unsigned i = 0; i < c.codepointCount; ++i)
            text.push_back(c.codepoints[i]);
    }
    return text;
}

// ---- FreeType, on the real paths the manifest records ---------------------
//
// FreeType is what opens these files, not HarfBuzz's own blob reader: 76 of the
// box's 176 faces live in a resource fork or a .dfont suitcase, which FT_New_Face
// understands and a raw sfnt parser does not.

static FT_Library library;

static hb_face_t* faceFor(const std::string& path, int index)
{
    static std::map<std::string, hb_face_t*> cache;
    std::string key = path + ":" + std::to_string(index);
    std::map<std::string, hb_face_t*>::iterator it = cache.find(key);
    if (it != cache.end())
        return it->second;

    FT_Face ftFace = nullptr;
    if (FT_New_Face(library, path.c_str(), index, &ftFace)) {
        fprintf(stderr, "  FreeType cannot open %s:%d\n", path.c_str(), index);
        cache[key] = nullptr;
        return nullptr;
    }
    hb_face_t* face = hb_ft_face_create_referenced(ftFace);
    FT_Done_Face(ftFace);
    cache[key] = face;
    return face;
}

// ---- shaping, under the two rules ------------------------------------------

struct Shaped {
    std::vector<uint16_t> glyphs;
    std::vector<double> x;
};

// scaleShift is the fraction of a point HarfBuzz rounds positions into: 16 is
// the 16.16 the real ComplexTextController uses, 6 is the conventional 26.6 the
// tolerance check below proves is not good enough.
enum Kerning {
    TigerRules,          // shape with -kern, apply the Apple table ourselves
    HarfBuzzKerning,     // leave the kern feature on and let HarfBuzz decide
    NoKerning,           // -kern and nothing applied: the control
};

static bool shape(hb_face_t* face, const std::vector<uint32_t>& text, float size, bool rtl,
    Shaped& out, int scaleShift = 16, Kerning kerning = TigerRules)
{
    if (!face)
        return false;
    hb_font_t* font = hb_font_create(face);
    int scale = static_cast<int>(size * (1 << scaleShift));
    hb_font_set_scale(font, scale, scale);
    hb_font_set_ppem(font, 0, 0);

    TigerAppleKern kern(face);
    bool suppressHarfBuzzKerning = kerning != HarfBuzzKerning && kern.isAppleFormat();

    hb_buffer_t* buffer = hb_buffer_create();
    for (size_t i = 0; i < text.size(); ++i)
        hb_buffer_add(buffer, text[i], static_cast<unsigned>(i));
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_direction(buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);

    if (suppressHarfBuzzKerning) {
        hb_feature_t noKern;
        hb_feature_from_string("-kern", -1, &noKern);
        hb_shape(font, buffer, &noKern, 1);
        if (kerning == TigerRules)
            kern.applyToShapedBuffer(buffer, font);
    } else
        hb_shape(font, buffer, nullptr, 0);

    unsigned count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    double pen = 0;
    out.glyphs.clear();
    out.x.clear();
    for (unsigned i = 0; i < count; ++i) {
        out.glyphs.push_back(static_cast<uint16_t>(infos[i].codepoint));
        out.x.push_back(pen + positions[i].x_offset / static_cast<double>(1 << scaleShift));
        pen += positions[i].x_advance / static_cast<double>(1 << scaleShift);
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    return count;
}

// ---- the cache's decision ---------------------------------------------------

static const TigerFontDatabase::Face* chooseFace(const TigerFontDatabase& database, const Case& c,
    const std::vector<uint32_t>& text)
{
    if (c.filePath && *c.filePath) {
        // A face named by bytes rather than by family: a web font, in the real
        // port. It still has to be checked for coverage here, because the
        // rasteriser draws whatever it is handed, .notdef included.
        for (size_t i = 0; i < database.faces().size(); ++i) {
            if (database.faces()[i].path == c.filePath)
                return &database.faces()[i];
        }
        return nullptr;
    }
    return database.firstFaceCovering(c.family, c.bold, false, &text[0], text.size(), "en");
}

// ---- the reference ----------------------------------------------------------

struct Reference {
    std::string postScriptName;
    std::vector<uint16_t> glyphs;
    std::vector<double> x;
};

static bool readReference(const char* path, std::map<std::string, Reference>& out)
{
    FILE* file = fopen(path, "r");
    if (!file)
        return false;
    char line[8192];
    while (fgets(line, sizeof(line), file)) {
        char* label = strtok(line, "\t");
        char* name = strtok(nullptr, "\t");
        char* rest = strtok(nullptr, "\n");
        if (!label || !name || !rest)
            continue;
        Reference reference;
        reference.postScriptName = name;
        for (char* token = strtok(rest, " "); token; token = strtok(nullptr, " ")) {
            unsigned glyph = 0;
            double x = 0;
            if (sscanf(token, "%u:%lf", &glyph, &x) == 2) {
                reference.glyphs.push_back(static_cast<uint16_t>(glyph));
                reference.x.push_back(x);
            }
        }
        out[label] = reference;
    }
    fclose(file);
    return !out.empty();
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: tigerfontcachetest <manifest> <ct-reference.txt | --record>\n");
        return 2;
    }
    bool record = !strcmp(argv[2], "--record");

    if (FT_Init_FreeType(&library)) {
        fprintf(stderr, "FreeType will not start\n");
        return 1;
    }
    TigerFontDatabase& database = TigerFontDatabase::singleton();
    if (!database.loadManifest(argv[1])) {
        fprintf(stderr, "cannot read the manifest at %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "manifest: %u faces\n", static_cast<unsigned>(database.faces().size()));

    std::map<std::string, Reference> reference;
    if (!record && !readReference(argv[2], reference)) {
        fprintf(stderr, "cannot read the reference at %s\n", argv[2]);
        return 1;
    }

    unsigned failures = 0;
    for (unsigned i = 0; i < caseCount; ++i) {
        const Case& c = cases[i];
        std::vector<uint32_t> text = textOf(c);
        const TigerFontDatabase::Face* face = chooseFace(database, c, text);
        if (!face) {
            fprintf(stderr, "%-26s NO FACE COVERS IT\n", c.label);
            ++failures;
            continue;
        }

        if (record) {
            printf("%s\t%s\t%g\t%d", c.label, face->postScriptName.c_str(), c.size, c.rtl ? 1 : 0);
            for (size_t k = 0; k < text.size(); ++k)
                printf("\t%u", text[k]);
            printf("\n");
            continue;
        }

        Shaped shaped;
        if (!shape(faceFor(face->path, face->faceIndex), text, c.size, c.rtl, shaped)) {
            fprintf(stderr, "%-26s SHAPED NOTHING (%s)\n", c.label, face->path.c_str());
            ++failures;
            continue;
        }

        std::map<std::string, Reference>::iterator it = reference.find(c.label);
        if (it == reference.end()) {
            fprintf(stderr, "%-26s no reference recorded\n", c.label);
            ++failures;
            continue;
        }
        const Reference& want = it->second;

        unsigned problems = 0;
        if (want.postScriptName != face->postScriptName) {
            fprintf(stderr, "%-26s CHOSE %s, reference is %s\n", c.label,
                face->postScriptName.c_str(), want.postScriptName.c_str());
            ++problems;
        }
        if (want.glyphs.size() != shaped.glyphs.size()) {
            fprintf(stderr, "%-26s %u glyphs, CoreText made %u\n", c.label,
                static_cast<unsigned>(shaped.glyphs.size()), static_cast<unsigned>(want.glyphs.size()));
            ++problems;
        }
        double worst = 0;
        unsigned worstGlyph = 0;
        size_t common = want.glyphs.size() < shaped.glyphs.size() ? want.glyphs.size() : shaped.glyphs.size();
        for (size_t k = 0; k < common; ++k) {
            if (!shaped.glyphs[k]) {
                fprintf(stderr, "%-26s .notdef at glyph %u\n", c.label, static_cast<unsigned>(k));
                ++problems;
            }
            if (shaped.glyphs[k] != want.glyphs[k]) {
                fprintf(stderr, "%-26s glyph %u is %u, CoreText chose %u\n", c.label,
                    static_cast<unsigned>(k), shaped.glyphs[k], want.glyphs[k]);
                ++problems;
            }
            double delta = fabs(shaped.x[k] - want.x[k]);
            if (delta > worst) {
                worst = delta;
                worstGlyph = static_cast<unsigned>(k);
            }
        }
        if (worst > kTolerance) {
            fprintf(stderr, "%-26s worst shift %.4f pt at glyph %u, over %.2f\n",
                c.label, worst, worstGlyph, kTolerance);
            ++problems;
        }
        printf("%-26s %-16s %2u gl  worst shift %.5f pt  %s\n", c.label,
            face->postScriptName.c_str(), static_cast<unsigned>(shaped.glyphs.size()),
            worst, problems ? "FAIL" : "ok");
        if (problems)
            ++failures;
    }

    if (record) {
        FT_Done_FreeType(library);
        return 0;
    }

    // The two rules, checked for teeth: if either stops mattering, this test
    // would pass for the wrong reason. Both of these must be *worse* than the
    // tolerance the cases above meet.
    {
        const Case& c = cases[0];
        std::vector<uint32_t> text = textOf(c);
        const TigerFontDatabase::Face* face = chooseFace(database, c, text);
        const Reference& want = reference[c.label];
        struct { const char* what; int shift; Kerning kerning; } controls[] = {
            { "26.6 scale (rule 2 off)", 6, TigerRules },
            { "kern table ignored (rule 1 off)", 16, NoKerning },
        };
        for (unsigned k = 0; k < sizeof(controls) / sizeof(controls[0]); ++k) {
            Shaped shaped;
            shape(faceFor(face->path, face->faceIndex), text, c.size, c.rtl, shaped,
                controls[k].shift, controls[k].kerning);
            double worst = 0;
            for (size_t g = 0; g < shaped.x.size() && g < want.x.size(); ++g)
                worst = fabs(shaped.x[g] - want.x[g]) > worst ? fabs(shaped.x[g] - want.x[g]) : worst;
            bool detected = worst > kTolerance;
            printf("control: %-30s worst shift %.5f pt  %s\n", controls[k].what, worst,
                detected ? "detected" : "NOT DETECTED");
            if (!detected)
                ++failures;
        }
    }

    FT_Done_FreeType(library);
    printf("%u case%s, %u failure%s\n", caseCount, caseCount == 1 ? "" : "s",
        failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
