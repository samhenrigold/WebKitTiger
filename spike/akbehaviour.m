/* akbehaviour.m -- AppKit on Tiger versus modern, measured side by side.
 *
 * Same dual-build differential method as spike/{cgbehaviour.c,fndbehaviour.m}:
 * one source built for 10.4.11 and for this Mac, printing identical KEY=value
 * lines, diffed by spike/run-akbehaviour.sh.
 *
 * Runs under a live NSApplication with a real window on both machines. That
 * works over ssh on the Tiger box as long as someone is logged in at the
 * console, which was checked before any of this was written.
 *
 * Scope is what WebCore/mac and WebKitLegacy/mac call by name. Divergences are
 * classified in logs/appkit-probe.md as Tiger behaviour, nscompat shim bug, or
 * expected absence.
 *
 * Caveat stated up front: appearance-derived values are NOT expected to match.
 * A 2005 Aqua colour and a 2025 one differ because twenty years of design
 * happened, not because anything is broken, so the colour section prints values
 * for comparison rather than asserting equality. What must match is structure:
 * scale factors, geometry, ordering, round trips.
 */
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
/* The overlay's CoreText headers carry Tiger's real signatures, notably
   CTFontCreateWithName taking a double size rather than a CGFloat. */
#import <CoreText/CoreText.h>
#include <stdio.h>
#include <string.h>

#if TIGER
#import <TigerCompat/NSCompat.h>
#import <TigerCompat/AppKitCompat.h>
#endif

/* ------------------------------------------------------------------ output */

static void kvs(const char *k, const char *v) { printf("%s=%s\n", k, v); }
static void kvi(const char *k, long long v) { printf("%s=%lld\n", k, v); }
/* Rounded to 1/1000 so a double's last bits do not manufacture differences. */
static void kvf(const char *k, double v) { printf("%s=%.3f\n", k, v); }
static void kvstr(const char *k, NSString *s)
{
    if (!s) { printf("%s=nil\n", k); return; }
    const char *u = [s UTF8String];
    printf("%s=len%lu:", k, (unsigned long)[s length]);
    for (const unsigned char *p = (const unsigned char *)(u ? u : ""); *p; ++p) printf("%02x", *p);
    printf("\n");
}
static void kvrect(const char *k, NSRect r)
{
    printf("%s=%.2f,%.2f,%.2f,%.2f\n", k, (double)r.origin.x, (double)r.origin.y,
           (double)r.size.width, (double)r.size.height);
}
static void kvpoint(const char *k, NSPoint p)
{
    printf("%s=%.2f,%.2f\n", k, (double)p.x, (double)p.y);
}

/* ------------------------------------------------------------- NSEvent ---- */

static void probeEvent(void)
{
    /* A left-mouse-down, not a scroll wheel: modern rejects NSScrollWheel in
       +mouseEventWithType: outright, and the only portable way to synthesise a
       real scroll event is CGEventCreateScrollWheelEvent plus +eventWithCGEvent:,
       which is 10.5. The nscompat accessors are a category on NSEvent, so they
       answer for any event, and the deltas being zero here is itself comparable. */
    NSEvent *e = [NSEvent mouseEventWithType:NSLeftMouseDown
                                    location:NSMakePoint(10, 20)
                               modifierFlags:(NSShiftKeyMask | NSCommandKeyMask)
                                   timestamp:0
                                windowNumber:0
                                     context:nil
                                 eventNumber:0
                                  clickCount:1
                                    pressure:1.0f];
    kvs("evt.created", e ? "nonnil" : "nil");
    if (!e) return;
    kvi("evt.type", (long long)[e type]);
    kvpoint("evt.location", [e locationInWindow]);
    kvi("evt.mod.shift", ([e modifierFlags] & NSShiftKeyMask) ? 1 : 0);
    kvi("evt.mod.command", ([e modifierFlags] & NSCommandKeyMask) ? 1 : 0);
    kvi("evt.mod.control", ([e modifierFlags] & NSControlKeyMask) ? 1 : 0);
    kvi("evt.mod.alt", ([e modifierFlags] & NSAlternateKeyMask) ? 1 : 0);

    /* The 10.7-10.10 accessors nscompat adds. Modern RAISES for several of these
       when the event type does not carry them -- -phase on a mouse event throws
       NSInternalInconsistencyException -- while the shim answers unconditionally.
       Catch per accessor so the difference is recorded rather than fatal. */
#define TRY_I(key, expr) do { @try { kvi(key, (long long)(expr)); } \
                              @catch (NSException *x) { kvs(key, "raises"); } } while (0)
