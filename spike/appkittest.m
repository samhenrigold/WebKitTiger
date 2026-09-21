/* Exercises compat/nscompat-appkit.m on Tiger, under a real NSApplication.
   Build and run: spike/run-appkittest.sh */

#import <TigerCompat/FoundationCompat.h>
#import <AppKit/AppKit.h>
#import <TigerCompat/AppKitCompat.h>

#include <stdio.h>

static int failures;
static void expect(const char *name, BOOL ok)
{
    printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

static BOOL sameColor(NSColor *a, NSColor *b)
{
    NSColor *x = [a colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    NSColor *y = [b colorUsingColorSpaceName:NSDeviceRGBColorSpace];
    return x && y && [x isEqual:y];
}

@interface TigerAppKitProbe : NSObject
- (void)runChecks;
@end

@implementation TigerAppKitProbe

- (void)runChecks
{
    /* ---- NSEvent, the 10.7-10.10 additions ---- */
    NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                        location:NSMakePoint(10, 20)
                                   modifierFlags:NSEventModifierFlagCommand
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                     eventNumber:0
                                      clickCount:1
                                        pressure:1.0f];
    expect("NSEvent built with renamed constants",
           event != nil && [event type] == NSLeftMouseDown);
    expect("modifier flags round trip",
           ([event modifierFlags] & NSEventModifierFlagCommand) != 0);
    expect("-phase is NSEventPhaseNone", [event phase] == NSEventPhaseNone);
    expect("-momentumPhase is NSEventPhaseNone", [event momentumPhase] == NSEventPhaseNone);
    expect("-hasPreciseScrollingDeltas is NO", ![event hasPreciseScrollingDeltas]);
    expect("-scrollingDeltaX tracks -deltaX", [event scrollingDeltaX] == [event deltaX]);
    expect("-scrollingDeltaY tracks -deltaY", [event scrollingDeltaY] == [event deltaY]);
    expect("-isDirectionInvertedFromDevice is NO", ![event isDirectionInvertedFromDevice]);
    expect("-stage is 0", [event stage] == 0);
    expect("-CGEvent is NULL", [event CGEvent] == NULL);
    /* Dot syntax has to resolve, which is how WebCore spells most of these. */
    expect("dot syntax on NSEvent", event.stage == 0 && event.phase == NSEventPhaseNone);

    /* ---- backing store ---- */
    NSRect frame = NSMakeRect(0, 0, 200, 100);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    NSView *view = [[NSView alloc] initWithFrame:frame];
    [[window contentView] addSubview:view];

    expect("NSView -backingScaleFactor is 1", [view backingScaleFactor] == 1.0f);
    expect("NSWindow -backingScaleFactor is 1", [window backingScaleFactor] == 1.0f);
    expect("-convertRectToBacking: is identity",
           NSEqualRects([view convertRectToBacking:frame], frame));
    expect("-convertRectFromBacking: is identity",
           NSEqualRects([view convertRectFromBacking:frame], frame));
    expect("-convertPointToBacking: is identity",
           NSEqualPoints([view convertPointToBacking:NSMakePoint(3, 4)], NSMakePoint(3, 4)));
    expect("NSWindow -occlusionState is Visible",
           [window occlusionState] == NSWindowOcclusionStateVisible);
    expect("dot syntax on NSView/NSWindow",
           view.backingScaleFactor == 1.0f
           && window.occlusionState == NSWindowOcclusionStateVisible);

    /* ---- NSColor ---- */
    expect("unemphasizedSelectedContentBackgroundColor",
           sameColor([NSColor unemphasizedSelectedContentBackgroundColor],
                     [NSColor secondarySelectedControlColor]));
    expect("selectedContentBackgroundColor",
           sameColor([NSColor selectedContentBackgroundColor],
                     [NSColor alternateSelectedControlColor]));
    expect("unemphasizedSelectedTextColor",
           sameColor([NSColor unemphasizedSelectedTextColor], [NSColor selectedTextColor]));
    expect("unemphasizedSelectedTextBackgroundColor",
           sameColor([NSColor unemphasizedSelectedTextBackgroundColor],
                     [NSColor selectedTextBackgroundColor]));
    expect("alternatingContentBackgroundColors",
           [[NSColor alternatingContentBackgroundColors] count] >= 2);
    expect("secondary/tertiary/quaternaryLabelColor",
           [NSColor secondaryLabelColor] && [NSColor tertiaryLabelColor]
           && [NSColor quaternaryLabelColor]);
    expect("findHighlightColor is yellow",
           sameColor([NSColor findHighlightColor],
                     [NSColor colorWithCalibratedRed:1 green:1 blue:0 alpha:1]));
    expect("colorWithSRGBRed: black matches calibrated black",
           sameColor([NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:1],
                     [NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:1]));

    /* ---- NSColor <-> CGColor, the ColorMac.mm cache ---- */
    {
        NSColor *red = [NSColor colorWithCalibratedRed:1 green:0 blue:0 alpha:1];
        CGColorRef cg = [red CGColor];
        expect("-CGColor", cg != NULL && CGColorGetNumberOfComponents(cg) == 4);
        NSColor *back = [NSColor colorWithCGColor:cg];
        expect("+colorWithCGColor: round trip", sameColor(back, red));

        CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
        CGFloat grayComponents[2] = { 0.5f, 1.0f };
        CGColorRef cgGray = CGColorCreate(gray, grayComponents);
        expect("+colorWithCGColor: grayscale", [NSColor colorWithCGColor:cgGray] != nil);
        CGColorRelease(cgGray);
        CGColorSpaceRelease(gray);
        expect("+colorWithCGColor:NULL is nil", [NSColor colorWithCGColor:NULL] == nil);
    }

    /* ---- NSData base64, the one Foundation remainder Tiger lacks ---- */
    {
        NSData *data = [@"Man" dataUsingEncoding:NSASCIIStringEncoding];
        expect("base64 of \"Man\"",
               [[data base64EncodedStringWithOptions:0] isEqualToString:@"TWFu"]);
        NSData *one = [@"M" dataUsingEncoding:NSASCIIStringEncoding];
        expect("base64 pads one byte",
               [[one base64EncodedStringWithOptions:0] isEqualToString:@"TQ=="]);
        NSData *two = [@"Ma" dataUsingEncoding:NSASCIIStringEncoding];
        expect("base64 pads two bytes",
               [[two base64EncodedStringWithOptions:0] isEqualToString:@"TWE="]);
        expect("base64 of empty data",
               [[[NSData data] base64EncodedStringWithOptions:0] isEqualToString:@""]);
        /* 240 bytes is 320 base64 characters, so 64-character lines give four
           breaks and no trailing one. */
        NSMutableData *big = [NSMutableData dataWithLength:240];
        NSString *wrapped = [big base64EncodedStringWithOptions:
            NSDataBase64Encoding64CharacterLineLength | NSDataBase64EncodingEndLineWithLineFeed];
        NSArray *lines = [wrapped componentsSeparatedByString:@"\n"];
        expect("base64 64-character line wrapping",
               [lines count] == 5 && [[lines objectAtIndex:0] length] == 64);
        expect("base64 unwrapped by default",
               [[big base64EncodedStringWithOptions:0] rangeOfString:@"\n"].location == NSNotFound);
    }

    [view release];
    [window release];

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    [NSApp stop:nil];
    /* -stop: only takes effect when the next event is dequeued, so post one. */
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0]
             atStart:YES];
}

@end

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);

    [NSApplication sharedApplication];

    /* Run the checks from inside a live run loop, which is the state WebKit's
       AppKit code actually runs in. */
    TigerAppKitProbe *probe = [[TigerAppKitProbe alloc] init];
    [probe performSelector:@selector(runChecks) withObject:nil afterDelay:0.0];
    [NSApp run];

    [probe release];
    [pool release];
    return failures ? 1 : 0;
}
