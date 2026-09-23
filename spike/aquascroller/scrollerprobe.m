/* Does TigerRenderAquaScrollbar (the UI process's scrollbar artwork for the web process)
 * draw exactly what a live NSScroller draws, and where does HITheme put the parts?
 *
 * A real window with real NSScrollers, vertical and horizontal, regular and small, at several
 * knob positions and proportions, active and inactive. Each live scroller's knob rect is read
 * with -rectForPart:, handed to TigerRenderAquaScrollbar as a pixel position and length, and
 * the two images are compared over the same captured backdrop. Also prints HITheme's part
 * bounds, which is where ScrollbarThemeTiger's metrics come from.
 *
 * Build and run: spike/aquascroller/run.sh
 */

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <TigerCompat/AquaControlKinds.h>
#include "TigerScrollbarArtwork.h"
#include <stdio.h>

static NSWindow *gWindow, *gOther;
static NSView *gContent;
static NSString *gOut;
static int gIdentical, gWrong;

static void pump(double s) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; }

static NSBitmapImageRep *capture(NSRect r)
{
    [gContent lockFocus];
    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc] initWithFocusedViewRect:r] autorelease];
    [gContent unlockFocus];
    return rep;
}

static void writePNG(NSBitmapImageRep *rep, NSString *name)
{
    [[rep representationUsingType:NSPNGFileType properties:nil] writeToFile:[gOut stringByAppendingPathComponent:name] atomically:YES];
}

/* rectForPart: in top-down scroller coordinates. */
static NSRect topDown(NSScroller *s, NSRect r)
{
    if ([s isFlipped])
        return r;
    return NSMakeRect(r.origin.x, NSHeight([s bounds]) - NSMaxY(r), r.size.width, r.size.height);
}

static void printParts(const char *name, int w, int h)
{
    HIThemeTrackDrawInfo info;
    memset(&info, 0, sizeof(info));
    info.kind = (w > h ? h : w) < 15 ? kThemeScrollBarSmall : kThemeScrollBarMedium;
    info.bounds = CGRectMake(0, 0, w, h);
    info.attributes = kThemeTrackShowThumb | (w > h ? kThemeTrackHorizontal : 0);
    info.enableState = kThemeTrackActive;
    info.min = 0; info.max = 1000; info.value = 0; info.trackInfo.scrollbar.viewsize = 1;
    int start, len;
    TigerAquaScrollbarTravel(w, h, &start, &len);
    printf("%s %dx%d travel start %d length %d\n", name, w, h, start, len);
    ControlPartCode parts[16]; UInt32 n = 0;
    HIThemeGetTrackParts(&info, &n, 16, parts);
    for (UInt32 i = 0; i < n; ++i) {
        HIRect r;
        HIThemeGetTrackPartBounds(&info, parts[i], &r);
        printf("   part %3d  %g,%g %gx%g\n", parts[i], r.origin.x, r.origin.y, r.size.width, r.size.height);
    }
    HIRect tb; HIThemeGetTrackBounds(&info, &tb);
    HIRect dr; HIThemeGetTrackDragRect(&info, &dr);
    printf("   trackBounds %g,%g %gx%g dragRect %g,%g %gx%g\n", tb.origin.x, tb.origin.y, tb.size.width, tb.size.height,
        dr.origin.x, dr.origin.y, dr.size.width, dr.size.height);
    /* The minimum thumb: viewsize 1 of 1000. */
    HIRect thumb; HIThemeGetTrackPartBounds(&info, kAppearancePartIndicator, &thumb);
    printf("   min thumb %gx%g\n", thumb.size.width, thumb.size.height);
    /* HITheme's hit test along the axis, to see where the arrows' hit areas really are. */
    ControlPartCode last = -1;
    int along = w > h ? w : h;
    for (int p = 0; p < along; ++p) {
        HIPoint pt = w > h ? CGPointMake(p + 0.5, h / 2.0) : CGPointMake(w / 2.0, p + 0.5);
        ControlPartCode hit = 0;
        HIThemeHitTestTrack(&info, &pt, &hit);
        if (hit != last) { printf("   hit from %d: part %d\n", p, hit); last = hit; }
    }
}