#define TRY_F(key, expr) do { @try { kvf(key, (double)(expr)); } \
                              @catch (NSException *x) { kvs(key, "raises"); } } while (0)
    TRY_I("evt.phase", [e phase]);
    TRY_I("evt.momentumPhase", [e momentumPhase]);
    TRY_I("evt.hasPrecise", [e hasPreciseScrollingDeltas] ? 1 : 0);
    TRY_F("evt.scrollingDeltaX", [e scrollingDeltaX]);
    TRY_F("evt.scrollingDeltaY", [e scrollingDeltaY]);
    kvf("evt.deltaX", (double)[e deltaX]);
    kvf("evt.deltaY", (double)[e deltaY]);
    TRY_I("evt.invertedFromDevice", [e isDirectionInvertedFromDevice] ? 1 : 0);
    @try { kvs("evt.CGEvent", [e CGEvent] ? "nonnil" : "nil"); }
    @catch (NSException *x) { kvs("evt.CGEvent", "raises"); }
    /* -stage is 10.10.3 and nscompat provides it; skip where it does not exist. */
    if ([e respondsToSelector:@selector(stage)]) TRY_I("evt.stage", [e stage]);
    else kvs("evt.stage", "absent");
#undef TRY_I
#undef TRY_F
}

/* ------------------------------------------------- NSView / NSWindow ------ */

@interface ProbeView : NSView { @public int drawCount; NSRect lastDirty; }
@end
@implementation ProbeView
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)r { ++drawCount; lastDirty = r; }
@end

static NSWindow *gWindow;
static ProbeView *gFlipped;
static NSView *gPlain;

static void probeViewGeometry(void)
{
    gWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 400, 300)
                                          styleMask:NSTitledWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    kvs("win.created", gWindow ? "nonnil" : "nil");
    if (!gWindow) return;
    /* The window has to be on screen or -displayIfNeeded does nothing and
       -occlusionState is meaningless. orderFront: rather than makeKeyAndOrderFront:
       so the probe does not steal focus from whoever is at the machine. */
    [gWindow orderFront:nil];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
    NSView *content = [gWindow contentView];
    kvrect("win.contentFrame", [content frame]);
    kvi("win.content.isFlipped", [content isFlipped] ? 1 : 0);

    gPlain = [[NSView alloc] initWithFrame:NSMakeRect(10, 20, 100, 50)];
    gFlipped = [[ProbeView alloc] initWithFrame:NSMakeRect(10, 20, 100, 50)];
    [content addSubview:gPlain];
    [content addSubview:gFlipped];
    kvi("view.plain.isFlipped", [gPlain isFlipped] ? 1 : 0);
    kvi("view.probe.isFlipped", [gFlipped isFlipped] ? 1 : 0);

    /* Backing scale must be exactly 1 on a non-Retina machine, and the backing
       conversions must therefore be the identity. */
    kvf("view.backingScale", (double)[gPlain backingScaleFactor]);
    kvf("win.backingScale", (double)[gWindow backingScaleFactor]);
    /* NSScreen's backing scale is NOT shimmed by nscompat, and WebKit calls it at
       WebView.mm:6895 and PlatformScreenMac.mm:201. Report rather than crash. */
    if ([[NSScreen mainScreen] respondsToSelector:@selector(backingScaleFactor)])
        kvf("screen.backingScale", (double)[[NSScreen mainScreen] backingScaleFactor]);
    else
        kvs("screen.backingScale", "absent");
    kvrect("view.rectToBacking", [gPlain convertRectToBacking:NSMakeRect(1, 2, 3, 4)]);
    kvrect("view.rectFromBacking", [gPlain convertRectFromBacking:NSMakeRect(1, 2, 3, 4)]);
    kvpoint("view.pointToBacking", [gPlain convertPointToBacking:NSMakePoint(5, 6)]);
    kvpoint("view.pointFromBacking", [gPlain convertPointFromBacking:NSMakePoint(5, 6)]);

    /* Flipped versus unflipped coordinate conversion, which WebHTMLView lives on. */
    kvpoint("conv.plain.fromContent", [gPlain convertPoint:NSMakePoint(0, 0) fromView:content]);
    kvpoint("conv.probe.fromContent", [gFlipped convertPoint:NSMakePoint(0, 0) fromView:content]);
    kvpoint("conv.plain.toContent", [gPlain convertPoint:NSMakePoint(0, 0) toView:content]);
    kvpoint("conv.probe.toContent", [gFlipped convertPoint:NSMakePoint(0, 0) toView:content]);
    kvrect("conv.plain.rectToContent", [gPlain convertRect:NSMakeRect(0, 0, 10, 10) toView:content]);
    kvrect("conv.probe.rectToContent", [gFlipped convertRect:NSMakeRect(0, 0, 10, 10) toView:content]);

    /* frame versus bounds after a bounds scale, which is how zoom is expressed. */
    kvrect("scale.before.frame", [gPlain frame]);
    kvrect("scale.before.bounds", [gPlain bounds]);
    [gPlain setBoundsSize:NSMakeSize(50, 25)];
    kvrect("scale.after.frame", [gPlain frame]);
    kvrect("scale.after.bounds", [gPlain bounds]);
    kvpoint("scale.conv.toContent", [gPlain convertPoint:NSMakePoint(10, 10) toView:content]);
    [gPlain setBounds:[gPlain frame]];

    kvi("win.occlusion.visible",
        ([gWindow occlusionState] & NSWindowOcclusionStateVisible) ? 1 : 0);
}

