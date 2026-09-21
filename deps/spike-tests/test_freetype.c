#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "DejaVuSans.ttf";
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) { printf("init failed\n"); return 1; }
    FT_Face face;
    if (FT_New_Face(lib, path, 0, &face)) { printf("open failed\n"); return 1; }
    printf("freetype OK: family=%s num_glyphs=%ld units_per_em=%d\n", face->family_name, face->num_glyphs, face->units_per_EM);
    FT_UInt gid = FT_Get_Char_Index(face, 'A');
    if (gid && FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT) == 0) {
        printf("glyph 'A': gid=%u advance=%ld\n", gid, (long)face->glyph->advance.x);
    }
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return 0;
}
