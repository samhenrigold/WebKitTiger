/* TIGER: resolve a font by file handle, the way a split-process port needs to.
 *
 * The 64-bit web process picks fonts and shapes with HarfBuzz over font *files*;
 * the 32-bit render process has to rasterise the same face through Tiger's
 * CoreText. A PostScript name is not a safe handle across that boundary,
 * because two files can carry the same name and activation order decides who
 * wins. A (path, face index) pair is, and it is what HarfBuzz and fontconfig
 * already use.
 *
 * spike/fontmanifest.c writes the manifest the web process builds its font
 * database from, so both sides name the same faces by the same handles.
 * Measured agreement between the two engines is in logs/hb-vs-ct.md.
 */

#ifndef TIGERCOMPAT_CTFONTHANDLE_H
#define TIGERCOMPAT_CTFONTHANDLE_H

/* This header deliberately includes no CoreText header of its own, because the
 * two that exist contradict each other on purpose: TigerCompat/CTCompat.h
 * declares Tiger's real ABI for the compat layer, the SDK overlay declares the
 * modern one for WebCore. Pulling either in here would pick a side for every
 * consumer. Include whichever you already use before this file; the types below
 * are only declared if neither has. */

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreServices/CoreServices.h>   /* OSStatus */

#if !defined(__CTFONT__) && !defined(TIGERCOMPAT_CTCOMPAT_H)
#error "include <CoreText/CoreText.h> or <TigerCompat/CTCompat.h> before this header"
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* Oblique is applied here, as a shear in the font matrix. Synthetic bold is
     * not: it is a draw-time stroke and belongs to whoever owns the context,
     * which is what WebCore does anyway. The flag is accepted and reported back
     * so a caller can see its request was understood. */
    kTigerCTFontSyntheticOblique = 1 << 0,
    kTigerCTFontSyntheticBold    = 1 << 1
};

/* Resolve a face from a file. faceIndex selects within a .ttc or .dfont
 * suitcase and is 0 for a plain sfnt. Returns noErr and a retained font in
 * *out, or an OSStatus and *out set to NULL. */
OSStatus TigerCTFontForHandle(const char* path, int faceIndex, CGFloat size,
    unsigned flags, CTFontRef* out);

/* The same for a face that has no file: a web font arriving as bytes. */
OSStatus TigerCTFontForData(CFDataRef data, int faceIndex, CGFloat size,
    unsigned flags, CTFontRef* out);

/* Drop the cached fonts. Activated files stay activated, because ATS has no
 * safe way to deactivate a container another font may still reference. */
void TigerCTFontHandleFlushCache(void);

/* Diagnostics for the test suites: how many entries the caches hold. */
void TigerCTFontHandleCacheStats(unsigned* activations, unsigned* fonts);

#ifdef __cplusplus
}
#endif

#endif /* TIGERCOMPAT_CTFONTHANDLE_H */