static void probeDisplayCoalescing(void)
{
    if (!gFlipped) return;
    gFlipped->drawCount = 0;
    [gFlipped setNeedsDisplayInRect:NSMakeRect(0, 0, 10, 10)];
    [gFlipped setNeedsDisplayInRect:NSMakeRect(20, 20, 10, 10)];
    /* Two dirty rects before one display pass: AppKit coalesces into the union,
       so the draw count and the rect it hands back are the interesting values. */
    [gFlipped displayIfNeeded];
    kvi("draw.coalesced.count", gFlipped->drawCount);
    kvrect("draw.coalesced.dirty", gFlipped->lastDirty);
    gFlipped->drawCount = 0;
    [gFlipped setNeedsDisplay:YES];
    [gFlipped displayIfNeeded];
    kvi("draw.full.count", gFlipped->drawCount);
    kvrect("draw.full.dirty", gFlipped->lastDirty);
}

/* --------------------------------------------------------- tracking ------- */

static void probeTrackingRects(void)
{
    if (!gFlipped) return;
    NSTrackingRectTag tag = [gFlipped addTrackingRect:NSMakeRect(0, 0, 50, 50)
                                                owner:gFlipped
                                             userData:NULL
                                         assumeInside:NO];
    kvi("track.tag.nonzero", tag != 0 ? 1 : 0);
    NSTrackingRectTag tag2 = [gFlipped addTrackingRect:NSMakeRect(50, 50, 10, 10)
                                                 owner:gFlipped
                                              userData:NULL
                                          assumeInside:NO];
    kvi("track.tags.distinct", (tag != tag2) ? 1 : 0);
    [gFlipped removeTrackingRect:tag];
    [gFlipped removeTrackingRect:tag2];
    kvs("track.removed", "ok");
}

/* -------------------------------------------------------- NSPasteboard ---- */

