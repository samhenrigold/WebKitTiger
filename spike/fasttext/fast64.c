/* TIGER fasttext: what fast mode's text can be made to look like.
 *
 * Rasterises the exact glyph run CoreText chose (spike/fasttext/ref.glyphs,
 * written by ctref32) with cairo + FreeType out of the manifest font files,
 * once per option variant, and scores each against the CoreText bitmap.
 *
 * Score is two numbers:
 *   luma   mean |luma(variant) - luma(reference)| over every pixel, 0..255
 *   ink    total ink (255-luma) of the variant over that of the reference; 1.00
 *          means the strokes carry the same weight, <1 means lighter than Quartz
 *
 * Build: see Makefile. Run on the box:  ./fast64 <manifest> <outdir>
 */
#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_MODULE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sample.h"

/* cairo's LCD filter knob is private API, but this is a static libcairo and the
 * symbol is right there; fontconfig's FC_LCD_FILTER is the supported spelling
 * and is what FontCacheTiger64 would use. */
typedef enum { LCD_DEFAULT, LCD_NONE, LCD_INTRA_PIXEL, LCD_FIR3, LCD_FIR5 } lcd_filter_t;
extern void _cairo_font_options_set_lcd_filter(cairo_font_options_t*, int);

/* ---- manifest: postScriptName -> file, index ----------------------------- */
#define MAXFACES 512
static struct { char psName[128]; char path[320]; int index; } g_faces[MAXFACES];
static int g_faceCount;

static int readField(const char* s, const char* e, const char* key, char* out, size_t n)
{
    char pat[64]; const char* p; size_t i = 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(s, pat);
    if (!p || p >= e) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    ++p;
    while (*p == ' ') ++p;
    if (*p == '"') {
        ++p;
        while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
    } else {
        while (*p && *p != ',' && *p != '}' && *p != '\n' && i + 1 < n) out[i++] = *p++;
    }
    out[i] = 0;
    return 1;
}

static int loadManifest(const char* path)
{
    FILE* f = fopen(path, "rb");
    char* buf; long size; const char* p;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (char*)malloc(size + 1);
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); return 0; }
    fclose(f); buf[size] = 0;
    p = strstr(buf, "\"faces\"");
    if (!p) return 0;
    while (g_faceCount < MAXFACES) {
        const char* s = strchr(p, '{'); const char* e;
        char idx[32];
        if (!s) break;
        e = strchr(s, '}');
        if (!e) break;
        /* face objects contain a nested "traits" object; take the outer one by
         * scanning to the closing brace of the last nested object instead. */
        { const char* q = s + 1; int depth = 1;
          while (*q && depth) { if (*q == '{') ++depth; else if (*q == '}') --depth; ++q; }
          e = q; }
        if (readField(s, e, "postScriptName", g_faces[g_faceCount].psName, 128)
            && readField(s, e, "path", g_faces[g_faceCount].path, 320)
            && readField(s, e, "faceIndex", idx, sizeof(idx))) {
            g_faces[g_faceCount].index = atoi(idx);
            if (g_faces[g_faceCount].index >= 0) ++g_faceCount;
        }
        p = e;
    }
    return g_faceCount;
}

static int faceForName(const char* psName)
{
    int i;
    for (i = 0; i < g_faceCount; ++i)
        if (!strcmp(g_faces[i].psName, psName)) return i;
    return -1;
}

/* ---- the reference layout ------------------------------------------------ */
#define MAXRUNS 64
#define MAXGLYPHS 256
static struct { int line; char psName[128]; double size; int n; cairo_glyph_t g[MAXGLYPHS]; } g_runs[MAXRUNS];
static int g_runCount;