static void compareOne(const char *name, NSScroller *s, BOOL active)
{
    NSRect f = [s frame];
    NSRect knob = topDown(s, [s rectForPart:NSScrollerKnob]);
    NSRect slot = topDown(s, [s rectForPart:NSScrollerKnobSlot]);
    BOOL horizontal = NSWidth(f) > NSHeight(f);
    int start, travel;
    TigerAquaScrollbarTravel((int)NSWidth(f), (int)NSHeight(f), &start, &travel);
    int pos = (int)(horizontal ? knob.origin.x : knob.origin.y) - start;
    int len = (int)(horizontal ? knob.size.width : knob.size.height);
    printf("%-28s knob %g,%g %gx%g slot %g,%g %gx%g -> pos %d len %d (travel %d@%d)\n", name,
        knob.origin.x, knob.origin.y, knob.size.width, knob.size.height,
        slot.origin.x, slot.origin.y, slot.size.width, slot.size.height, pos, len, travel, start);

    [s setHidden:YES]; [gContent display];
    NSBitmapImageRep *backdrop = capture(f);
    [s setHidden:NO]; [gContent display]; pump(0.05);
    NSBitmapImageRep *live = capture(f);

    int w = (int)NSWidth(f), h = (int)NSHeight(f);
    unsigned states = TigerControlStateEnabled | (active ? TigerControlStateWindowActive : 0);
    unsigned char *art = TigerRenderAquaScrollbar(w, h, states, pos, len);
    NSBitmapImageRep *ours = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
        bytesPerRow:w * 4 bitsPerPixel:32] autorelease];
    /* backdrop, then our premultiplied BGRA over it */
    unsigned char *o = [ours bitmapData], *b = [backdrop bitmapData];
    int bsp = [backdrop samplesPerPixel], brow = [backdrop bytesPerRow];
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        unsigned char *src = art + (y * w + x) * 4; /* B G R A */
        unsigned char *bg = b + y * brow + x * bsp;
        unsigned a = src[3];
        unsigned char *dst = o + (y * w + x) * 4;
        dst[0] = src[2] + bg[0] * (255 - a) / 255;
        dst[1] = src[1] + bg[1] * (255 - a) / 255;
        dst[2] = src[0] + bg[2] * (255 - a) / 255;
        dst[3] = 255;
    }
    free(art);

    unsigned char *l = [live bitmapData];
    int lsp = [live samplesPerPixel], lrow = [live bytesPerRow];
    int differing = 0, maxDelta = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
        int d = abs(l[y * lrow + x * lsp + c] - o[(y * w + x) * 4 + c]);
        if (d > maxDelta) maxDelta = d;
        if (d > 0 && c == 0) ++differing;
    }
    printf("   %s: differing %d/%d maxDelta %d\n", differing ? "DIFFERENT" : "IDENTICAL", differing, w * h, maxDelta);
    if (differing) ++gWrong; else ++gIdentical;
    writePNG(live, [NSString stringWithFormat:@"%s-live.png", name]);
    writePNG(ours, [NSString stringWithFormat:@"%s-ours.png", name]);
}

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);
    gOut = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"out";
    [[NSFileManager defaultManager] createDirectoryAtPath:gOut attributes:nil];

    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);
    [NSApplication sharedApplication];

    gWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 560, 420) styleMask:NSTitledWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    gOther = [[NSWindow alloc] initWithContentRect:NSMakeRect(700, 100, 120, 80) styleMask:NSTitledWindowMask
        backing:NSBackingStoreBuffered defer:NO];
    gContent = [gWindow contentView];

    printParts("vertical-regular", 15, 300);
    printParts("vertical-small", 11, 300);
    printParts("horizontal-regular", 300, 15);
    printParts("vertical-regular-short", 15, 60);
    printParts("horizontal-small", 300, 11);

    struct { const char *name; NSRect frame; NSControlSize size; float value, proportion; } cases[] = {
        { "v15-top",     { { 20, 40 }, { 15, 300 } }, NSRegularControlSize, 0.0, 0.2 },
        { "v15-mid",     { { 60, 40 }, { 15, 300 } }, NSRegularControlSize, 0.5, 0.3 },
        { "v15-end",     { { 100, 40 }, { 15, 300 } }, NSRegularControlSize, 1.0, 0.5 },
        { "v15-tiny",    { { 140, 40 }, { 15, 300 } }, NSRegularControlSize, 0.37, 0.01 },
        { "v11-mid",     { { 180, 40 }, { 11, 300 } }, NSSmallControlSize, 0.6, 0.25 },
        { "h15-mid",     { { 220, 20 }, { 300, 15 } }, NSRegularControlSize, 0.4, 0.35 },
        { "h11-mid",     { { 220, 60 }, { 300, 11 } }, NSSmallControlSize, 0.8, 0.2 },
    };
    unsigned count = sizeof(cases) / sizeof(cases[0]);
    NSScroller *scrollers[16];
    for (unsigned i = 0; i < count; ++i) {
        NSScroller *s = [[NSScroller alloc] initWithFrame:cases[i].frame];
        [s setControlSize:cases[i].size];
        [s setEnabled:YES];
        [s setFloatValue:cases[i].value knobProportion:cases[i].proportion];
        [gContent addSubview:s];
        scrollers[i] = s;
    }
    /* A real NSScrollView with a long document, for the eye: the reference the page's
     * scrollbars get compared with. */
    NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(360, 120, 180, 260)];
    [sv setHasVerticalScroller:YES];
    [sv setHasHorizontalScroller:YES];
    NSView *doc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 1200)];
    [sv setDocumentView:doc];
    [gContent addSubview:sv];

    [gWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    pump(0.5);
    for (int attempt = 0; attempt < 5 && !([gWindow isKeyWindow] && [NSApp isActive]); ++attempt) {
        [NSApp activateIgnoringOtherApps:YES];
        [gWindow makeKeyAndOrderFront:nil];
        pump(0.4);
    }
    printf("key=%d active=%d scroller flipped=%d width regular %g small %g\n", (int)[gWindow isKeyWindow], (int)[NSApp isActive],
        (int)[scrollers[0] isFlipped], [NSScroller scrollerWidthForControlSize:NSRegularControlSize],
        [NSScroller scrollerWidthForControlSize:NSSmallControlSize]);
    if (!([gWindow isKeyWindow] && [NSApp isActive]))
        return 2;

    [gContent display]; pump(0.1);
    writePNG(capture([gContent bounds]), @"window-active.png");
    for (unsigned i = 0; i < count; ++i)
        compareOne(cases[i].name, scrollers[i], YES);

    [gOther makeKeyAndOrderFront:nil];
    pump(0.3);
    for (unsigned i = 0; i < count; ++i)
        compareOne([[NSString stringWithFormat:@"%s-inactive", cases[i].name] UTF8String], scrollers[i], NO);

    printf("identical %d, different %d\n", gIdentical, gWrong);
    [pool release];
    return 0;
}
