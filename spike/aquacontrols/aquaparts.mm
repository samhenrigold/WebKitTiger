/* aquaparts -- pixel-compare WebCore's ControlParts against live 10.4 NSCells.
 *
 * spike/aquaatlas/comparecontrols.m already answers "does TigerDrawControl match a
 * live control?" at the C API. This one answers the question one layer up, which is
 * the one that can actually be wrong once WebKit is in the picture: does a
 * WebCore::ControlPart -- the thing the 64-bit web process puts on the wire -- come
 * out of ControlFactoryTiger as the same pixels? Everything between the part and the
 * cell is under test: the StyleAppearance -> TigerControlKind mapping, the
 * ControlStyle -> TigerControlStyle state transfer, rectForBounds, and the per-part
 * payload (slider position, progress position, switch on-ness).
 *
 * Harness shape is spike/aquaatlas/comparecontrols.m's, which is spike/textpixel's:
 * draw both sides into bitmaps of identical geometry, compare channel by channel,
 * write only the failures out as PNG pairs, and exit non-zero if any state differs
 * by more than the tolerance. The live pixels come from a real window's backing
 * store via -initWithFocusedViewRect:, never from screencapture.
 *
 * Runs on the box from inside a .app bundle (see README): a bare executable cannot
 * be foregrounded, and every NSCell then silently draws its inactive artwork.
 *
 * NOT COMPILED. Written against the ControlFactory API while the build machine was
 * reserved; the link line in the README has never been run.
 */

#import <Cocoa/Cocoa.h>

#include "config.h"

#include <WebCore/ButtonPart.h>
#include <WebCore/ControlFactory.h>
#include <WebCore/ControlPart.h>
#include <WebCore/ControlStyle.h>
#include <WebCore/FloatRoundedRect.h>
#include <WebCore/GraphicsContextCG.h>
#include <WebCore/MenuListPart.h>
#include <WebCore/ProgressBarPart.h>
#include <WebCore/SearchFieldPart.h>
#include <WebCore/SliderThumbPart.h>
#include <WebCore/SliderTrackPart.h>
#include <WebCore/SwitchPart.h>
#include <WebCore/TextAreaPart.h>
#include <WebCore/TextFieldPart.h>
#include <WebCore/ToggleButtonPart.h>

#include <stdio.h>

using namespace WebCore;

static const int margin = 8;

// A channel may differ by this much and still pass. Zero would be the honest bar and
// is what the 7/48 byte-identical states in spike/aquaatlas/compare reached; the rest
// differ in artwork, so the suite starts at 0 and each known-differing state carries
// its own recorded delta rather than loosening the global number.
static const int defaultTolerance = 0;

static NSWindow *gWindow;
static NSView *gContent;
static NSString *gOutDir;
static int gFailures;

// ------------------------------------------------------------------ live capture

static void ensureWindow()
{
    if (gWindow)
        return;
    NSRect frame = NSMakeRect(0, 0, 400, 200);
    gWindow = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    gContent = [[NSView alloc] initWithFrame:frame];
    [gWindow setContentView:gContent];
    [gWindow makeKeyAndOrderFront:nil];
}

// Draws a real cell into the real window and reads the backing store back.
static NSBitmapImageRep *captureLiveCell(NSCell *cell, NSRect frame)
{
    ensureWindow();
    NSRect captured = NSInsetRect(frame, -margin, -margin);

    [gContent lockFocus];
    [[NSColor windowBackgroundColor] set];
    NSRectFill(captured);
    [cell drawWithFrame:frame inView:gContent];
    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc] initWithFocusedViewRect:captured] autorelease];
    [gContent unlockFocus];
    return rep;
}

// ------------------------------------------------------------- the part under test