static void probePasteboard(void)
{
    /* A private pasteboard, so the machine's clipboard is left alone. */
    /* Measured limitation, not a Tiger behaviour: over ssh the pasteboard server
       is not in the session's bootstrap namespace, so EVERY pasteboard including
       +generalPasteboard comes back nil on the box. The window server IS reachable
       (windows and screencapture both work), so this is specific to pbs. Checked
       with a standalone program and via osascript before concluding it. */
    NSPasteboard *pb = [NSPasteboard pasteboardWithName:@"AkProbePasteboard"];
    kvs("pb.created", pb ? "nonnil" : "nil");
    kvs("pb.general", [NSPasteboard generalPasteboard] ? "nonnil" : "nil");
    if (!pb) { kvs("pb.unmeasurable", "no pasteboard server in this session"); return; }
    NSArray *types = [NSArray arrayWithObject:NSStringPboardType];
    int changeCount = (int)[pb declareTypes:types owner:nil];
    kvi("pb.declare.positive", changeCount > 0 ? 1 : 0);
    kvi("pb.setString", [pb setString:@"probe value" forType:NSStringPboardType] ? 1 : 0);
    kvstr("pb.stringForType", [pb stringForType:NSStringPboardType]);
    kvi("pb.types.count", (long long)[[pb types] count]);
    kvi("pb.available",
        [pb availableTypeFromArray:types] != nil ? 1 : 0);
    kvstr("pb.missingType", [pb stringForType:@"com.example.absent"]);
    /* A second declare must bump the change count. */
    int c2 = (int)[pb declareTypes:types owner:nil];
    kvi("pb.declare.bumps", c2 > changeCount ? 1 : 0);
    [pb releaseGlobally];
}

/* ------------------------------------------------- cursor, menu ----------- */

static void probeCursorAndMenu(void)
{
    kvs("cursor.arrow", [NSCursor arrowCursor] ? "nonnil" : "nil");
    kvs("cursor.ibeam", [NSCursor IBeamCursor] ? "nonnil" : "nil");
    kvs("cursor.pointing", [NSCursor pointingHandCursor] ? "nonnil" : "nil");
    kvs("cursor.crosshair", [NSCursor crosshairCursor] ? "nonnil" : "nil");
    NSCursor *before = [NSCursor currentCursor];
    [[NSCursor IBeamCursor] push];
    [NSCursor pop];
    kvi("cursor.pushpop.restored", [NSCursor currentCursor] == before ? 1 : 0);
    kvpoint("cursor.hotspot.ibeam", [[NSCursor IBeamCursor] hotSpot]);

    NSMenu *m = [[[NSMenu alloc] initWithTitle:@"probe"] autorelease];
    [m addItemWithTitle:@"One" action:@selector(terminate:) keyEquivalent:@"1"];
    [m addItemWithTitle:@"Two" action:NULL keyEquivalent:@""];
    [m addItem:[NSMenuItem separatorItem]];
    kvi("menu.count", (long long)[m numberOfItems]);
    kvstr("menu.item0.title", [[m itemAtIndex:0] title]);
    kvstr("menu.item0.keyEquiv", [[m itemAtIndex:0] keyEquivalent]);
    kvi("menu.item2.isSeparator", [[m itemAtIndex:2] isSeparatorItem] ? 1 : 0);
    kvi("menu.indexOfTitle", (long long)[m indexOfItemWithTitle:@"Two"]);
    kvstr("menu.title", [m title]);
}

/* -------------------------------------------------------- NSFont ---------- */

static void probeFont(void)
{
    NSFont *f = [NSFont fontWithName:@"Helvetica" size:16.0];
    kvs("font.helvetica", f ? "nonnil" : "nil");
    if (!f) return;
    kvstr("font.fontName", [f fontName]);
    kvstr("font.familyName", [f familyName]);
    kvf("font.pointSize", (double)[f pointSize]);
    kvf("font.ascender", (double)[f ascender]);
    kvf("font.descender", (double)[f descender]);
    kvf("font.xHeight", (double)[f xHeight]);
    kvf("font.capHeight", (double)[f capHeight]);
    kvf("font.leading", (double)[f leading]);
    kvi("font.numberOfGlyphs.positive", [f numberOfGlyphs] > 0 ? 1 : 0);
    kvf("font.boundingRect.h", (double)[f boundingRectForFont].size.height);

    /* The same metrics through CoreText, which is what WebCore actually uses.
       The two must agree or text lays out differently depending on the path. */
    CTFontRef ct = CTFontCreateWithName((CFStringRef)@"Helvetica", 16.0, NULL);
    kvs("ctfont.created", ct ? "nonnil" : "nil");
    if (ct) {
        kvf("ctfont.ascent", (double)CTFontGetAscent(ct));
        kvf("ctfont.descent", (double)CTFontGetDescent(ct));
        kvf("ctfont.xHeight", (double)CTFontGetXHeight(ct));
        kvf("ctfont.capHeight", (double)CTFontGetCapHeight(ct));
        kvf("ctfont.leading", (double)CTFontGetLeading(ct));
        /* NSFont's descender is negative, CT's descent is positive. */
        kvf("font.vs.ct.ascender", (double)([f ascender] - CTFontGetAscent(ct)));
        kvf("font.vs.ct.descender", (double)([f descender] + CTFontGetDescent(ct)));
        kvf("font.vs.ct.xHeight", (double)([f xHeight] - CTFontGetXHeight(ct)));
        kvf("font.vs.ct.capHeight", (double)([f capHeight] - CTFontGetCapHeight(ct)));
        CFRelease(ct);
    }
    kvs("font.missing", [NSFont fontWithName:@"NoSuchFaceXYZ" size:12.0] ? "nonnil" : "nil");
}

