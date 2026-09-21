/* Acceptance test: does TigerDrawControl produce the same pixels as a live
 * AppKit control in a real window?
 *
 * Real NSButton / NSPopUpButton / NSTextField / NSSlider / NSProgressIndicator /
 * NSScroller instances are put in a real window, driven into each state, and
 * their pixels read back. TigerDrawControl is then asked for the same kind,
 * size class and state, and the two are compared channel by channel.
 *
 * The live pixels come from the window's backing store via
 * -initWithFocusedViewRect:, not from screencapture. That is deliberate: it is
 * the same rendering the screen shows, minus the window shadow, the menu bar
 * and any chance of the capture landing a pixel off. A crop from a screenshot
 * would introduce differences that are not the controls'.
 *
 * Each comparison covers the control's frame plus a margin, so a control drawn
 * in the right style but the wrong place fails rather than passing.
 */

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <TigerCompat/AquaControls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARGIN 8

/* NSRectToCGRect is 10.5. */
static CGRect NSRectToCGRectCompat(NSRect r)
{
    return CGRectMake(r.origin.x, r.origin.y, r.size.width, r.size.height);
}

static NSString *gCompareDir;
static NSWindow *gWindow;
static NSWindow *gOtherWindow;
static NSView *gContent;

typedef enum {
    StNormal, StPressed, StDisabled, StFocused, StInactive, StMixed, StCount
} TestState;

static const char *stateNames[StCount] = {
    "normal", "pressed", "disabled", "focused", "inactive", "mixed"
};

/* ------------------------------------------------------------- utilities */

static void writePNGFromRep(NSBitmapImageRep *rep, NSString *path)
{
    NSData *png = [rep representationUsingType:NSPNGFileType properties:nil];
    [png writeToFile:path atomically:YES];
}

static NSBitmapImageRep *captureLive(NSRect frame)
{
    NSRect r = NSInsetRect(frame, -MARGIN, -MARGIN);
    [gContent lockFocus];
    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc] initWithFocusedViewRect:r] autorelease];
    [gContent unlockFocus];
    return rep;
}

/* Renders TigerDrawControl into a bitmap the same size as the captured region,
 * over the same window background, so the two are comparable. */
static NSBitmapImageRep *renderOurs(TigerControlKind kind, const TigerControlStyle *base,
                                    NSRect frame, NSBitmapImageRep *backdrop)
{
    unsigned w = (unsigned)(frame.size.width + MARGIN * 2);
    unsigned h = (unsigned)(frame.size.height + MARGIN * 2);

    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h bitsPerSample:8
                 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 4
                    bitsPerPixel:32] autorelease];

    NSGraphicsContext *saved = [NSGraphicsContext currentContext];
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext setCurrentContext:gc];
    /* The real window background, captured with the control hidden, rather than
     * a flat fill of +windowBackgroundColor. That colour is a pattern, so a flat
     * fill differs from the real backdrop everywhere the control is transparent
     * and the comparison would be measuring the backdrop, not the control. */
    if (backdrop)
        [backdrop drawInRect:NSMakeRect(0, 0, w, h)];
    else {
        [[NSColor windowBackgroundColor] set];
        NSRectFill(NSMakeRect(0, 0, w, h));
    }
    [gc flushGraphics];
    [NSGraphicsContext setCurrentContext:saved];

    /* base->rect is the border box in window coordinates; shift it into the
     * captured region's coordinates. The live control has already been sized to
     * this style's painted bounds, so the two land on the same pixels. */
    TigerControlStyle style = *base;
    style.rect = CGRectMake(base->rect.origin.x - frame.origin.x + MARGIN,
                            base->rect.origin.y - frame.origin.y + MARGIN,
                            base->rect.size.width, base->rect.size.height);
    TigerDrawControl((CGContextRef)[gc graphicsPort], kind, &style);
    [gc flushGraphics];
    return rep;
}

