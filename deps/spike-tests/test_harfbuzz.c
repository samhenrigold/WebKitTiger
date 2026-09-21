/* HarfBuzz smoke test: shapes a Latin ligature and an Arabic string, and shows joining
 * forms differ from isolated-letter shaping. See deps/HARFBUZZ.md for the Tiger-box output
 * (DejaVu Sans -- spike/ctprobe-data/DejaVuSans.ttf -- proves an actual 'fi' GSUB ligature
 * and Arabic initial/medial/final substitution, vs. SF Pro/SF Arabic used in the first pass
 * which didn't carry a 'fi' ligature).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hb.h>
#include <hb-ot.h>

static hb_font_t *g_font;

static hb_font_t *load_font(const char *path) {
    hb_blob_t *blob = hb_blob_create_from_file(path);
    if (hb_blob_get_length(blob) == 0) { printf("failed to read font %s\n", path); exit(1); }
    hb_face_t *face = hb_face_create(blob, 0);
    hb_font_t *font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    hb_font_set_scale(font, 1000, 1000);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return font;
}

/* Returns glyph count and fills gids[] (caller-sized buffer). No explicit feature list --
 * hb_shape's built-in defaults already enable standard ligation (liga) and Arabic joining. */
static unsigned shape(hb_font_t *font, const char *text, hb_direction_t dir, hb_script_t script,
                       unsigned *gids, unsigned max) {
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text, -1, 0, -1);
    hb_buffer_set_direction(buf, dir);
    hb_buffer_set_script(buf, script);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font, buf, NULL, 0);
    unsigned count;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
    unsigned n = count < max ? count : max;
    for (unsigned i = 0; i < n; i++) gids[i] = info[i].codepoint;
    hb_buffer_destroy(buf);
    return count;
}

static void print_glyphs(const char *label, unsigned *gids, unsigned n) {
    printf("%s: %u glyphs:", label, n);
    for (unsigned i = 0; i < n; i++) printf(" %u", gids[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    const char *font_path = argc > 1 ? argv[1] : "DejaVuSans.ttf";
    hb_font_t *font = load_font(font_path);
    unsigned gids[16];

    /* "fi" ligature */
    unsigned n = shape(font, "fi", HB_DIRECTION_LTR, HB_SCRIPT_LATIN, gids, 16);
    print_glyphs("fi", gids, n);
    if (n == 1) printf("  -> ligated into a single glyph\n");
    else printf("  -> NOT ligated (%u glyphs)\n", n);

    /* Arabic "بسم" shaped as a connected run */
    const char *beh = "\xD8\xA8", *seen = "\xD8\xB3", *meem = "\xD9\x85";
    char joined[16]; snprintf(joined, sizeof joined, "%s%s%s", beh, seen, meem);
    unsigned joined_gids[16];
    unsigned jn = shape(font, joined, HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, joined_gids, 16);
    print_glyphs("arabic joined (beh-seen-meem)", joined_gids, jn);

    /* Each letter shaped in isolation (its standalone form) */
    unsigned iso_gids[3];
    unsigned n0 = shape(font, beh, HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, &iso_gids[0], 1);
    unsigned n1 = shape(font, seen, HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, &iso_gids[1], 1);
    unsigned n2 = shape(font, meem, HB_DIRECTION_RTL, HB_SCRIPT_ARABIC, &iso_gids[2], 1);
    printf("arabic isolated: beh=%u(%u) seen=%u(%u) meem=%u(%u)\n",
           iso_gids[0], n0, iso_gids[1], n1, iso_gids[2], n2);

    if (jn == 3) {
        printf("  compare: beh  isolated=%u vs joined(initial)=%u  %s\n",
               iso_gids[0], joined_gids[0], iso_gids[0] != joined_gids[0] ? "(differs)" : "(same)");
        printf("  compare: seen isolated=%u vs joined(medial)=%u  %s\n",
               iso_gids[1], joined_gids[1], iso_gids[1] != joined_gids[1] ? "(differs)" : "(same)");
        printf("  compare: meem isolated=%u vs joined(final)=%u  %s\n",
               iso_gids[2], joined_gids[2], iso_gids[2] != joined_gids[2] ? "(differs)" : "(same)");
    } else {
        printf("  (joined run didn't come out to 3 glyphs, skipping per-letter compare)\n");
    }

    hb_font_destroy(font);
    return 0;
}
