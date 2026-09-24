/* Bounded Tiger FreeType suitcase/resource-fork regression. No font bytes leave
 * the machine: only names, scalar metrics and rendered-glyph checksums are logged. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ft2build.h>
#include FT_FREETYPE_H

struct Result {
    int passed;
    int style;
    long advance;
    unsigned upem, width, rows;
    uint32_t hash;
    char name[96];
};

static int courierStyle(const char* name)
{
    static const char* names[] = { "CourierNewPSMT", "CourierNewPS-BoldMT", "CourierNewPS-ItalicMT", "CourierNewPS-BoldItalicMT" };
    if (name) {
        for (unsigned i = 0; i < 4; ++i) {
            if (!strcmp(name, names[i]))
                return (int)i;
        }
    }
    return -1;
}

static struct Result probe(FT_Library library, const char* label, const char* path, int index, int courier)
{
    struct Result result = { 0, -1, 0, 0, 0, 0, 0, { 0 } };
    FT_Face face = NULL;
    FT_Error opened = FT_New_Face(library, path, index, &face);
    FT_Error sized = -1, mapped = -1, loaded = -1, rendered = -1;
    FT_UInt glyph = 0;
    unsigned ink = 0;
    unsigned long coverage = 0;
    long faces = 0;
    if (!opened) {
        const char* name = FT_Get_Postscript_Name(face);
        snprintf(result.name, sizeof(result.name), "%s", name ? name : "");
        result.style = courierStyle(name);
        result.upem = face->units_per_EM;
        faces = face->num_faces;
        sized = FT_Set_Char_Size(face, 0, 22 * 64, 72, 72);
        mapped = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        if (!sized && !mapped) {
            glyph = FT_Get_Char_Index(face, 'S');
            loaded = FT_Load_Glyph(face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
            if (!loaded && glyph) {
                result.advance = face->glyph->advance.x;
                rendered = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
                if (!rendered) {
                    FT_Bitmap* bitmap = &face->glyph->bitmap;
                    result.width = bitmap->width;
                    result.rows = bitmap->rows;
                    result.hash = UINT32_C(2166136261);
                    if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY && bitmap->buffer && bitmap->pitch > 0
                        && bitmap->width <= (unsigned)bitmap->pitch) {
                        for (unsigned y = 0; y < bitmap->rows; ++y) {
                            const unsigned char* row = bitmap->buffer + (size_t)y * bitmap->pitch;
                            for (unsigned x = 0; x < bitmap->width; ++x) {
                                ink += row[x] != 0;
                                coverage += row[x];
                                result.hash = (result.hash ^ row[x]) * UINT32_C(16777619);
                            }
                        }
                    }
                }
            }
        }
        result.passed = !sized && !mapped && glyph && !loaded && !rendered && result.upem
            && result.advance > 0 && result.width > 0 && result.rows > 0 && ink > 0 && coverage > 0
            && (!courier || (result.style >= 0 && faces == 4));
        if (!courier && !strcmp(label, "monaco"))
            result.passed &= !strcmp(result.name, "Monaco");
        if (!courier && !strcmp(label, "courier-control"))
            result.passed &= !strncmp(result.name, "Courier", 7);
    }
    printf("CASE\t%s\t%d\t%d\t%d\t%s\t%ld\t%u\t%u\t%d\t%d\t%d\t%d\t%ld\t%u\t%u\t%u\t%lu\t%08x\n",
        label, index, result.passed, opened, result.name, faces, result.upem, glyph, sized, mapped, loaded,
        rendered, result.advance, result.width, result.rows, ink, coverage, (unsigned)result.hash);
    if (face)
        FT_Done_Face(face);
    return result;
}

int main(void)
{
    alarm(15);
    setvbuf(stdout, NULL, _IOLBF, 0);
    FT_Library library = NULL;
    FT_Error error = FT_Init_FreeType(&library);
    if (error) {
        fprintf(stderr, "FT_Init_FreeType failed: %d\n", error);
        return 2;
    }
    int major, minor, patch;
    FT_Library_Version(library, &major, &minor, &patch);
    printf("VERSION\t%d.%d.%d\n", major, minor, patch);
    struct Result normal[4], fork[4];
    int failures = 0;
    unsigned normalStyles = 0, forkStyles = 0;
    for (int i = 0; i < 4; ++i) {
        normal[i] = probe(library, "courier-normal", "/Library/Fonts/Courier New", i, 1);
        fork[i] = probe(library, "courier-fork", "/Library/Fonts/Courier New/..namedfork/rsrc", i, 1);
        failures += !normal[i].passed + !fork[i].passed;
        if (normal[i].passed)
            normalStyles |= 1U << normal[i].style;
        if (fork[i].passed)
            forkStyles |= 1U << fork[i].style;
    }
    int aliasesMatch = 1;
    for (int i = 0; i < 4; ++i) {
        if (!normal[i].passed || !fork[i].passed || strcmp(normal[i].name, fork[i].name)
            || normal[i].advance != fork[i].advance || normal[i].upem != fork[i].upem
            || normal[i].width != fork[i].width || normal[i].rows != fork[i].rows || normal[i].hash != fork[i].hash)
            aliasesMatch = 0;
    }
    failures += normalStyles != 15 || forkStyles != 15 || !aliasesMatch;
    struct Result monaco = probe(library, "monaco", "/System/Library/Fonts/Monaco.dfont", 0, 0);
    struct Result courier = probe(library, "courier-control", "/System/Library/Fonts/Courier.dfont", 0, 0);
    failures += !monaco.passed + !courier.passed;
    printf("SUMMARY\t%d\t%u\t%u\t%d\n", failures, normalStyles, forkStyles, aliasesMatch);
    FT_Done_FreeType(library);
    return failures ? 1 : 0;
}