/* Compares and reports. Returns 0 identical, 1 near, 2 wrong. */
static int compareReps(const char *kindName, const char *stateName,
                       NSBitmapImageRep *live, NSBitmapImageRep *ours)
{
    if (!live || !ours) {
        printf("  %-18s %-9s  NO CAPTURE\n", kindName, stateName);
        return 2;
    }
    int w = [live pixelsWide], h = [live pixelsHigh];
    if (w != (int)[ours pixelsWide] || h != (int)[ours pixelsHigh]) {
        printf("  %-18s %-9s  SIZE MISMATCH live %dx%d ours %dx%d\n",
               kindName, stateName, w, h, (int)[ours pixelsWide], (int)[ours pixelsHigh]);
        return 2;
    }

    unsigned char *a = [live bitmapData], *b = [ours bitmapData];
    int rowA = [live bytesPerRow], rowB = [ours bytesPerRow];
    int spA = [live samplesPerPixel], spB = [ours samplesPerPixel];
    long total = (long)w * h;

    /* Compare at a range of offsets, not just at zero. Artwork that is right
     * but placed a pixel out differs almost everywhere, which would read as
     * "completely wrong" and send someone looking at the drawing code when the
     * answer is the geometry. The best offset and its residual separate the two
     * questions. */
    long bestDiffering = -1;
    int bestMaxDelta = 255, bestDX = 0, bestDY = 0;
    long zeroDiffering = 0;
    int zeroMaxDelta = 0;

    for (int dy = -MARGIN; dy <= MARGIN; ++dy) {
        for (int dx = -MARGIN; dx <= MARGIN; ++dx) {
            long differing = 0;
            int maxDelta = 0;
            for (int y = 0; y < h; ++y) {
                int sy = y + dy;
                if (sy < 0 || sy >= h) continue;
                for (int x = 0; x < w; ++x) {
                    int sx = x + dx;
                    if (sx < 0 || sx >= w) continue;
                    unsigned char *pa = a + y * rowA + x * spA;
                    unsigned char *pb = b + sy * rowB + sx * spB;
                    int worst = 0;
                    for (int c = 0; c < 3; ++c) {
                        int d = (int)pa[c] - (int)pb[c];
                        if (d < 0) d = -d;
                        if (d > worst) worst = d;
                    }
                    if (worst) {
                        ++differing;
                        if (worst > maxDelta) maxDelta = worst;
                    }
                }
            }
            if (!dx && !dy) { zeroDiffering = differing; zeroMaxDelta = maxDelta; }
            if (bestDiffering < 0 || differing < bestDiffering) {
                bestDiffering = differing;
                bestMaxDelta = maxDelta;
                bestDX = dx;
                bestDY = dy;
            }
        }
    }

    long differing = zeroDiffering;
    int maxDelta = zeroMaxDelta;

    int verdict;
    const char *label;
    if (!differing) { verdict = 0; label = "IDENTICAL"; }
    else if (maxDelta <= 8 && differing * 100 < total * 5) { verdict = 1; label = "near"; }
    else if (!bestDiffering && (bestDX || bestDY)) { verdict = 1; label = "OFFSET"; }
    else if (bestMaxDelta <= 8 && bestDiffering * 100 < total * 5) { verdict = 1; label = "off+near"; }
    else { verdict = 2; label = "WRONG"; }

    printf("  %-18s %-9s  %-9s  maxDelta %3d  differing %ld/%ld (%.1f%%)"
           "  best %+d%+d -> %ld (%.1f%%) maxDelta %d\n",
           kindName, stateName, label, maxDelta, differing, total,
           100.0 * differing / total,
           bestDX, bestDY, bestDiffering, 100.0 * bestDiffering / total, bestMaxDelta);

    if (verdict) {
        writePNGFromRep(live, [gCompareDir stringByAppendingPathComponent:
            [NSString stringWithFormat:@"%s-%s-live.png", kindName, stateName]]);
        writePNGFromRep(ours, [gCompareDir stringByAppendingPathComponent:
            [NSString stringWithFormat:@"%s-%s-ours.png", kindName, stateName]]);
    }
    return verdict;
}

/* ------------------------------------------------------------ the fixtures */

static void setKeyWindow(BOOL ours)
{
    if (ours)
        [gWindow makeKeyAndOrderFront:nil];
    else
        [gOtherWindow makeKeyAndOrderFront:nil];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
}

/* Applies a state to a live control and to a TigerControlStyle in step, so the
 * two can never drift apart in the harness itself. */