// Draws a ControlPart through whatever ControlFactory::create() returns -- which on
// this build is ControlFactoryTiger -- into a bitmap laid out exactly like the live
// capture, so the two can be compared pixel for pixel.
static NSBitmapImageRep *renderPart(ControlPart& part, const ControlStyle& style, NSRect frame)
{
    unsigned width = (unsigned)(frame.size.width + margin * 2);
    unsigned height = (unsigned)(frame.size.height + margin * 2);

    NSBitmapImageRep *rep = [[[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL pixelsWide:width pixelsHigh:height bitsPerSample:8
                 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:width * 4
                    bitsPerPixel:32] autorelease];

    NSGraphicsContext *saved = [NSGraphicsContext currentContext];
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext setCurrentContext:gc];
    [[NSColor windowBackgroundColor] set];
    NSRectFill(NSMakeRect(0, 0, width, height));
    [NSGraphicsContext setCurrentContext:saved];

    GraphicsContextCG context((CGContextRef)[gc graphicsPort]);

    // The part's rect inside the bitmap: the live capture put the control `margin`
    // in from the top-left of the captured region, so this one must too.
    FloatRect rect(margin, margin, frame.size.width, frame.size.height);
    part.draw(context, FloatRoundedRect(rect), 1, style);
    return rep;
}

// ------------------------------------------------------------------- comparison

