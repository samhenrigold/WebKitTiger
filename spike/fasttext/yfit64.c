/*
 * yfit64: replay ctref32's layout (ref.glyphs) through FreeType alone, on the host, under one
 * vertical-fit rule per run, and write a PGM to score against the CoreText bitmap. It exists to
 * find what Quartz does to glyph outlines vertically at integral ppem without a 50-minute
 * WebKit build per hypothesis.
 *
 *   yfit64 <rule> <ref.glyphs> <out.pgm> <psName=file:index>...
 *
 * rules:  none      unhinted outline, x floored to 1/N px, y whole pixels (what TigerGlyphSnap does)
 *         ftY       y from the font's bytecode (FT v40), x unhinted
 *         bboxY     unhinted outline scaled about the baseline so its top lands where the
 *                   bytecode puts it (per glyph); the same for the bottom, separately
 *         capY      unhinted outline scaled about the baseline by hinted/unhinted cap height of 'H'
 *         scale:U[,D]  explicit factors above/below the baseline, for sweeps
 *         zones     piecewise-linear: x-height ('o' top) and cap ('H' top) each to round(), baseline
 *                   fixed, slope 1 above the cap;  zones+ only when the x-height stretches
 *         zonesX    x-height knot only, everything above rides along;  zonesXF same with the
 *                   bytecode's hinted 'o' top as the target
 *         quartz    zonesX applied only where round() and the bytecode agree on the target --
 *                   the rule TigerGlyphFit.cpp implements
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 720
#define H 264

static unsigned char canvas[W * H];
static FT_Library lib;

struct Face { char name[64]; FT_Face face; };
static struct Face faces[16];
static int nfaces;

static FT_Face faceFor(const char* name)
{
    for (int i = 0; i < nfaces; ++i)
        if (!strcmp(faces[i].name, name)) return faces[i].face;
    return NULL;
}

static int phasesFor(double px) { int n = 1 + (int)((100.0 / 3.0) / px); return n > 5 ? 5 : n; }

/* Unhinted outline copy of glyph g at the face's current size. Caller owns via FT_Outline_Done. */
static int loadOutline(FT_Face face, unsigned g, int flags, FT_Outline* out)
{
    if (FT_Load_Glyph(face, g, flags | FT_LOAD_NO_BITMAP) || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return -1;
    FT_Outline_New(lib, face->glyph->outline.n_points, face->glyph->outline.n_contours, out);
    FT_Outline_Copy(&face->glyph->outline, out);
    return 0;
}

static void bbox(const FT_Outline* o, FT_BBox* b) { FT_Outline_Get_CBox(o, b); }

/* Scale y about the baseline, separately above and below it. */
static void scaleY(FT_Outline* o, double sUp, double sDown)
{
    for (int i = 0; i < o->n_points; ++i) {
        double y = o->points[i].y;
        o->points[i].y = (FT_Pos)lround(y * (y >= 0 ? sUp : sDown));
    }
}

static void blit(FT_Outline* o, int penX, int penY)
{
    FT_BBox b; bbox(o, &b);
    int xMin = (int)floor(b.xMin / 64.0), xMax = (int)ceil(b.xMax / 64.0);
    int yMin = (int)floor(b.yMin / 64.0), yMax = (int)ceil(b.yMax / 64.0);
    int w = xMax - xMin, h = yMax - yMin;
    if (w <= 0 || h <= 0) return;
    unsigned char* buf = calloc(w * h, 1);
    FT_Outline_Translate(o, -xMin * 64, -yMin * 64);
    FT_Bitmap bm; memset(&bm, 0, sizeof bm);
    bm.rows = h; bm.width = w; bm.pitch = w; bm.buffer = buf; bm.num_grays = 256; bm.pixel_mode = FT_PIXEL_MODE_GRAY;
    FT_Outline_Get_Bitmap(lib, o, &bm);
    for (int y = 0; y < h; ++y) {
        int cy = penY - yMax + y;
        if (cy < 0 || cy >= H) continue;
        for (int x = 0; x < w; ++x) {
            int cx = penX + xMin + x;
            if (cx < 0 || cx >= W) continue;
            int v = canvas[cy * W + cx] + buf[y * w + x];
            canvas[cy * W + cx] = v > 255 ? 255 : v;
        }
    }
    free(buf);
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: yfit64 <rule> <ref.glyphs> <out.pgm> <psName=file:index>...\n"); return 1; }
    const char* rule = argv[1];
    FT_Init_FreeType(&lib);
    for (int i = 4; i < argc; ++i) {
        char* eq = strchr(argv[i], '='); char* colon = strrchr(argv[i], ':');
        if (!eq || !colon) continue;
        *eq = 0; *colon = 0;
        strncpy(faces[nfaces].name, argv[i], 63);
        if (FT_New_Face(lib, eq + 1, atoi(colon + 1), &faces[nfaces].face)) { fprintf(stderr, "cannot open %s\n", eq + 1); continue; }
        ++nfaces;
    }
    FILE* f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 1; }
    memset(canvas, 0, sizeof canvas);
    char line[65536];
    while (fgets(line, sizeof line, f)) {
        char* save; char* tok = strtok_r(line, "\t\n", &save);       /* line index */
        char* ps = strtok_r(NULL, "\t\n", &save);
        char* sz = strtok_r(NULL, "\t\n", &save);
        strtok_r(NULL, "\t\n", &save);                              /* glyph count */
        FT_Face face = faceFor(ps);
        if (!face) { fprintf(stderr, "skip %s (no face)\n", ps); continue; }
        double px = atof(sz);
        FT_Set_Char_Size(face, 0, (FT_F26Dot6)lround(px * 64), 72, 72);
        int N = phasesFor(px);
        int integral = fabs(px - round(px)) < 1e-6;
        /* zones: piecewise-linear y map with knots at the x-height ('o' top, overshoot included)
         * and the cap height ('H' top), each rounded to whole pixels; slope 1 beyond the cap and
         * below the baseline. zones+ applies it only when the x-height knot stretches (s >= 1). */
        double xhU = 0, xhH = 0, capU = 0, capH = 0; int zonesOn = 0;
        if ((!strncmp(rule, "zones", 5) || !strcmp(rule, "quartz")) && integral) {
            FT_Outline t; FT_BBox bt;
            if (!loadOutline(face, FT_Get_Char_Index(face, 'o'), FT_LOAD_NO_HINTING, &t)) { bbox(&t, &bt); xhU = bt.yMax / 64.0; FT_Outline_Done(lib, &t); }
            if (!loadOutline(face, FT_Get_Char_Index(face, 'H'), FT_LOAD_NO_HINTING, &t)) { bbox(&t, &bt); capU = bt.yMax / 64.0; FT_Outline_Done(lib, &t); }
            xhH = round(xhU); capH = round(capU);
            zonesOn = xhU > 0 && capU > xhU;
            if (!strcmp(rule, "zones+") && xhH < xhU) zonesOn = 0;
            if (!strcmp(rule, "zonesXF")) {                             /* x-height target from the bytecode instead */
                if (!loadOutline(face, FT_Get_Char_Index(face, 'o'), FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT, &t)) { bbox(&t, &bt); xhH = bt.yMax / 64.0; FT_Outline_Done(lib, &t); }
            }
            if (!strcmp(rule, "quartz")) {
                double ft = xhU;
                if (!loadOutline(face, FT_Get_Char_Index(face, 'o'), FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT, &t)) { bbox(&t, &bt); ft = bt.yMax / 64.0; FT_Outline_Done(lib, &t); }
                if (fabs(ft - xhH) > 1.0 / 64 || fabs(xhH - xhU) < 1.0 / 64) zonesOn = 0;
            }
            if (!strcmp(rule, "zonesX") || !strcmp(rule, "zonesXF") || !strcmp(rule, "quartz")) capH = capU + (xhH - xhU);     /* x-height knot only, caps ride along */
            if (!strcmp(rule, "zonesC")) xhH = xhU * capH / capU;        /* cap knot only, uniform below */
        }
        /* capY: one scale per face and size, from 'H' (glyph for 'H' via cmap). */
        double capUp = 1.0;
        if (!strcmp(rule, "capY") && integral) {
            unsigned gH = FT_Get_Char_Index(face, 'H');
            FT_Outline u, hh; FT_BBox bu, bh;
            if (!loadOutline(face, gH, FT_LOAD_NO_HINTING, &u) && !loadOutline(face, gH, FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT, &hh)) {
                bbox(&u, &bu); bbox(&hh, &bh);
                if (bu.yMax > 0) capUp = (double)bh.yMax / bu.yMax;
                FT_Outline_Done(lib, &u); FT_Outline_Done(lib, &hh);
            }
        }
        while ((tok = strtok_r(NULL, "\t\n", &save))) {
            unsigned g; double x, y;
            if (sscanf(tok, "%u,%lf,%lf", &g, &x, &y) != 3) continue;
            double sx = floor(x * N) / N;
            int penX = (int)floor(sx); int phase = (int)lround((sx - penX) * N);
            int penY = (int)ceil(y);
            FT_Outline o;
            if (loadOutline(face, g, FT_LOAD_NO_HINTING, &o)) continue;
            if (zonesOn) {
                for (int i = 0; i < o.n_points; ++i) {
                    double y = o.points[i].y / 64.0, ny;
                    if (y <= 0) ny = y;
                    else if (y <= xhU) ny = y * xhH / xhU;
                    else if (y <= capU) ny = xhH + (y - xhU) * (capH - xhH) / (capU - xhU);
                    else ny = capH + (y - capU);
                    o.points[i].y = (FT_Pos)lround(ny * 64);
                }
            } else if (integral && strcmp(rule, "none") && strncmp(rule, "zones", 5) && strcmp(rule, "quartz")) {
                FT_Outline hinted;
                if (!loadOutline(face, g, FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT, &hinted)) {
                    if (!strcmp(rule, "ftY") && hinted.n_points == o.n_points) {
                        for (int i = 0; i < o.n_points; ++i) o.points[i].y = hinted.points[i].y;
                    } else if (!strcmp(rule, "bboxY")) {
                        FT_BBox bu, bh; bbox(&o, &bu); bbox(&hinted, &bh);
                        double up = bu.yMax > 0 ? (double)bh.yMax / bu.yMax : 1.0;
                        double down = bu.yMin < 0 ? (double)bh.yMin / bu.yMin : 1.0;
                        scaleY(&o, up, down);
                    } else if (!strcmp(rule, "capY")) {
                        scaleY(&o, capUp, 1.0);
                    } else if (!strncmp(rule, "scale:", 6)) {
                        double up = atof(rule + 6), down = 1.0;   /* scale:<up>[,<down>] explicit factors, for sweeps */
                        const char* c = strchr(rule, ',');
                        if (c) down = atof(c + 1);
                        scaleY(&o, up, down);
                    }
                    FT_Outline_Done(lib, &hinted);
                }
            }
            FT_Outline_Translate(&o, lround(phase * 64.0 / N), 0);
            blit(&o, penX, penY);
            FT_Outline_Done(lib, &o);
        }
    }
    fclose(f);
    FILE* out = fopen(argv[3], "wb");
    fprintf(out, "P5\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) fputc(255 - canvas[i], out);
    fclose(out);
    return 0;
}