static BOOL applyState(NSView *control, TigerControlStyle *style, TestState state,
                       BOOL supportsMixed)
{
    /* NSProgressIndicator is an NSView, not an NSControl, so it has no cell and
     * reaches none of the cell-driven states. */
    NSCell *cell = [control respondsToSelector:@selector(cell)]
        ? [(NSControl *)control cell] : nil;
    [cell setEnabled:YES];
    [cell setHighlighted:NO];
    [gWindow makeFirstResponder:nil];
    style->states = TigerControlStateEnabled | TigerControlStateWindowActive;

    switch (state) {
    case StNormal:
        setKeyWindow(YES);
        break;
    case StPressed:
        if (!cell)
            return NO;
        setKeyWindow(YES);
        [cell setHighlighted:YES];
        style->states |= TigerControlStatePressed;
        break;
    case StDisabled:
        if (!cell)
            return NO;
        setKeyWindow(YES);
        [cell setEnabled:NO];
        style->states &= ~TigerControlStateEnabled;
        break;
    case StFocused:
        setKeyWindow(YES);
        if (![gWindow makeFirstResponder:control])
            return NO;   /* not focusable; skip rather than report a false failure */
        style->states |= TigerControlStateFocused;
        break;
    case StInactive:
        setKeyWindow(NO);
        style->states &= ~TigerControlStateWindowActive;
        break;
    case StMixed:
        if (!supportsMixed || !cell)
            return NO;
        setKeyWindow(YES);
        [cell setState:NSMixedState];
        style->states |= TigerControlStateIndeterminate;
        break;
    default:
        return NO;
    }
    return YES;
}

static int gIdentical, gNear, gWrong, gSkipped;

static void testControl(const char *name, NSView *control, TigerControlKind kind,
                        BOOL supportsMixed, float fontSize)
{
    /* The border box is what WebCore hands the API; a cell paints outside it.
     * So the live control is resized to those painted bounds, because an
     * NSControl draws its cell across its whole frame. Comparing a live control
     * sized to the border box against a cell drawn across the border box plus
     * its outsets would be comparing two different sizes of button. */
    NSRect borderBox = [control frame];

    for (int s = 0; s < StCount; ++s) {
        TigerControlStyle style;
        TigerControlStyleInit(&style, NSRectToCGRectCompat(borderBox));
        style.fontSize = fontSize;
        style.value = 0.6;

        CGRect painted = TigerControlDrawingBounds(kind, &style);
        NSRect liveFrame = NSMakeRect(painted.origin.x, painted.origin.y,
                                      painted.size.width, painted.size.height);
        [control setFrame:liveFrame];

        if (![control isDescendantOf:gContent]) {
            ++gSkipped;
            continue;
        }
        if (!applyState(control, &style, (TestState)s, supportsMixed)) {
            printf("  %-18s %-9s  skipped (state not reachable)\n", name, stateNames[s]);
            ++gSkipped;
            continue;
        }

        [gContent display];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];

        NSRect frame = liveFrame;

        /* Capture the backdrop with the control hidden, then the control. */
        [control setHidden:YES];
        [gContent display];
        NSBitmapImageRep *backdrop = captureLive(frame);
        [control setHidden:NO];
        [gContent display];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];

        NSBitmapImageRep *live = captureLive(frame);
        NSBitmapImageRep *ours = renderOurs(kind, &style, frame, backdrop);

        int verdict = compareReps(name, stateNames[s], live, ours);
        if (!verdict) ++gIdentical; else if (verdict == 1) ++gNear; else ++gWrong;

        if (supportsMixed && [control respondsToSelector:@selector(cell)])
            [[(NSControl *)control cell] setState:NSOffState];
    }
}