/* --------------------------------------------- NSAttributedString --------- */

static void probeAttributedString(void)
{
    NSFont *f = [NSFont fontWithName:@"Helvetica" size:14.0];
    NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                           f, NSFontAttributeName,
                           [NSColor redColor], NSForegroundColorAttributeName, nil];
    NSAttributedString *as = [[[NSAttributedString alloc] initWithString:@"Hello World"
                                                             attributes:attrs] autorelease];
    kvi("as.length", (long long)[as length]);
    kvstr("as.string", [as string]);
    NSAttributedString *sub = [as attributedSubstringFromRange:NSMakeRange(6, 5)];
    kvstr("as.sub.string", [sub string]);
    kvi("as.sub.length", (long long)[sub length]);

    NSRange eff;
    NSDictionary *got = [as attributesAtIndex:0 effectiveRange:&eff];
    kvi("as.attrs.count", (long long)[got count]);
    kvi("as.attrs.range.len", (long long)eff.length);
    kvi("as.attrs.hasFont", [got objectForKey:NSFontAttributeName] ? 1 : 0);
    kvi("as.attrs.hasColor", [got objectForKey:NSForegroundColorAttributeName] ? 1 : 0);
    kvstr("as.attrs.fontName", [[got objectForKey:NSFontAttributeName] fontName]);

    /* Mixed attributes: the effective range must stop at the boundary. */
    NSMutableAttributedString *ms = [[[NSMutableAttributedString alloc]
                                      initWithString:@"abcdef"] autorelease];
    [ms setAttributes:attrs range:NSMakeRange(0, 3)];
    NSRange eff2;
    [ms attributesAtIndex:0 effectiveRange:&eff2];
    kvi("as.mixed.range.len", (long long)eff2.length);
    [ms attributesAtIndex:4 effectiveRange:&eff2];
    kvi("as.mixed.tail.len", (long long)eff2.length);

    /* RTF round trip, which is the pasteboard path. */
    NSData *rtf = [as RTFFromRange:NSMakeRange(0, [as length]) documentAttributes:nil];
    kvi("as.rtf.nonempty", [rtf length] > 0 ? 1 : 0);
    NSAttributedString *back = [[[NSAttributedString alloc] initWithRTF:rtf
                                                    documentAttributes:NULL] autorelease];
    kvstr("as.rtf.roundtrip.string", [back string]);
    kvi("as.rtf.roundtrip.hasFont",
        (back && [[back attributesAtIndex:0 effectiveRange:NULL] objectForKey:NSFontAttributeName]) ? 1 : 0);
}

/* ------------------------------------------------- NSImage, NSBezierPath -- */

