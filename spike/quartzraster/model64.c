/* TIGER quartzraster: a model of Quartz's glyph rasteriser, scored against
 * Quartz's own glyph bitmaps (spike/quartzraster/glyphs.bin, written by
 * ctglyph32 on the box).
 *
 * The model:
 *   - outline from FreeType, unhinted, scaled to the device pixel size
 *   - the pen's device x is snapped to floor(x*N)/N, N phases per pixel, N from
 *     the measured size table; the pen's device y is snapped to floor(y) in CG's
 *     bottom-up space
 *   - coverage is exact area (CG's scan converter is area-exact to 1/256, proven
 *     by spike/quartzraster/pathprobe32), which is also what FreeType's smooth
 *     rasteriser computes, so FT_Outline_Get_Bitmap is the model's rasteriser
 *
 * usage: model64 <manifest.json> <glyphs.bin> [sweep]
 *   sweep: for every patch, also try every 1/48 px model offset and report the
 *          one that matches best, which says whether the residual is a position
 *          error or a shape error.
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_MODULE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- manifest (same reader as spike/fasttext/fast64.c) ------------------- */
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
    if (*p == '"') { ++p; while (*p && *p != '"' && i + 1 < n) out[i++] = *p++; }
    else { while (*p && *p != ',' && *p != '}' && *p != '\n' && i + 1 < n) out[i++] = *p++; }
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
        const char* s = strchr(p, '{'); const char* e; char idx[32];
        if (!s) break;
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

/* ---- the measured phase table -------------------------------------------- */
/* Horizontal subpixel phases per pixel as a function of the glyph's DEVICE pixel
 * size (spike/quartzraster/ctgrid32, thresholds measured to 1/8 pt):
 *   px <= 100/12   5 phases      px <= 100/6    3
 *   px <= 100/9    4             px <= 100/3    2      else 1
 * i.e. N = min(5, 1 + floor((100/3) / px)). Vertical: N = min(5, 1 + floor((25/3) / px)),
 * which is 1 for every size above 8.33 px, so real text has no vertical subpixel
 * positioning at all. */
static int phasesX(double px) { int n = 1 + (int)floor((100.0 / 3.0) / px); return n > 5 ? 5 : n; }
static int phasesY(double px) { int n = 1 + (int)floor((25.0 / 3.0) / px); return n > 5 ? 5 : n; }

/* ---- the patch file ------------------------------------------------------ */
typedef struct { char ps[128]; float size; int glyph; float dx, dy; unsigned char* cov; } Patch;
static Patch* g_p; static int g_n, g_patch;

static int loadPatches(const char* path)
{
    FILE* f = fopen(path, "rb");
    char magic[4]; int i;
    if (!f) return 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "QGP1", 4)) return 0;
    if (fread(&g_n, 4, 1, f) != 1 || fread(&g_patch, 4, 1, f) != 1) return 0;
    g_p = (Patch*)calloc(g_n, sizeof(Patch));
    for (i = 0; i < g_n; ++i) {
        if (fread(g_p[i].ps, 1, 128, f) != 128) return 0;
        if (fread(&g_p[i].size, 4, 1, f) != 1) return 0;
        if (fread(&g_p[i].glyph, 4, 1, f) != 1) return 0;
        if (fread(&g_p[i].dx, 4, 1, f) != 1) return 0;
        if (fread(&g_p[i].dy, 4, 1, f) != 1) return 0;
        g_p[i].cov = (unsigned char*)malloc(g_patch * g_patch);
        if (fread(g_p[i].cov, 1, g_patch * g_patch, f) != (size_t)(g_patch * g_patch)) return 0;
    }
    fclose(f);
    return g_n;
}

/* ---- the model ----------------------------------------------------------- */
static FT_Library g_ft;
#define MAXCACHE 64
static struct { char ps[128]; double size; FT_Face face; } g_cache[MAXCACHE];
static int g_cacheN;