static int loadRuns(const char* path)
{
    FILE* f = fopen(path, "r");
    char line[16384];
    if (!f) return 0;
    while (fgets(line, sizeof(line), f) && g_runCount < MAXRUNS) {
        char* save = NULL;
        char* tok = strtok_r(line, "\t\n", &save);
        int k = 0;
        if (!tok) continue;
        g_runs[g_runCount].line = atoi(tok);
        tok = strtok_r(NULL, "\t\n", &save);
        snprintf(g_runs[g_runCount].psName, 128, "%s", tok ? tok : "");
        tok = strtok_r(NULL, "\t\n", &save);
        g_runs[g_runCount].size = tok ? atof(tok) : 0;
        tok = strtok_r(NULL, "\t\n", &save);   /* count, re-derived below */
        while ((tok = strtok_r(NULL, "\t\n", &save)) && k < MAXGLYPHS) {
            unsigned gid; double x, y;
            if (sscanf(tok, "%u,%lf,%lf", &gid, &x, &y) != 3) break;
            g_runs[g_runCount].g[k].index = gid;
            g_runs[g_runCount].g[k].x = x;
            g_runs[g_runCount].g[k].y = y;
            ++k;
        }
        g_runs[g_runCount].n = k;
        if (k) ++g_runCount;
    }
    fclose(f);
    return g_runCount;
}

/* ---- variants ------------------------------------------------------------ */
typedef struct {
    const char* name;
    cairo_antialias_t aa;
    cairo_hint_style_t hint;
    int lcdFilter;        /* lcd_filter_t */
    int forceAutohint;
    int stemDarken;
    double embolden;      /* second pass offset in px; 0 = off */
    int intPos;           /* 1 = round x to whole px, 2 = floor to 1/4 px, 3 = round to 1/4 px */
    double gamma;         /* applied to coverage; <1 darkens */
    double contrast;      /* S-curve on coverage; >1 sharpens edges, 1 = off */
    double dx, dy;        /* global subpixel shift, to test for a phase error */
} Variant;

static FT_Library g_ft;

