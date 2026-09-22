/* The one sample both sides render. Kept here so the CoreText reference and the
 * cairo/FreeType variants can never drift apart. */
#ifndef FASTTEXT_SAMPLE_H
#define FASTTEXT_SAMPLE_H

#define FT_W 720
#define FT_H 264
#define FT_X0 12.0          /* left margin, in points */
#ifndef FT_SCALE
#define FT_SCALE 1.0        /* ctref32zoom: a CTM scale, applied to size, pen and baseline alike */
#endif
#ifndef FT_LINE0
#define FT_LINE0 17.25      /* first baseline, from the top -- deliberately NOT an integer */
#endif
#ifndef FT_LEADING
#define FT_LEADING 30.375   /* fractional parts walk .25 .625 .0 .375 .75 .125 .5 .875 */
#endif

typedef struct { const char* psName; double size; const char* utf8; } SampleLine;

static const SampleLine kSample[] = {
    { "LucidaGrande",      13.0, "Lucida Grande 13: File Edit View History Bookmarks Window Help" },
    { "LucidaGrande-Bold", 13.0, "Lucida Grande Bold 13: Save Changes Before Closing?" },
    { "LucidaGrande",      11.0, "Lucida Grande 11: 1,204 items, 38.7 GB available" },
    { "Helvetica",         16.0, "Helvetica 16: The quick brown fox jumps over the lazy dog. 0123456789" },
    { "Helvetica-Bold",    16.0, "Helvetica Bold 16: Waltz, bad nymph, for quick jigs vex." },
    { "Times-Roman",       16.0, "Times 16: Sphinx of black quartz, judge my vow. AVATAR To. We." },
    { "Times-Italic",      16.0, "Times Italic 16: fi fl ffi \xe2\x80\x94 hamburgefonstiv" },
    { "HiraKakuPro-W3",    16.0, "\xe3\x83\x92\xe3\x83\xa9\xe3\x82\xae\xe3\x83\x8e\xe8\xa7\x92\xe3\x82\xb4 16: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xae\xe3\x83\x86\xe3\x82\xad\xe3\x82\xb9\xe3\x83\x88\xe8\xa1\xa8\xe7\xa4\xba" },
};
#define FT_LINES ((int)(sizeof(kSample) / sizeof(kSample[0])))

/* ref.bin: "FTX1", w, h, then w*h RGBA pixels, rows top-down. */
#define FT_MAGIC "FTX1"

#endif