static FT_Face faceFor(const char* ps, double size)
{
    int i;
    for (i = 0; i < g_cacheN; ++i)
        if (!strcmp(g_cache[i].ps, ps) && g_cache[i].size == size) return g_cache[i].face;
    for (i = 0; i < g_faceCount; ++i) {
        if (strcmp(g_faces[i].psName, ps)) continue;
        { FT_Face face;
          if (FT_New_Face(g_ft, g_faces[i].path, g_faces[i].index, &face)) return NULL;
          FT_Set_Char_Size(face, 0, (FT_F26Dot6)(size * 64.0 + 0.5), 72, 72);
          snprintf(g_cache[g_cacheN].ps, 128, "%s", ps);
          g_cache[g_cacheN].size = size;
          g_cache[g_cacheN].face = face;
          return g_cache[g_cacheN++].face; }
    }
    return NULL;
}

/* ---- our own flattener --------------------------------------------------
 * FreeType's smooth rasteriser flattens conics and cubics with its own fixed
 * criterion; if Quartz's flattener is finer, curved glyphs pick up a systematic
 * coverage deficit at the extrema that straight-edged glyphs never show. Flatten
 * the outline ourselves at a chosen tolerance (in device pixels) and hand the
 * rasteriser a polygon, so the tolerance becomes a knob we can sweep. */
#define MAXPTS 200000
static FT_Vector g_pts[MAXPTS];
static char g_tags[MAXPTS];
static short g_ends[1024];
static int g_np, g_nc;
static double g_tol = 0.01;

static void emit(double x, double y)
{
    if (g_np >= MAXPTS) return;
    g_pts[g_np].x = (FT_Pos)llround(x * 64.0);
    g_pts[g_np].y = (FT_Pos)llround(y * 64.0);
    g_tags[g_np++] = FT_CURVE_TAG_ON;
}

static double g_cx, g_cy;   /* current point, in pixels */

static int fMove(const FT_Vector* to, void* u)
{
    (void)u;
    if (g_np) g_ends[g_nc++] = (short)(g_np - 1);
    g_cx = to->x / 64.0; g_cy = to->y / 64.0;
    emit(g_cx, g_cy);
    return 0;
}
static int fLine(const FT_Vector* to, void* u)
{
    (void)u;
    g_cx = to->x / 64.0; g_cy = to->y / 64.0;
    emit(g_cx, g_cy);
    return 0;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, int d)
{
    double mx = (x0 + 2 * x1 + x2) / 4.0, my = (y0 + 2 * y1 + y2) / 4.0;
    double dx = mx - (x0 + x2) / 2.0, dy = my - (y0 + y2) / 2.0;
    if (d > 16 || dx * dx + dy * dy < g_tol * g_tol) { emit(x2, y2); return; }
    { double ax = (x0 + x1) / 2, ay = (y0 + y1) / 2, bx = (x1 + x2) / 2, by = (y1 + y2) / 2;
      double cx = (ax + bx) / 2, cy = (ay + by) / 2;
      quad(x0, y0, ax, ay, cx, cy, d + 1);
      quad(cx, cy, bx, by, x2, y2, d + 1); }
}
static void cube(double x0, double y0, double x1, double y1, double x2, double y2,
                 double x3, double y3, double* dummy, int d)
{
    double d1x = x1 - (2 * x0 + x3) / 3, d1y = y1 - (2 * y0 + y3) / 3;
    double d2x = x2 - (x0 + 2 * x3) / 3, d2y = y2 - (y0 + 2 * y3) / 3;
    double e = d1x * d1x + d1y * d1y; double e2 = d2x * d2x + d2y * d2y;
    (void)dummy;
    if (e2 > e) e = e2;
    if (d > 16 || e < g_tol * g_tol) { emit(x3, y3); return; }
    { double ax = (x0+x1)/2, ay = (y0+y1)/2, bx = (x1+x2)/2, by = (y1+y2)/2, cx = (x2+x3)/2, cy = (y2+y3)/2;
      double dx1 = (ax+bx)/2, dy1 = (ay+by)/2, ex = (bx+cx)/2, ey = (by+cy)/2;
      double mx = (dx1+ex)/2, my = (dy1+ey)/2;
      cube(x0,y0,ax,ay,dx1,dy1,mx,my,NULL,d+1);
      cube(mx,my,ex,ey,cx,cy,x3,y3,NULL,d+1); }
}
static int fConic(const FT_Vector* c, const FT_Vector* to, void* u)
{
    (void)u;
    quad(g_cx, g_cy, c->x / 64.0, c->y / 64.0, to->x / 64.0, to->y / 64.0, 0);
    g_cx = to->x / 64.0; g_cy = to->y / 64.0;
    return 0;
}
static int fCubic(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u)
{
    (void)u;
    cube(g_cx, g_cy, c1->x / 64.0, c1->y / 64.0, c2->x / 64.0, c2->y / 64.0,
         to->x / 64.0, to->y / 64.0, NULL, 0);
    g_cx = to->x / 64.0; g_cy = to->y / 64.0;
    return 0;
}
static const FT_Outline_Funcs kFuncs = { fMove, fLine, fConic, fCubic, 0, 0 };