static cairo_surface_t* render(const Variant* v)
{
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, FT_W, FT_H);
    cairo_t* cr = cairo_create(surf);
    cairo_font_options_t* opts = cairo_font_options_create();
    int i;

    { FT_Bool no = v->stemDarken ? 0 : 1;
      FT_Property_Set(g_ft, "cff", "no-stem-darkening", &no);
      FT_Property_Set(g_ft, "autofitter", "no-stem-darkening", &no);
      FT_Property_Set(g_ft, "type1", "no-stem-darkening", &no); }

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);

    cairo_font_options_set_antialias(opts, v->aa);
    cairo_font_options_set_hint_style(opts, v->hint);
    cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_OFF);
    if (v->aa == CAIRO_ANTIALIAS_SUBPIXEL)
        cairo_font_options_set_subpixel_order(opts, CAIRO_SUBPIXEL_ORDER_RGB);
    _cairo_font_options_set_lcd_filter(opts, v->lcdFilter);
    cairo_set_font_options(cr, opts);

    for (i = 0; i < g_runCount; ++i) {
        int fi = faceForName(g_runs[i].psName);
        FT_Face face;
        cairo_font_face_t* ff;
        int flags = FT_LOAD_DEFAULT | (v->forceAutohint ? FT_LOAD_FORCE_AUTOHINT : 0);
        if (fi < 0) { fprintf(stderr, "no manifest face for %s\n", g_runs[i].psName); continue; }
        if (FT_New_Face(g_ft, g_faces[fi].path, g_faces[fi].index, &face)) {
            fprintf(stderr, "FT_New_Face failed: %s\n", g_faces[fi].path); continue;
        }
        /* ponytail: faces are leaked, one per run per variant. This is a
         * short-lived measurement program; the real cache owns them properly. */
        ff = cairo_ft_font_face_create_for_ft_face(face, flags);
        cairo_set_font_face(cr, ff);
        cairo_set_font_size(cr, g_runs[i].size);
        if (v->intPos) {
            cairo_glyph_t tmp[MAXGLYPHS]; int k;
            memcpy(tmp, g_runs[i].g, sizeof(cairo_glyph_t) * g_runs[i].n);
            for (k = 0; k < g_runs[i].n; ++k) {
                if (v->intPos == 1) tmp[k].x = floor(tmp[k].x + 0.5);
                else if (v->intPos == 2) tmp[k].x = floor(tmp[k].x * 4.0) / 4.0;
                else if (v->intPos == 3) tmp[k].x = floor(tmp[k].x * 4.0 + 0.5) / 4.0;
                tmp[k].x += v->dx;
            }
            cairo_show_glyphs(cr, tmp, g_runs[i].n);
        } else if (v->dx != 0 || v->dy != 0) {
            cairo_glyph_t tmp[MAXGLYPHS]; int k;
            memcpy(tmp, g_runs[i].g, sizeof(cairo_glyph_t) * g_runs[i].n);
            for (k = 0; k < g_runs[i].n; ++k) { tmp[k].x += v->dx; tmp[k].y += v->dy; }
            cairo_show_glyphs(cr, tmp, g_runs[i].n);
        } else
            cairo_show_glyphs(cr, g_runs[i].g, g_runs[i].n);
        if (v->embolden > 0) {
            int k;
            cairo_glyph_t tmp[MAXGLYPHS];
            memcpy(tmp, g_runs[i].g, sizeof(cairo_glyph_t) * g_runs[i].n);
            for (k = 0; k < g_runs[i].n; ++k) tmp[k].x += v->embolden;
            cairo_show_glyphs(cr, tmp, g_runs[i].n);
        }
        cairo_font_face_destroy(ff);
    }
    cairo_font_options_destroy(opts);
    cairo_destroy(cr);
    cairo_surface_flush(surf);

    if (v->gamma != 1.0 || v->contrast != 1.0) {
        unsigned char lut[256];
        unsigned char* d = cairo_image_surface_get_data(surf);
        int stride = cairo_image_surface_get_stride(surf), x, y;
        for (x = 0; x < 256; ++x) {
            /* coverage = 1 - value (black ink on white); gamma the coverage */
            double c = 1.0 - x / 255.0;
            if (v->contrast != 1.0 && c > 0 && c < 1) {
                double a = pow(c, v->contrast), b = pow(1.0 - c, v->contrast);
                c = a / (a + b);
            }
            c = pow(c, v->gamma);
            lut[x] = (unsigned char)(255.0 * (1.0 - c) + 0.5);
        }
        for (y = 0; y < FT_H; ++y)
            for (x = 0; x < FT_W * 4; ++x)
                if ((x & 3) != 3) d[y * stride + x] = lut[d[y * stride + x]];
        cairo_surface_mark_dirty(surf);
    }
    return surf;
}

/* ---- scoring ------------------------------------------------------------- */
static long g_refSoft, g_refSolid;
static unsigned char* g_ref;  /* FT_W*FT_H*4 RGBA */

static int loadRef(const char* path)
{
    FILE* f = fopen(path, "rb");
    char magic[4]; int32_t d[2];
    if (!f) return 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, FT_MAGIC, 4)) { fclose(f); return 0; }
    if (fread(d, 4, 2, f) != 2 || d[0] != FT_W || d[1] != FT_H) { fclose(f); return 0; }
    g_ref = (unsigned char*)malloc((size_t)FT_W * FT_H * 4);
    if (fread(g_ref, 4, (size_t)FT_W * FT_H, f) != (size_t)FT_W * FT_H) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static double luma(double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }

/* partial[0] = pixels with any ink but not solid, partial[1] = near-solid. Tells
 * whether we are spreading the same ink over more pixels than Quartz does. */
