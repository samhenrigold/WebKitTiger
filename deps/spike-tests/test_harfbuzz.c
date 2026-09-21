#include <stdio.h>
#include <stdlib.h>
#include <hb.h>
#include <hb-ot.h>

static void shape_and_print(const char *label, const char *font_path, const char *text, hb_direction_t dir, hb_script_t script) {
    hb_blob_t *blob = hb_blob_create_from_file(font_path);
    if (hb_blob_get_length(blob) == 0) { printf("%s: failed to read font\n", label); return; }
    hb_face_t *face = hb_face_create(blob, 0);
    hb_font_t *font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, 1000, 1000);

    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, -1, 0, -1);
    hb_buffer_set_direction(buf, dir);
    hb_buffer_set_script(buf, script);
    hb_buffer_guess_segment_properties(buf);

    hb_shape(font, buf, NULL, 0);

    unsigned int count;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, &count);

    printf("%s: %u glyphs:", label, count);
    for (unsigned i = 0; i < count; i++) {
        printf(" [gid=%u adv=%d]", info[i].codepoint, pos[i].x_advance);
    }
    printf("\n");

    hb_buffer_destroy(buf);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
}

int main(int argc, char **argv) {
    const char *latin_font = argc > 1 ? argv[1] : "SF-Pro.ttf";
    const char *arabic_font = argc > 2 ? argv[2] : "SFArabic.ttf";
    shape_and_print("fi", latin_font, "fi", HB_DIRECTION_LTR, HB_SCRIPT_LATIN);
    /* Arabic "بسم" (three letters, should join into fewer glyphs with contextual forms) */
    shape_and_print("arabic", arabic_font, "\xD8\xA8\xD8\xB3\xD9\x85", HB_DIRECTION_RTL, HB_SCRIPT_ARABIC);
    return 0;
}