/* render one glyph with its pen at device (penX, penYup) in a patch-sized
 * bitmap whose y axis runs up from the bottom (CG's convention). */
static double g_yscale = 1.0;
static int renderModel(FT_Face face, int glyph, double penX, double penYup,
                       unsigned char* out, int flags)
{
    FT_Outline* o;
    FT_Bitmap bm;
    if (FT_Load_Glyph(face, glyph, flags | FT_LOAD_NO_BITMAP)) return 0;
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return 0;
    o = &face->glyph->outline;
    if (g_yscale != 1.0) {
        FT_Matrix m;
        m.xx = 0x10000; m.xy = 0; m.yx = 0;
        m.yy = (FT_Fixed)(g_yscale * 65536.0 + 0.5);
        FT_Outline_Transform(o, &m);
    }
    FT_Outline_Translate(o, (FT_Pos)lround(penX * 64.0), (FT_Pos)lround(penYup * 64.0));
    if (g_tol > 0) {
        FT_Outline flat;
        g_np = g_nc = 0;
        FT_Outline_Decompose(o, &kFuncs, NULL);
        if (g_np) g_ends[g_nc++] = (short)(g_np - 1);
        flat.n_points = (short)g_np; flat.n_contours = (short)g_nc;
        flat.points = g_pts; flat.tags = g_tags; flat.contours = g_ends;
        flat.flags = FT_OUTLINE_NONE;
        memset(out, 0, g_patch * g_patch);
        { FT_Bitmap b; b.rows = g_patch; b.width = g_patch; b.pitch = g_patch; b.buffer = out;
          b.num_grays = 256; b.pixel_mode = FT_PIXEL_MODE_GRAY; b.palette_mode = 0; b.palette = NULL;
          FT_Outline_Get_Bitmap(g_ft, &flat, &b); }
        return 1;
    }
    memset(out, 0, g_patch * g_patch);
    bm.rows = g_patch; bm.width = g_patch; bm.pitch = g_patch;
    bm.buffer = out; bm.num_grays = 256; bm.pixel_mode = FT_PIXEL_MODE_GRAY; bm.palette_mode = 0; bm.palette = NULL;
    FT_Outline_Get_Bitmap(g_ft, o, &bm);
    return 1;
}

static double compare(const unsigned char* a, const unsigned char* b, long* nInk, long* nExact)
{
    int i, n = g_patch * g_patch; double e = 0; long k = 0, x = 0;
    for (i = 0; i < n; ++i) {
        if (a[i] || b[i]) { e += fabs((double)a[i] - b[i]); ++k; if (a[i] == b[i]) ++x; }
    }
    if (nInk) *nInk += k;
    if (nExact) *nExact += x;
    return e;
}

/* ---- page mode: the whole spike/fasttext sample, model vs CoreText --------
 * Same inputs as fast64 (ref.glyphs + ref-smooth.bin from ctref32) but with no
 * cairo in the way, so the glyph positions can be put exactly on Quartz's grid
 * instead of being re-quantised to cairo's 1/4 px. */
#define PMAXRUNS 64
#define PMAXGLYPHS 256
static struct { int line; char ps[128]; double size; int n;
                struct { int gid; double x, y; } g[PMAXGLYPHS]; } g_pr[PMAXRUNS];
static int g_prN;