static void coverageShape(const unsigned char* d, int stride, int cairoOrder, long* soft, long* solid)
{
    int x, y;
    *soft = *solid = 0;
    for (y = 0; y < FT_H; ++y)
        for (x = 0; x < FT_W; ++x) {
            const unsigned char* p = d + (stride ? y * stride + x * 4 : ((size_t)y * FT_W + x) * 4);
            double l = cairoOrder ? luma(p[2], p[1], p[0]) : luma(p[0], p[1], p[2]);
            if (l < 32) ++*solid;
            else if (l < 250) ++*soft;
        }
}

static void score(cairo_surface_t* s, double* meanErr, double* inkErr, double* inkRatio, double* lineInk)
{
    const unsigned char* d = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s), x, y, i;
    double err = 0, inkV = 0, inkR = 0, errInk = 0; long nInk = 0;
    double lv[FT_LINES], lr[FT_LINES];
    for (i = 0; i < FT_LINES; ++i) lv[i] = lr[i] = 0;
    for (y = 0; y < FT_H; ++y) {
        int band = (int)((y - (FT_LINE0 - FT_LEADING * 0.7)) / FT_LEADING);
        for (x = 0; x < FT_W; ++x) {
            const unsigned char* p = d + y * stride + x * 4;    /* cairo: BGRA */
            const unsigned char* q = g_ref + ((size_t)y * FT_W + x) * 4; /* CG: RGBA */
            double a = luma(p[2], p[1], p[0]), b = luma(q[0], q[1], q[2]);
            err += fabs(a - b);
            if (a < 253 || b < 253) { errInk += fabs(a - b); ++nInk; }
            inkV += 255 - a; inkR += 255 - b;
            if (band >= 0 && band < FT_LINES) { lv[band] += 255 - a; lr[band] += 255 - b; }
        }
    }
    *meanErr = err / (FT_W * (double)FT_H);
    *inkErr = nInk ? errInk / nInk : 0;
    *inkRatio = inkR > 0 ? inkV / inkR : 0;
    for (i = 0; i < FT_LINES; ++i) lineInk[i] = lr[i] > 0 ? lv[i] / lr[i] : 0;
}

