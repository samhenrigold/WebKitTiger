/* TigerCompat/AquaControls.h -- draw WebCore's native controls with real Aqua.
 *
 * WebCore already describes a native control as a serialisable ControlPart plus
 * a ControlStyle and hands the pair to whichever process can draw it
 * (WebCore/platform/graphics/controls/, drawn on Mac by
 * platform/graphics/mac/controls/*.mm). On this port the 64-bit content process
 * has no AppKit, so the 32-bit render process draws them here with the system's
 * own NSCell and HITheme code.
 *
 * This is a C API on purpose: the caller is C++ that must not import AppKit.
 * The cell configuration follows the *Mac.mm files closely -- the cell types,
 * bezel styles, control-size-from-font rule and the cellSize/cellOutsets tables
 * are ported from them rather than invented, so the artwork lands where WebCore
 * expects it.
 *
 * Implemented in compat/aquacontrols.m. Not thread safe: the cells are shared,
 * exactly as ControlFactoryMac's are, and AppKit drawing belongs on the main
 * thread anyway.
 */

#ifndef TIGERCOMPAT_AQUACONTROLS_H
#define TIGERCOMPAT_AQUACONTROLS_H

#include <ApplicationServices/ApplicationServices.h>
#include <TigerCompat/AquaControlKinds.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned states;        /* the bits above */
    float fontSize;         /* CSS px; picks the control size class */
    float zoomFactor;       /* 1 for no zoom */
    CGRect rect;            /* border box, in the context's coordinates */

    /* Per-control extras. Unused fields are ignored. */
    double value;           /* slider position, progress and meter: 0..1 */
    double animationPhase;  /* indeterminate progress: 0..1, wraps */
    double meterLow;        /* meter: 0..1 */
    double meterHigh;       /* meter: 0..1 */
    double meterOptimum;    /* meter: 0..1 */
    int smallScrollbar;     /* scrollbars: non-zero for the small metrics */
} TigerControlStyle;

/* Fills style with the defaults a caller should start from: enabled, in an
 * active window, 12px font, no zoom, the given rect. */
void TigerControlStyleInit(TigerControlStyle *style, CGRect rect);

/* Draws into the context's current coordinate system. The context is left as it
 * was found. */
void TigerDrawControl(CGContextRef context, TigerControlKind kind, const TigerControlStyle *style);

/* Render a control into a fresh premultiplied-BGRA buffer (row 0 is the top row,
 * CAIRO_FORMAT_ARGB32's byte order on a little-endian machine), for a caller with no
 * AppKit: the x86_64 web process asks the 32-bit UI process for this and blits it,
 * which is the one artefact that works in both of this port's rendering modes.
 * style->rect should be at the origin; the buffer covers TigerControlDrawingBounds and
 * *outOriginX/Y report its top-left relative to style->rect.origin. free() the result. */
void *TigerRenderControlBitmap(TigerControlKind kind, const TigerControlStyle *style,
                               int *outWidth, int *outHeight,
                               int *outOriginX, int *outOriginY);

/* The focus ring WebCore draws for a control it painted itself, such as a text
 * field. Separate because the ring lives outside the control's border box and
 * the caller owns that geometry. */
void TigerDrawFocusRing(CGContextRef context, CGRect rect, float cornerRadius);

/* What the content process needs but cannot ask AppKit for: the system colours
 * RenderThemeMac reads and the system font sizes per control size class.
 * Writes a small JSON document; returns non-zero on success. */
int TigerWriteControlMetricsJSON(const char *path);

/* The size class this style's font maps to, and the control's preferred size
 * for it, so the content process can lay out before anything is drawn.
 * sizeClass is 0 regular, 1 small, 2 mini. */
int TigerControlSizeClassForStyle(const TigerControlStyle *style);
CGSize TigerControlPreferredSize(TigerControlKind kind, const TigerControlStyle *style);

/* The rect TigerDrawControl will actually paint into for this style. It is
 * usually larger than style->rect, because a cell's bezel and shadow live
 * outside the border box; WebCore calls the same thing rectForBounds. Callers
 * need it for damage rects, and a test needs it to line a drawn control up
 * against a live one. */
CGRect TigerControlDrawingBounds(TigerControlKind kind, const TigerControlStyle *style);

#ifdef __cplusplus
}
#endif

#endif /* TIGERCOMPAT_AQUACONTROLS_H */