static int loadRuns(const char* path)
{
    FILE* f = fopen(path, "r");
    char line[16384];
    if (!f) return 0;
    while (fgets(line, sizeof(line), f) && g_prN < PMAXRUNS) {
        char* save = NULL; char* tok = strtok_r(line, "\t\n", &save); int k = 0;
        if (!tok) continue;
        g_pr[g_prN].line = atoi(tok);
        tok = strtok_r(NULL, "\t\n", &save);
        snprintf(g_pr[g_prN].ps, 128, "%s", tok ? tok : "");
        tok = strtok_r(NULL, "\t\n", &save);
        g_pr[g_prN].size = tok ? atof(tok) : 0;
        tok = strtok_r(NULL, "\t\n", &save);
        while ((tok = strtok_r(NULL, "\t\n", &save)) && k < PMAXGLYPHS) {
            unsigned gid; double x, y;
            if (sscanf(tok, "%u,%lf,%lf", &gid, &x, &y) != 3) break;
            g_pr[g_prN].g[k].gid = gid; g_pr[g_prN].g[k].x = x; g_pr[g_prN].g[k].y = y; ++k;
        }
        g_pr[g_prN].n = k;
        if (k) ++g_prN;
    }
    fclose(f);
    return g_prN;
}

static int pageMode(const char* dir, const char* refName, int flags, int xrule, int yrule)
{
    char path[512];
    FILE* f;
    int W = 0, H = 0, i, k, x, y;
    unsigned char* ref; unsigned char* img;
    double err = 0, errInk = 0, inkV = 0, inkR = 0; long nInk = 0;
    char magic[4]; int32_t d[2];

    snprintf(path, sizeof(path), "%s/%s", dir, refName);
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no %s\n", path); return 1; }
    if (fread(magic, 1, 4, f) != 4) return 1;
    if (fread(d, 4, 2, f) != 2) return 1;
    W = d[0]; H = d[1];
    ref = (unsigned char*)malloc((size_t)W * H * 4);
    if (fread(ref, 4, (size_t)W * H, f) != (size_t)W * H) return 1;
    fclose(f);
    snprintf(path, sizeof(path), "%s/ref.glyphs", dir);
    if (!loadRuns(path)) { fprintf(stderr, "no %s\n", path); return 1; }

    img = (unsigned char*)calloc((size_t)W * H, 1);      /* coverage, 0 = white */
    for (i = 0; i < g_prN; ++i) {
        FT_Face face = faceFor(g_pr[i].ps, g_pr[i].size);
        int nx = phasesX(g_pr[i].size), ny = phasesY(g_pr[i].size);
        if (!face) { fprintf(stderr, "no face %s\n", g_pr[i].ps); continue; }
        for (k = 0; k < g_pr[i].n; ++k) {
            /* ref.glyphs is top-down; CG's own space is bottom-up, and that is
             * where the snapping happens. */
            double px = g_pr[i].g[k].x, pyUp = H - g_pr[i].g[k].y;
            double qx = xrule ? floor(px * nx) / nx : px;
            double qy = yrule ? floor(pyUp * ny) / ny : pyUp;
            FT_Outline* o;
            FT_BBox cb; int gx0, gy0, bw, bh, r, c;
            static unsigned char tile[256 * 256];
            if (FT_Load_Glyph(face, g_pr[i].g[k].gid, flags | FT_LOAD_NO_BITMAP)) continue;
            if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) continue;
            o = &face->glyph->outline;
            FT_Outline_Get_CBox(o, &cb);
            gx0 = (int)floor(qx + cb.xMin / 64.0) - 1;
            gy0 = (int)floor(qy + cb.yMin / 64.0) - 1;
            bw = (int)ceil(qx + cb.xMax / 64.0) + 1 - gx0;
            bh = (int)ceil(qy + cb.yMax / 64.0) + 1 - gy0;
            if (bw <= 0 || bh <= 0 || bw > 256 || bh > 256) continue;
            FT_Outline_Translate(o, (FT_Pos)lround((qx - gx0) * 64.0), (FT_Pos)lround((qy - gy0) * 64.0));
            memset(tile, 0, (size_t)bw * bh);
            { FT_Bitmap b; b.rows = bh; b.width = bw; b.pitch = bw; b.buffer = tile;
              b.num_grays = 256; b.pixel_mode = FT_PIXEL_MODE_GRAY; b.palette_mode = 0; b.palette = NULL;
              FT_Outline_Get_Bitmap(g_ft, o, &b); }
            for (r = 0; r < bh; ++r) {
                int dy2 = H - 1 - (gy0 + (bh - 1 - r));   /* tile row 0 = top of tile */
                if (dy2 < 0 || dy2 >= H) continue;
                for (c = 0; c < bw; ++c) {
                    int dx2 = gx0 + c, v;
                    if (dx2 < 0 || dx2 >= W) continue;
                    v = img[(size_t)dy2 * W + dx2] + tile[(size_t)r * bw + c];
                    img[(size_t)dy2 * W + dx2] = (unsigned char)(v > 255 ? 255 : v);
                }
            }
        }
    }
    for (y = 0; y < H; ++y) for (x = 0; x < W; ++x) {
        double a = 255.0 - img[(size_t)y * W + x];
        double b = ref[((size_t)y * W + x) * 4];
        err += fabs(a - b);
        if (a < 253 || b < 253) { errInk += fabs(a - b); ++nInk; }
        inkV += 255 - a; inkR += 255 - b;
    }
    printf("page: luma %.3f  inkluma %.3f  ink %.4f  (xrule %d yrule %d hintflags 0x%x)\n",
        err / (W * (double)H), nInk ? errInk / nInk : 0, inkR > 0 ? inkV / inkR : 0, xrule, yrule, flags);
    { char op[512]; FILE* g2;
      snprintf(op, sizeof(op), "%s/model-page.pgm", dir);
      g2 = fopen(op, "wb");
      if (g2) { fprintf(g2, "P5\n%d %d\n255\n", W, H);
                for (y = 0; y < H; ++y) for (x = 0; x < W; ++x)
                    { unsigned char v = 255 - img[(size_t)y * W + x]; fwrite(&v, 1, 1, g2); }
                fclose(g2); } }
    free(ref); free(img);
    return 0;
}