/* ---- main ---------------------------------------------------------------- */
int main(int argc, char** argv)
{
    const char* manifest = argc > 1 ? argv[1] : "tiger-fonts.json";
    const char* dir = argc > 2 ? argv[2] : ".";
    const char* refName = argc > 3 ? argv[3] : "ref-smooth.bin";
    char path[512];
    Variant vs[64]; int nv = 0, i; int coloured[64]; double softRatio[64], solidRatio[64];
    double bestErr[64]; int order[64];
    static const struct { const char* n; cairo_hint_style_t h; } hints[] = {
        { "none", CAIRO_HINT_STYLE_NONE }, { "slight", CAIRO_HINT_STYLE_SLIGHT }, { "full", CAIRO_HINT_STYLE_FULL }
    };
    static const double gammas[] = { 1.0, 0.85, 0.7, 0.55 };
    int hi, gi;

    if (FT_Init_FreeType(&g_ft)) { fprintf(stderr, "no freetype\n"); return 1; }
    {   /* Does this FreeType even have the ClearType-style LCD filter? */
        FT_Error e = FT_Library_SetLcdFilter(g_ft, FT_LCD_FILTER_DEFAULT);
        printf("# FT_Library_SetLcdFilter(DEFAULT) -> %d (%s)\n", e,
            e ? "unimplemented: FreeType is in Harmony mode" : "available");
        FT_Library_SetLcdFilter(g_ft, FT_LCD_FILTER_NONE);
    }
    if (!loadManifest(manifest)) { fprintf(stderr, "no manifest %s\n", manifest); return 1; }
    snprintf(path, sizeof(path), "%s/ref.glyphs", dir);
    if (!loadRuns(path)) { fprintf(stderr, "no %s\n", path); return 1; }
    snprintf(path, sizeof(path), "%s/%s", dir, refName);
    if (!loadRef(path)) { fprintf(stderr, "no %s\n", path); return 1; }
    printf("# %d faces, %d runs, reference %s\n", g_faceCount, g_runCount, refName);
    {   long soft, solid;
        coverageShape(g_ref, 0, 0, &soft, &solid);
        printf("# reference coverage: %ld soft px, %ld solid px, soft/solid %.2f\n", soft, solid, solid ? (double)soft / solid : 0);
        g_refSoft = soft; g_refSolid = solid; }

    for (hi = 0; hi < 3; ++hi)
        for (gi = 0; gi < 4; ++gi) {
            static char names[64][64];
            Variant v = { NULL, CAIRO_ANTIALIAS_GRAY, hints[hi].h, LCD_DEFAULT, 0, 0, 0, 0, gammas[gi], 1.0 };
            snprintf(names[nv], 64, "gray-%s-g%.2f", hints[hi].n, gammas[gi]);
            v.name = names[nv]; vs[nv++] = v;
        }
    for (hi = 0; hi < 3; ++hi) {
        static char names[8][64];
        Variant v = { NULL, CAIRO_ANTIALIAS_SUBPIXEL, hints[hi].h, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.0 };
        snprintf(names[hi], 64, "rgb-%s-g1.00", hints[hi].n);
        v.name = names[hi]; vs[nv++] = v;
    }
    {   Variant v = { "rgb-slight-fir5-g0.85", CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_HINT_STYLE_SLIGHT, LCD_FIR5, 0, 0, 0, 0, 0.85, 1.0 };
        vs[nv++] = v; }
    {   Variant v = { "rgb-slight-light-g0.85", CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_HINT_STYLE_SLIGHT, LCD_FIR3, 0, 0, 0, 0, 0.85, 1.0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-slight-autohint-darken", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_SLIGHT, LCD_DEFAULT, 1, 1, 0, 0, 1.0, 1.0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-darken", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 1, 0, 0, 1.0, 1.0 };
        vs[nv++] = v; }
    if (argc > 4 && !strcmp(argv[4], "sweep")) {
        static char names[64][64];
        static const double off[] = { -0.375, -0.3125, -0.25, -0.1875, -0.125, -0.0625, 0, 0.0625, 0.125, 0.25 };
        static const double yoff[] = { 0 };
        int a, b;
        for (a = 0; a < 10; ++a)
            for (b = 0; b < 1; ++b) {
                Variant v = { NULL, CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.0, off[a], yoff[b] };
                snprintf(names[nv], 64, "shift-dx%+.3f-dy%+.3f", off[a], yoff[b]);
                v.name = names[nv]; vs[nv++] = v;
            }
    } else {
    {   Variant v = { "gray-none-q4floor", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 2, 1.0, 1.0, 0, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-q4round", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 3, 1.0, 1.0, 0, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-slight-q4floor", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_SLIGHT, LCD_DEFAULT, 0, 0, 0, 2, 1.0, 1.0, 0, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-dx0.125", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.0, -0.125, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-slight-dx0.125", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_SLIGHT, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.0, -0.125, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-full-dx0.125", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_FULL, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.0, -0.125, 0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-contrast1.3", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.3 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-contrast1.6", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 1.6 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-contrast0.8", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 1.0, 0.8 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-contrast1.3-g0.92", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 0, 0.92, 1.3 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-intpos", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0, 1, 1.0, 1.0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-none-embolden0.3", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_NONE, LCD_DEFAULT, 0, 0, 0.3, 0, 1.0, 1.0 };
        vs[nv++] = v; }
    {   Variant v = { "gray-slight-embolden0.3-g0.85", CAIRO_ANTIALIAS_GRAY, CAIRO_HINT_STYLE_SLIGHT, LCD_DEFAULT, 0, 0, 0.3, 0, 0.85, 1.0 };
        vs[nv++] = v; }
    }

    printf("%-32s %8s %8s %8s %5s %5s %3s  per-line ink\n", "variant", "luma", "inkluma", "ink", "soft", "solid", "clr");
    for (i = 0; i < nv; ++i) {
        cairo_surface_t* s = render(&vs[i]);
        double err, ierr, ink, lineInk[FT_LINES]; int k;
        score(s, &err, &ierr, &ink, lineInk);
        {   const unsigned char* dd = cairo_image_surface_get_data(s);
            int st = cairo_image_surface_get_stride(s), x, y, col = 0;
            for (y = 0; y < FT_H && !col; ++y)
                for (x = 0; x < FT_W; ++x) {
                    const unsigned char* pp = dd + y * st + x * 4;
                    if (pp[0] != pp[1] || pp[1] != pp[2]) { col = 1; break; }
                }
            if (col) vs[i].name = vs[i].name;   /* reported in the colour column */
            coloured[i] = col; }
        {   long soft, solid;
            coverageShape(cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s), 1, &soft, &solid);
            softRatio[i] = g_refSoft ? (double)soft / g_refSoft : 0;
            solidRatio[i] = g_refSolid ? (double)solid / g_refSolid : 0; }
        snprintf(path, sizeof(path), "%s/v-%s.png", dir, vs[i].name);
        cairo_surface_write_to_png(s, path);
        printf("%-32s %8.3f %8.2f %8.3f %5.2f %5.2f %3s ", vs[i].name, err, ierr, ink,
            softRatio[i], solidRatio[i], coloured[i] ? "rgb" : "-");
        for (k = 0; k < FT_LINES; ++k) printf(" %.2f", lineInk[k]);
        printf("\n");
        fflush(stdout);
        bestErr[i] = err; order[i] = i;
        cairo_surface_destroy(s);
    }

    /* reference as a PNG, and a contact sheet: reference on top, best three under it */
    {
        cairo_surface_t* ref = cairo_image_surface_create(CAIRO_FORMAT_RGB24, FT_W, FT_H);
        unsigned char* d = cairo_image_surface_get_data(ref);
        int stride = cairo_image_surface_get_stride(ref), x, y, a, b, t;
        for (y = 0; y < FT_H; ++y)
            for (x = 0; x < FT_W; ++x) {
                const unsigned char* q = g_ref + ((size_t)y * FT_W + x) * 4;
                unsigned char* p = d + y * stride + x * 4;
                p[2] = q[0]; p[1] = q[1]; p[0] = q[2]; p[3] = 255;
            }
        cairo_surface_mark_dirty(ref);
        snprintf(path, sizeof(path), "%s/v-reference.png", dir);
        cairo_surface_write_to_png(ref, path);

        for (a = 0; a < nv; ++a)
            for (b = a + 1; b < nv; ++b)
                if (bestErr[order[b]] < bestErr[order[a]]) { t = order[a]; order[a] = order[b]; order[b] = t; }

        {
            cairo_surface_t* sheet = cairo_image_surface_create(CAIRO_FORMAT_RGB24, FT_W, FT_H * 4 + 12);
            cairo_t* cr = cairo_create(sheet);
            cairo_set_source_rgb(cr, 0.6, 0.6, 0.6); cairo_paint(cr);
            cairo_set_source_surface(cr, ref, 0, 0); cairo_paint(cr);
            for (i = 0; i < 3 && i < nv; ++i) {
                cairo_surface_t* s = render(&vs[order[i]]);
                cairo_set_source_surface(cr, s, 0, (FT_H + 4) * (i + 1));
                cairo_paint(cr);
                cairo_surface_destroy(s);
                printf("# sheet band %d: %s (luma %.3f)\n", i + 1, vs[order[i]].name, bestErr[order[i]]);
            }
            cairo_destroy(cr);
            snprintf(path, sizeof(path), "%s/contact.png", dir);
            cairo_surface_write_to_png(sheet, path);
            cairo_surface_destroy(sheet);
        }
        cairo_surface_destroy(ref);
    }
    printf("# best: %s\n", vs[order[0]].name);
    return 0;
}