static CGImageRef makeCGImage(void)
{
    static unsigned char px[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i) {
        px[i * 4 + 0] = 255; px[i * 4 + 1] = 0; px[i * 4 + 2] = 0; px[i * 4 + 3] = 255;
    }
    CGDataProviderRef p = CGDataProviderCreateWithData(NULL, px, sizeof px, NULL);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(16, 16, 8, 32, 64, rgb, kCGImageAlphaPremultipliedLast,
                                   p, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(rgb);
    CGDataProviderRelease(p);
    return img;
}

static void probeImageAndPath(void)
{
    CGImageRef cg = makeCGImage();
    kvs("img.cgsource", cg ? "nonnil" : "nil");
    NSImage *img = nil;
    if (cg) {
        /* -[NSBitmapImageRep initWithCGImage:] is 10.5 and is not shimmed. The
           Tiger route is to draw the CGImage into a rep's own context, which is
           what a shim would do, so measure that too rather than just the gap. */
        NSBitmapImageRep *rep = nil;
        if ([NSBitmapImageRep instancesRespondToSelector:@selector(initWithCGImage:)]) {
            rep = [[[NSBitmapImageRep alloc] initWithCGImage:cg] autorelease];
            kvs("img.repFromCG", rep ? "nonnil" : "nil");
        } else {
            kvs("img.repFromCG", "absent");
        }
        if (!rep) {
            rep = [[[NSBitmapImageRep alloc]
                    initWithBitmapDataPlanes:NULL pixelsWide:16 pixelsHigh:16
                    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                    colorSpaceName:NSCalibratedRGBColorSpace
                    bytesPerRow:64 bitsPerPixel:32] autorelease];
            if (rep) {
                NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
                kvs("img.repDrawFallback.ctx", gc ? "nonnil" : "nil");
                if (gc) {
                    [NSGraphicsContext saveGraphicsState];
                    [NSGraphicsContext setCurrentContext:gc];
                    CGContextDrawImage((CGContextRef)[gc graphicsPort],
                                       CGRectMake(0, 0, 16, 16), cg);
                    [NSGraphicsContext restoreGraphicsState];
                }
            }
            kvs("img.repDrawFallback", rep ? "nonnil" : "nil");
        }
        if (rep) {
            img = [[[NSImage alloc] initWithSize:NSMakeSize(16, 16)] autorelease];
            [img addRepresentation:rep];
            kvi("img.rep.width", (long long)[rep pixelsWide]);
            kvi("img.rep.height", (long long)[rep pixelsHigh]);
            kvi("img.rep.hasAlpha", [rep hasAlpha] ? 1 : 0);
        }
    }
    kvs("img.created", img ? "nonnil" : "nil");
    if (img) {
        NSSize sz = [img size];
        kvf("img.size.w", (double)sz.width);
        kvf("img.size.h", (double)sz.height);
        NSData *tiff = [img TIFFRepresentation];
        kvi("img.tiff.nonempty", [tiff length] > 0 ? 1 : 0);
        NSImage *back = [[[NSImage alloc] initWithData:tiff] autorelease];
        kvs("img.tiff.roundtrip", back ? "nonnil" : "nil");
        kvf("img.tiff.roundtrip.w", back ? (double)[back size].width : -1);
        kvi("img.reps.count", (long long)[[img representations] count]);
    }
    if (cg) CGImageRelease(cg);

    NSBezierPath *bp = [NSBezierPath bezierPath];
    [bp moveToPoint:NSMakePoint(0, 0)];
    [bp lineToPoint:NSMakePoint(10, 0)];
    [bp lineToPoint:NSMakePoint(10, 10)];
    [bp closePath];
    kvi("path.elementCount", (long long)[bp elementCount]);
    kvrect("path.bounds", [bp bounds]);
    kvi("path.isEmpty", [bp isEmpty] ? 1 : 0);
    kvi("path.containsPoint.in", [bp containsPoint:NSMakePoint(8, 2)] ? 1 : 0);
    kvi("path.containsPoint.out", [bp containsPoint:NSMakePoint(1, 8)] ? 1 : 0);
    NSBezierPath *rect = [NSBezierPath bezierPathWithRect:NSMakeRect(1, 2, 3, 4)];
    kvrect("path.rect.bounds", [rect bounds]);
    kvi("path.rect.elements", (long long)[rect elementCount]);
    NSBezierPath *oval = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(0, 0, 10, 10)];
    kvi("path.oval.elements", (long long)[oval elementCount]);
    kvf("path.default.lineWidth", (double)[bp lineWidth]);
    kvi("path.default.windingRule", (long long)[bp windingRule]);
}

/* ------------------------------------------------------- NSScrollView ----- */