/* ------------------------------------------------------------------- main */

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);

    gCompareDir = (argc > 1) ? [NSString stringWithUTF8String:argv[1]] : @"compare";
    [[NSFileManager defaultManager] createDirectoryAtPath:gCompareDir attributes:nil];

    /* A tool launched over ssh is a background process. TransformProcessType
     * promotes it, and a promoted process still needs to be in a .app bundle
     * whose first launch has already registered it with LaunchServices. Without
     * all three, no window becomes key and every live control draws its
     * inactive artwork. */
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);

    [NSApplication sharedApplication];

    NSRect frame = NSMakeRect(100, 100, 420, 360);
    gWindow = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    gContent = [gWindow contentView];

    /* A second window so the first can be made genuinely not key, which is how
     * the window-inactive appearance is reached. */
    gOtherWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(600, 100, 120, 80)
                                               styleMask:NSTitledWindowMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];

    NSButton *push = [[NSButton alloc] initWithFrame:NSMakeRect(20, 300, 90, 20)];
    [push setBezelStyle:NSRoundedBezelStyle];
    [push setTitle:nil];
    [gContent addSubview:push];

    NSButton *checkbox = [[NSButton alloc] initWithFrame:NSMakeRect(20, 260, 14, 14)];
    [checkbox setButtonType:NSSwitchButton];
    [[checkbox cell] setAllowsMixedState:YES];
    /* nil, not @"": ControlFactoryMac uses a nil title, and a switch cell with
     * a title left-aligns its glyph while one without centres it. WebCore draws
     * the label itself, so nil is the configuration under test. */
    [checkbox setTitle:nil];
    [[checkbox cell] setFocusRingType:NSFocusRingTypeExterior];
    [gContent addSubview:checkbox];

    NSButton *radio = [[NSButton alloc] initWithFrame:NSMakeRect(60, 258, 16, 16)];
    [radio setButtonType:NSRadioButton];
    [radio setTitle:nil];
    [[radio cell] setFocusRingType:NSFocusRingTypeExterior];
    [gContent addSubview:radio];

    NSPopUpButton *popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(20, 215, 110, 21)
                                                      pullsDown:NO];
    [gContent addSubview:popup];

    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 175, 120, 22)];
    [field setStringValue:@""];
    [gContent addSubview:field];

    NSSlider *slider = [[NSSlider alloc] initWithFrame:NSMakeRect(20, 130, 120, 21)];
    [slider setMinValue:0];
    [slider setMaxValue:1];
    [slider setDoubleValue:0.6];
    [gContent addSubview:slider];

    NSProgressIndicator *progress =
        [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 95, 140, 16)];
    [progress setStyle:NSProgressIndicatorBarStyle];
    [progress setIndeterminate:NO];
    [progress setMinValue:0];
    [progress setMaxValue:1];
    [progress setDoubleValue:0.6];
    [gContent addSubview:progress];

    NSScroller *scroller = [[NSScroller alloc] initWithFrame:NSMakeRect(250, 60, 15, 200)];
    [scroller setEnabled:YES];
    [scroller setFloatValue:0.3 knobProportion:0.4];
    [gContent addSubview:scroller];

    [gWindow makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];

    for (int attempt = 0; attempt < 5 && !([gWindow isKeyWindow] && [NSApp isActive]); ++attempt) {
        [NSApp activateIgnoringOtherApps:YES];
        [gWindow makeKeyAndOrderFront:nil];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.4]];
    }
    printf("live window key=%d, app active=%d\n",
           (int)[gWindow isKeyWindow], (int)[NSApp isActive]);
    if (!([gWindow isKeyWindow] && [NSApp isActive])) {
        /* Every live control would draw its inactive artwork and every
         * comparison would fail for a reason that has nothing to do with the
         * code under test. */
        fprintf(stderr, "comparecontrols: window never became key; refusing to report.\n");
        return 2;
    }
    printf("\n  %-18s %-9s  %-9s\n", "control", "state", "verdict");

    testControl("button",      push,     TigerControlButton,            NO,  13);
    testControl("checkbox",    checkbox, TigerControlCheckbox,          YES, 13);
    testControl("radio",       radio,    TigerControlRadio,             NO,  13);
    testControl("menulist",    popup,    TigerControlMenuList,          NO,  13);
    testControl("textfield",   field,    TigerControlTextField,         NO,  13);
    testControl("slider",      slider,   TigerControlSliderTrackHorizontal, NO, 13);
    testControl("progressbar", progress, TigerControlProgressBar,       NO,  13);
    testControl("scrollbar",   scroller, TigerControlScrollbarVertical, NO,  13);

    printf("\nidentical %d, near %d, wrong %d, skipped %d\n",
           gIdentical, gNear, gWrong, gSkipped);
    printf("pairs for anything not identical are in %s\n", [gCompareDir UTF8String]);

    [pool release];
    return gWrong ? 1 : 0;
}