int main(int argc, char** argv)
{
    const char* manifest = argc > 1 ? argv[1] : "tiger-fonts.json";
    const char* patches = argc > 2 ? argv[2] : "glyphs.bin";
    int sweep = argc > 3 && !strcmp(argv[3], "sweep");
    int dump = argc > 3 && !strcmp(argv[3], "dump");
    int edge = argc > 3 && !strcmp(argv[3], "edge");
    int yfit = argc > 3 && !strcmp(argv[3], "yfit");
    int hintMode = argc > 4 ? atoi(argv[4]) : 0;
    if (argc > 5) g_tol = atof(argv[5]);   /* flattening tolerance in px; <=0 = FreeType's own */
    int flags = FT_LOAD_NO_HINTING;
    int i;
    unsigned char* buf;
    const int PEN = 12, BASE = 36;      /* must match ctglyph32 */
    double totErr = 0; long totInk = 0, totExact = 0;
    char curKey[160] = ""; double keyErr = 0; long keyInk = 0, keyExact = 0, keyIdent = 0, keyN = 0;

    if (hintMode == 3 || hintMode == 4) {
        /* native TrueType bytecode: v35 is the classic full interpreter, v40 the
         * y-only "subpixel" one FreeType defaults to. */
        FT_UInt v = hintMode == 3 ? 35 : 40;
        FT_Property_Set(g_ft, "truetype", "interpreter-version", &v);
        flags = FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_AUTOHINT;
    }
    else if (hintMode == 1) flags = FT_LOAD_TARGET_LIGHT;                       /* slight */
    else if (hintMode == 2) flags = FT_LOAD_TARGET_NORMAL;                  /* full */

    if (FT_Init_FreeType(&g_ft)) { fprintf(stderr, "no freetype\n"); return 1; }
    if (argc > 2 && !strcmp(argv[2], "page")) {
        /* model64 <manifest> page <dir> <refname> [xrule] [yrule] */
        if (!loadManifest(manifest)) { fprintf(stderr, "no manifest\n"); return 1; }
        return pageMode(argc > 3 ? argv[3] : ".", argc > 4 ? argv[4] : "ref-smooth.bin",
            FT_LOAD_NO_HINTING, argc > 5 ? atoi(argv[5]) : 1, argc > 6 ? atoi(argv[6]) : 1);
    }
    {   /* FreeType's Adobe CFF engine darkens stems by a ppem-dependent amount;
         * Quartz does no such thing, and the amount changes with fractional ppem,
         * which shows up as a size-dependent error on CFF faces. */
        FT_Bool no = 1;
        FT_Property_Set(g_ft, "cff", "no-stem-darkening", &no);
        FT_Property_Set(g_ft, "autofitter", "no-stem-darkening", &no);
        FT_Property_Set(g_ft, "type1", "no-stem-darkening", &no);
    }
    if (!loadManifest(manifest)) { fprintf(stderr, "no manifest %s\n", manifest); return 1; }
    if (!loadPatches(patches)) { fprintf(stderr, "no patches %s\n", patches); return 1; }
    buf = (unsigned char*)malloc(g_patch * g_patch);
    printf("# %d patches of %dx%d, %d faces, hint=%d\n", g_n, g_patch, g_patch, g_faceCount, hintMode);
    printf("# face/size/glyph        mean|d| over inked px   exact px%%   identical patches\n");

    for (i = 0; i <= g_n; ++i) {
        char key[160];
        if (i < g_n) snprintf(key, sizeof(key), "%s %g #%d", g_p[i].ps, g_p[i].size, g_p[i].glyph);
        else key[0] = 0;
        if (i == g_n || strcmp(key, curKey)) {
            if (curKey[0] && keyInk)
                printf("%-34s %7.3f   %6.2f%%   %ld/%ld\n", curKey, keyErr / keyInk,
                    100.0 * keyExact / keyInk, keyIdent, keyN);
            snprintf(curKey, sizeof(curKey), "%s", key);
            keyErr = 0; keyInk = keyExact = keyIdent = keyN = 0;
        }
        if (i == g_n) break;
        {
            FT_Face face = faceFor(g_p[i].ps, g_p[i].size);
            double px = g_p[i].size;                 /* CTM is identity in ctglyph32 */
            int nx = phasesX(px), ny = phasesY(px);
            double penX = PEN + g_p[i].dx;
            double penY = g_patch - BASE - g_p[i].dy;     /* CG bottom-up */
            double qx = floor(penX * nx) / nx;
            double qy = floor(penY * ny) / ny;
            long ink = 0, ex = 0; double err;
            if (!face) { fprintf(stderr, "no face %s\n", g_p[i].ps); continue; }
            if (yfit) {
                double bs = 1, be2 = 1e18, sc;
                for (sc = 0.94; sc <= 1.0601; sc += 0.0005) {
                    long ji = 0, je = 0; double e;
                    g_yscale = sc;
                    renderModel(face, g_p[i].glyph, qx, qy, buf, flags);
                    e = compare(buf, g_p[i].cov, &ji, &je);
                    if (ji) e /= ji;
                    if (e < be2) { be2 = e; bs = sc; }
                }
                g_yscale = 1.0;
                { long ji = 0, je = 0; double e0;
                  renderModel(face, g_p[i].glyph, qx, qy, buf, flags);
                  e0 = compare(buf, g_p[i].cov, &ji, &je); if (ji) e0 /= ji;
                  printf("%-26s err %6.2f -> %6.2f at yscale %.4f\n", curKey, e0, be2, bs); }
                continue;
            }
            if (edge) {
                /* sub-pixel top and bottom edge of the tallest fully-covered column,
                 * measured the same way in both images: a column whose interior rows
                 * are saturated, so the partial row at each end is pure y coverage. */
                int r, c, bestc = -1; int bestrun = 0;
                double te[2], be[2];
                const unsigned char* img[2];
                int im;
                renderModel(face, g_p[i].glyph, qx, qy, buf, flags);
                img[0] = g_p[i].cov; img[1] = buf;
                for (c = 0; c < g_patch; ++c) {
                    int run = 0;
                    for (r = 0; r < g_patch; ++r) if (img[0][r*g_patch+c] >= 250 && img[1][r*g_patch+c] >= 250) ++run;
                    if (run > bestrun) { bestrun = run; bestc = c; }
                }
                if (bestc < 0) { printf("%-30s no saturated column\n", curKey); continue; }
                for (im = 0; im < 2; ++im) {
                    int first = -1, last = -1;
                    for (r = 0; r < g_patch; ++r) if (img[im][r*g_patch+bestc] >= 250) { if (first < 0) first = r; last = r; }
                    te[im] = first - (first > 0 ? img[im][(first-1)*g_patch+bestc] / 255.0 : 0);
                    be[im] = last + 1 + (last + 1 < g_patch ? img[im][(last+1)*g_patch+bestc] / 255.0 : 0);
                }
                printf("%-26s col %2d  top: Q %7.3f  M %7.3f  dT %+6.3f   bottom: Q %7.3f  M %7.3f  dB %+6.3f  height Q %6.3f M %6.3f\n",
                    curKey, bestc, te[0], te[1], te[0]-te[1], be[0], be[1], be[0]-be[1], be[0]-te[0], be[1]-te[1]);
                continue;
            }
            if (dump) {
                int r, c;
                if (g_p[i].dx != 0 || g_p[i].dy != 0) continue;
                renderModel(face, g_p[i].glyph, qx, qy, buf, flags);
                printf("\n=== %s  dx=%.3f dy=%.3f  qx=%.4f qy=%.4f  nx=%d\n", curKey, g_p[i].dx, g_p[i].dy, qx, qy, nx);
                for (r = 0; r < g_patch; ++r) {
                    int any = 0;
                    for (c = 0; c < g_patch; ++c) if (buf[r*g_patch+c] || g_p[i].cov[r*g_patch+c]) any = 1;
                    if (!any) continue;
                    printf("q%02d ", r);
                    for (c = 0; c < g_patch; ++c) { int v = g_p[i].cov[r*g_patch+c]; if (v) printf("%3d", (v*99+127)/255); else printf("  ."); }
                    printf("\nm%02d ", r);
                    for (c = 0; c < g_patch; ++c) { int v = buf[r*g_patch+c]; if (v) printf("%3d", (v*99+127)/255); else printf("  ."); }
                    printf("\nd%02d ", r);
                    for (c = 0; c < g_patch; ++c) { int d = (int)buf[r*g_patch+c] - g_p[i].cov[r*g_patch+c]; if (d) printf("%3d", (d*99)/255); else printf("  ."); }
                    printf("\n");
                }
                continue;
            }
            if (sweep) {
                double best = 1e18; int bk = 0, bj = 0, k, j;
                for (k = -12; k <= 12; ++k) for (j = -12; j <= 12; ++j) {
                    long junkI = 0, junkE = 0; double e;
                    renderModel(face, g_p[i].glyph, qx + k / 48.0, qy + j / 48.0, buf, flags);
                    e = compare(buf, g_p[i].cov, &junkI, &junkE);
                    if (junkI) e /= junkI;
                    if (e < best) { best = e; bk = k; bj = j; }
                }
                printf("sweep %-28s dx=%.4f dy=%.4f  best model offset x%+d/48 y%+d/48, err %.3f\n",
                    curKey, g_p[i].dx, g_p[i].dy, bk, bj, best);
                continue;
            }
            renderModel(face, g_p[i].glyph, qx, qy, buf, flags);
            err = compare(buf, g_p[i].cov, &ink, &ex);
            totErr += err; totInk += ink; totExact += ex;
            keyErr += err; keyInk += ink; keyExact += ex; ++keyN;
            if (!memcmp(buf, g_p[i].cov, g_patch * g_patch)) ++keyIdent;
        }
    }
    if (!sweep && totInk)
        printf("\nTOTAL  mean|d| over inked px = %.4f (%.2f%% of 255)   exact pixels %.2f%%\n",
            totErr / totInk, 100.0 * (totErr / totInk) / 255.0, 100.0 * totExact / totInk);
    return 0;
}