static void probeScrollView(void)
{
    NSScrollView *sv = [[[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 200, 100)] autorelease];
    [sv setHasVerticalScroller:YES];
    [sv setHasHorizontalScroller:YES];
    ProbeView *doc = [[[ProbeView alloc] initWithFrame:NSMakeRect(0, 0, 400, 800)] autorelease];
    [sv setDocumentView:doc];
    kvs("sv.clip", [sv contentView] ? "nonnil" : "nil");
    kvi("sv.clip.isFlipped", [[sv contentView] isFlipped] ? 1 : 0);
    kvs("sv.doc", [sv documentView] ? "nonnil" : "nil");
    kvrect("sv.docFrame", [doc frame]);
    kvrect("sv.clipBounds.initial", [[sv contentView] bounds]);
    [[sv contentView] scrollToPoint:NSMakePoint(0, 100)];
    kvrect("sv.clipBounds.scrolled", [[sv contentView] bounds]);
    kvrect("sv.documentVisible", [sv documentVisibleRect]);
    kvi("sv.hasVert", [sv hasVerticalScroller] ? 1 : 0);
    kvf("sv.scrollerWidth", (double)[NSScroller scrollerWidth]);
}

/* ----------------------------------------------------------- NSColor ------ */

/* Appearance-derived values are not expected to match; these are printed for
   comparison, and the report says which are structural and which are cosmetic. */
static void colorComponents(const char *key, NSColor *c)
{
    char k[128];
    if (!c) { printf("%s=nil\n", key); return; }
    NSColor *rgb = [c colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
    if (!rgb) { printf("%s=nonrgb\n", key); return; }
    snprintf(k, sizeof k, "%s.r", key); kvf(k, (double)[rgb redComponent]);
    snprintf(k, sizeof k, "%s.g", key); kvf(k, (double)[rgb greenComponent]);
    snprintf(k, sizeof k, "%s.b", key); kvf(k, (double)[rgb blueComponent]);
    snprintf(k, sizeof k, "%s.a", key); kvf(k, (double)[rgb alphaComponent]);
}

static void probeColor(void)
{
    /* Structural: these are fixed by definition, not by appearance. */
    colorComponents("color.black", [NSColor blackColor]);
    colorComponents("color.white", [NSColor whiteColor]);
    colorComponents("color.red", [NSColor redColor]);
    colorComponents("color.clear", [NSColor clearColor]);
    colorComponents("color.srgb",
                    [NSColor colorWithSRGBRed:0.25f green:0.5f blue:0.75f alpha:1.0f]);

    /* CGColor conversion, which WebCore uses to cross into CoreGraphics. */
    NSColor *r = [[NSColor redColor] colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
    CGColorRef cg = [r CGColor];
    kvs("color.cgcolor", cg ? "nonnil" : "nil");
    if (cg) {
        kvi("color.cgcolor.ncomp", (long long)CGColorGetNumberOfComponents(cg));
        const CGFloat *comps = CGColorGetComponents(cg);
        if (comps) {
            kvf("color.cgcolor.c0", (double)comps[0]);
            kvf("color.cgcolor.c3", (double)comps[3]);
        }
    }
    NSColor *back = [NSColor colorWithCGColor:cg];
    colorComponents("color.fromCG", back);

    /* Appearance-derived: printed for the record, not expected to match. */
    colorComponents("color.appearance.selectedText", [NSColor selectedTextColor]);
    colorComponents("color.appearance.selectedTextBg", [NSColor selectedTextBackgroundColor]);
    colorComponents("color.appearance.secondaryLabel", [NSColor secondaryLabelColor]);
    colorComponents("color.appearance.findHighlight", [NSColor findHighlightColor]);
    NSArray *alt = [NSColor alternatingContentBackgroundColors];
    kvi("color.appearance.alternating.count", (long long)[alt count]);
}

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);
#if TIGER
    kvs("platform", "tiger");
#else
    kvs("platform", "host");
#endif
    NSApplication *app = [NSApplication sharedApplication];
    kvs("app.shared", app ? "nonnil" : "nil");
    kvi("app.screens", (long long)[[NSScreen screens] count]);

    probeEvent();
    probeViewGeometry();
    probeDisplayCoalescing();
    probeTrackingRects();
    probePasteboard();
    probeCursorAndMenu();
    probeFont();
    probeAttributedString();
    probeImageAndPath();
    probeScrollView();
    probeColor();

    [pool release];
    return 0;
}