static bool compare(NSBitmapImageRep *live, NSBitmapImageRep *ours, const char *name, int tolerance)
{
    if ([live pixelsWide] != [ours pixelsWide] || [live pixelsHigh] != [ours pixelsHigh]) {
        printf("FAIL %-40s geometry: live %ldx%ld, ours %ldx%ld\n", name,
            (long)[live pixelsWide], (long)[live pixelsHigh],
            (long)[ours pixelsWide], (long)[ours pixelsHigh]);
        ++gFailures;
        return false;
    }

    unsigned char *a = [live bitmapData];
    unsigned char *b = [ours bitmapData];
    size_t count = (size_t)[live pixelsWide] * [live pixelsHigh] * 4;

    int worst = 0;
    size_t differing = 0;
    for (size_t i = 0; i < count; ++i) {
        int delta = abs((int)a[i] - (int)b[i]);
        if (delta > worst)
            worst = delta;
        if (delta > tolerance)
            ++differing;
    }

    if (!differing) {
        printf("ok   %-40s worst channel delta %d\n", name, worst);
        return true;
    }

    printf("FAIL %-40s %lu channels over tolerance, worst %d\n", name, (unsigned long)differing, worst);
    ++gFailures;

    NSString *base = [gOutDir stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
    [[live representationUsingType:NSPNGFileType properties:nil]
        writeToFile:[base stringByAppendingString:@"-live.png"] atomically:YES];
    [[ours representationUsingType:NSPNGFileType properties:nil]
        writeToFile:[base stringByAppendingString:@"-ours.png"] atomically:YES];
    return false;
}

// ------------------------------------------------------------------- the states

struct StateCase {
    const char *name;
    OptionSet<ControlStyle::State> states;
};

static const StateCase& stateCase(unsigned index)
{
    static const StateCase cases[] = {
        { "normal",        { ControlStyle::State::Enabled, ControlStyle::State::WindowActive } },
        { "pressed",       { ControlStyle::State::Enabled, ControlStyle::State::WindowActive, ControlStyle::State::Pressed } },
        { "disabled",      { ControlStyle::State::WindowActive } },
        { "focused",       { ControlStyle::State::Enabled, ControlStyle::State::WindowActive, ControlStyle::State::Focused } },
        { "inactive",      { ControlStyle::State::Enabled } },
        { "checked",       { ControlStyle::State::Enabled, ControlStyle::State::WindowActive, ControlStyle::State::Checked } },
        { "indeterminate", { ControlStyle::State::Enabled, ControlStyle::State::WindowActive, ControlStyle::State::Indeterminate } },
        { "default",       { ControlStyle::State::Enabled, ControlStyle::State::WindowActive, ControlStyle::State::Default } },
    };
    return cases[index];
}
static const unsigned stateCaseCount = 8;

// Font sizes that select each control size class: 13 regular, 11 small, 9 mini, the
// values +[NSFont systemFontSizeForControlSize:] returns on 10.4.11.
static const float sizeClassFontSizes[3] = { 13, 11, 9 };
static const char *sizeClassNames[3] = { "regular", "small", "mini" };

// --------------------------------------------------------------------- one case

typedef NSCell *(^CellMaker)(const ControlStyle&);

// The live cell's state, set the way compat/aquacontrols.m sets the drawn one. The
// blocks below only choose the cell TYPE; every state comes from here, so the two
// sides cannot drift in how a state is expressed.
static void applyStatesToLiveCell(NSCell *cell, const ControlStyle& style, unsigned sizeClass)
{
    static const NSControlSize appKitSizes[3] = { NSRegularControlSize, NSSmallControlSize, NSMiniControlSize };
    [cell setControlSize:appKitSizes[sizeClass]];

    if (style.states.contains(ControlStyle::State::Indeterminate))
        [cell setState:NSMixedState];
    else
        [cell setState:style.states.contains(ControlStyle::State::Checked) ? NSOnState : NSOffState];

    [cell setEnabled:style.states.contains(ControlStyle::State::Enabled)];
    [cell setHighlighted:style.states.contains(ControlStyle::State::Pressed)];
    [cell setShowsFirstResponder:style.states.contains(ControlStyle::State::Focused)];
}

static void runCase(const char *kindName, ControlPart& part, NSRect frame, CellMaker makeCell, int tolerance)
{
    for (unsigned sizeClass = 0; sizeClass < 3; ++sizeClass) {
        for (unsigned i = 0; i < stateCaseCount; ++i) {
            const StateCase& sc = stateCase(i);

            ControlStyle style;
            style.states = sc.states;
            style.fontSize = sizeClassFontSizes[sizeClass];
            style.zoomFactor = 1;

            char name[128];
            snprintf(name, sizeof(name), "%s-%s-%s", kindName, sizeClassNames[sizeClass], sc.name);

            // The window's key state is the only way to reach a popup's inactive
            // artwork on 10.4; aquacontrols orders its own window out, and the live
            // side has to do the same or the comparison is against the wrong pixels.
            ensureWindow();
            if (sc.states.contains(ControlStyle::State::WindowActive))
                [gWindow makeKeyAndOrderFront:nil];
            else
                [gWindow orderOut:nil];

            NSCell *cell = makeCell(style);
            if (!cell) {
                printf("SKIP %-40s no live cell (view-backed control, see README)\n", name);
                continue;
            }
            applyStatesToLiveCell(cell, style, sizeClass);
            NSBitmapImageRep *live = captureLiveCell(cell, frame);
            NSBitmapImageRep *ours = renderPart(part, style, frame);
            compare(live, ours, name, tolerance);
        }
    }
}

// --------------------------------------------------------------------------- main

int main(int argc, const char *argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    gOutDir = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"/tmp/aquaparts-out";
    [[NSFileManager defaultManager] createDirectoryAtPath:gOutDir attributes:nil];

    // Foreground the process or every live control draws inactive (aquaatlas finding).
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);
    [NSApplication sharedApplication];

    // Push button.
    {
        auto part = ButtonPart::create(StyleAppearance::PushButton);
        runCase("button", part.get(), NSMakeRect(40, 40, 90, 20), ^NSCell *(const ControlStyle&) {
            NSButtonCell *cell = [[[NSButtonCell alloc] init] autorelease];
            [cell setTitle:nil];
            [cell setButtonType:NSMomentaryPushInButton];
            [cell setBezelStyle:NSRoundedBezelStyle];
            return cell;
        }, defaultTolerance);
    }

    // Check box and radio.
    {
        auto checkbox = ToggleButtonPart::create(StyleAppearance::Checkbox);
        runCase("checkbox", checkbox.get(), NSMakeRect(40, 40, 14, 14), ^NSCell *(const ControlStyle&) {
            NSButtonCell *cell = [[[NSButtonCell alloc] init] autorelease];
            [cell setTitle:nil];
            [cell setButtonType:NSSwitchButton];
            [cell setAllowsMixedState:YES];
            return cell;
        }, defaultTolerance);

        auto radio = ToggleButtonPart::create(StyleAppearance::Radio);
        runCase("radio", radio.get(), NSMakeRect(40, 40, 16, 16), ^NSCell *(const ControlStyle&) {
            NSButtonCell *cell = [[[NSButtonCell alloc] init] autorelease];
            [cell setTitle:nil];
            [cell setButtonType:NSRadioButton];
            return cell;
        }, defaultTolerance);
    }

    // Text field, text area, search field.
    {
        auto textField = TextFieldPart::create();
        runCase("textfield", textField.get(), NSMakeRect(40, 40, 160, 22), ^NSCell *(const ControlStyle&) {
            NSTextFieldCell *cell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
            [cell setBezeled:YES];
            [cell setBezelStyle:NSTextFieldSquareBezel];
            [cell setDrawsBackground:YES];
            return cell;
        }, defaultTolerance);

        auto textArea = TextAreaPart::create(StyleAppearance::TextArea);
        runCase("textarea", textArea.get(), NSMakeRect(40, 40, 160, 60), ^NSCell *(const ControlStyle&) {
            NSTextFieldCell *cell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
            [cell setBezeled:YES];
            [cell setBezelStyle:NSTextFieldSquareBezel];
            [cell setDrawsBackground:YES];
            return cell;
        }, defaultTolerance);

        auto searchField = SearchFieldPart::create();
        runCase("searchfield", searchField.get(), NSMakeRect(40, 40, 160, 22), ^NSCell *(const ControlStyle&) {
            NSTextFieldCell *cell = [[[NSTextFieldCell alloc] initTextCell:@""] autorelease];
            [cell setBezeled:YES];
            [cell setBezelStyle:NSTextFieldRoundedBezel];
            [cell setDrawsBackground:YES];
            return cell;
        }, defaultTolerance);
    }

    // Menu list.
    {
        auto menuList = MenuListPart::create();
        runCase("menulist", menuList.get(), NSMakeRect(40, 40, 120, 21), ^NSCell *(const ControlStyle&) {
            NSPopUpButtonCell *cell = [[[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO] autorelease];
            [cell setUsesItemFromMenu:NO];
            return cell;
        }, defaultTolerance);
    }

    // Slider thumb and track. The track carries the thumb position, which is the part
    // payload this harness exists to check: a track drawn at 0 when the part says 0.75
    // is a bug the C-level atlas cannot see.
    {
        auto thumb = SliderThumbPart::create(StyleAppearance::SliderThumbHorizontal);
        runCase("sliderthumb", thumb.get(), NSMakeRect(40, 40, 15, 15), ^NSCell *(const ControlStyle&) {
            NSSliderCell *cell = [[[NSSliderCell alloc] init] autorelease];
            [cell setSliderType:NSLinearSlider];
            [cell setControlSize:NSSmallControlSize];
            return cell;
        }, defaultTolerance);

        auto track = SliderTrackPart::create(StyleAppearance::SliderHorizontal,
            IntSize(15, 15), IntRect(0, 0, 160, 15), Vector<double>(), 0.75);
        runCase("slidertrack", track.get(), NSMakeRect(40, 40, 160, 15), ^NSCell *(const ControlStyle&) {
            NSSliderCell *cell = [[[NSSliderCell alloc] init] autorelease];
            [cell setSliderType:NSLinearSlider];
            [cell setControlSize:NSSmallControlSize];
            [cell setMinValue:0];
            [cell setMaxValue:1];
            [cell setDoubleValue:0.75];
            return cell;
        }, defaultTolerance);
    }

    // Progress bar. NSProgressIndicator is a view, not a cell, so the live side of
    // this one needs a view capture, which is not written: the block returns nil and
    // every progress state reports SKIP. STUB, listed in the README.
    {
        auto progress = ProgressBarPart::create(0.4, Seconds(0));
        runCase("progressbar", progress.get(), NSMakeRect(40, 40, 160, 16), ^NSCell *(const ControlStyle&) {
            return nil;
        }, defaultTolerance);
    }

    // Switch: drawn as a check box, so it is compared against a check box. This case
    // is the one that documents the divergence rather than hiding it.
    {
        auto switchPart = SwitchPart::create(true, 1);
        runCase("switch", switchPart.get(), NSMakeRect(40, 40, 14, 14), ^NSCell *(const ControlStyle&) {
            NSButtonCell *cell = [[[NSButtonCell alloc] init] autorelease];
            [cell setTitle:nil];
            [cell setButtonType:NSSwitchButton];
            [cell setState:NSOnState];
            return cell;
        }, defaultTolerance);
    }

    printf("\n%s: %d failing states\n", gFailures ? "FAIL" : "PASS", gFailures);
    [pool release];
    return gFailures ? 1 : 0;
}
