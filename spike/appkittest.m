/* Exercises compat/nscompat-appkit.m on Tiger, under a real NSApplication.
   Build and run: spike/run-appkittest.sh */

#import <TigerCompat/FoundationCompat.h>
#import <AppKit/AppKit.h>
#import <TigerCompat/AppKitCompat.h>

#include <stdio.h>

/* Tiger's CoreFoundation exports this; the 10.4u SDK header does not declare
   it. Declared here rather than added to TigerCompat/CFCompat.h, which the
   cfcompat track owns. */
extern CFRunLoopRef CFRunLoopGetMain(void);

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
    /* Window-to-screen conversion, the shape PopupMenu.mm uses to place the
       <select> popup. These must be real translations, not identities. */
    {
        [window setFrameOrigin:NSMakePoint(120, 340)];
        NSRect inWindow = NSMakeRect(10, 20, 50, 30);
        NSRect onScreen = [window convertRectToScreen:inWindow];
        NSPoint windowOrigin = [window frame].origin;
        expect("-convertRectToScreen: translates by the window origin",
               onScreen.origin.x == windowOrigin.x + 10
               && onScreen.origin.y == windowOrigin.y + 20);
        expect("-convertRectToScreen: preserves size",
               onScreen.size.width == 50 && onScreen.size.height == 30);
        expect("-convertRectToScreen: is not the identity",
               !NSEqualRects(onScreen, inWindow));
        NSRect roundTrip = [window convertRectFromScreen:onScreen];
        expect("-convertRectFromScreen: round trips",
               NSEqualRects(roundTrip, inWindow));
    }

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
    /* Pin the documented behaviour rather than only the endpoints: the
       components pass through unconverted, so a mid-tone stays where it was.
       A modern system converts sRGB 0.5 to roughly 0.417 in calibrated RGB.
       Asserting this means nobody later reads the shim as a real conversion. */
    {
        NSColor *passedThrough = [[NSColor colorWithSRGBRed:0.25f green:0.5f blue:0.75f alpha:1]
            colorUsingColorSpaceName:NSCalibratedRGBColorSpace];
        CGFloat r = 0, g = 0, b = 0, a = 0;
        [passedThrough getRed:&r green:&g blue:&b alpha:&a];
        expect("colorWithSRGBRed: passes components through unconverted",
               r > 0.24f && r < 0.26f && g > 0.49f && g < 0.51f
               && b > 0.74f && b < 0.76f);
    }

    /* ---- the PAL PopupMenu.mm and WebView.mm shapes ---- */
    {
        expect("NSControlSize renames match the old spellings",
               NSControlSizeRegular == NSRegularControlSize
               && NSControlSizeSmall == NSSmallControlSize
               && NSControlSizeMini == NSMiniControlSize);
        expect("NSControlSizeLarge is distinct",
               NSControlSizeLarge != NSControlSizeRegular
               && NSControlSizeLarge != NSControlSizeSmall
               && NSControlSizeLarge != NSControlSizeMini);

        NSMenu *menu = [[NSMenu alloc] initWithTitle:@"probe"];
        expect("NSMenu -userInterfaceLayoutDirection is LeftToRight",
               [menu userInterfaceLayoutDirection] == NSUserInterfaceLayoutDirectionLeftToRight);
        /* PopupMenu.mm spells it with dot syntax. */
        expect("dot syntax on NSMenu",
               menu.userInterfaceLayoutDirection != NSUserInterfaceLayoutDirectionRightToLeft);

        NSView *probeView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
        expect("NSView -userInterfaceLayoutDirection is LeftToRight",
               probeView.userInterfaceLayoutDirection == NSUserInterfaceLayoutDirectionLeftToRight);

        /* NSNotificationName is a real type, usable where WebKit's SPI headers
           declare notification names with it. */
        NSNotificationName probeName = @"TigerProbeNotification";
        expect("NSNotificationName", [probeName isEqualToString:@"TigerProbeNotification"]);
#if !__has_feature(objc_arc)
        [menu release];
        [probeView release];
#endif
    }

    /* ---- the triage batch from logs/appkit-selector-gaps.md ---- */
    {
        expect("NSWorkspace accessibility display settings are all NO",
               ![[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast]
               && ![[NSWorkspace sharedWorkspace] accessibilityDisplayShouldDifferentiateWithoutColor]
               && ![[NSWorkspace sharedWorkspace] accessibilityDisplayShouldInvertColors]
               && ![[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion]);

        /* A real query, so the only safe assertion with no mouse held down is
           that it answers without raising and reports nothing pressed. */
        expect("NSEvent +pressedMouseButtons", [NSEvent pressedMouseButtons] == 0);

        NSEvent *rightClick = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                                 location:NSZeroPoint modifierFlags:0
                                                timestamp:0 windowNumber:0 context:nil
                                              eventNumber:0 clickCount:1 pressure:1.0f];
        NSEvent *plainClick = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                 location:NSZeroPoint modifierFlags:0
                                                timestamp:0 windowNumber:0 context:nil
                                              eventNumber:0 clickCount:1 pressure:1.0f];
        NSEvent *controlClick = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                   location:NSZeroPoint
                                              modifierFlags:NSEventModifierFlagControl
                                                  timestamp:0 windowNumber:0 context:nil
                                                eventNumber:0 clickCount:1 pressure:1.0f];
        expect("menuTypeForEvent: right click is a context menu",
               [NSMenu menuTypeForEvent:rightClick] == NSMenuTypeContextMenu);
        expect("menuTypeForEvent: control click is a context menu",
               [NSMenu menuTypeForEvent:controlClick] == NSMenuTypeContextMenu);
        expect("menuTypeForEvent: plain click is not",
               [NSMenu menuTypeForEvent:plainClick] == NSMenuTypeNone);
        expect("menuTypeForEvent: nil is not", [NSMenu menuTypeForEvent:nil] == NSMenuTypeNone);

        /* Checking it against +currentRunLoop on the main thread is trivially
           true. Tiger's CoreFoundation exports CFRunLoopGetMain (the 10.4u SDK
           just never declares it), and NSRunLoop can hand back its CFRunLoop,
           so the captured object can be checked against the real main run loop.
           That is what would catch the load-time capture running on the wrong
           thread, which is the only way this shim can be wrong. */
        expect("NSRunLoop +mainRunLoop is really the main run loop",
               [NSRunLoop mainRunLoop] != nil
               && [[NSRunLoop mainRunLoop] getCFRunLoop] == CFRunLoopGetMain());
        expect("NSCalendar +calendarWithIdentifier:",
               [NSCalendar calendarWithIdentifier:NSGregorianCalendar] != nil);

        NSProcessInfo *info = [NSProcessInfo processInfo];
        [info disableSuddenTermination];
        [info enableSuddenTermination];
        expect("sudden termination pair is a no-op that returns", YES);

        /* NSPropertyListSerialization, the 10.6 error-returning signature. */
        NSDictionary *plist = [NSDictionary dictionaryWithObject:@"v" forKey:@"k"];
        NSString *errorText = nil;
        NSData *data = [NSPropertyListSerialization dataFromPropertyList:plist
                            format:NSPropertyListXMLFormat_v1_0 errorDescription:&errorText];
        NSPropertyListFormat format;
        NSError *plistError = nil;
        id parsed = [NSPropertyListSerialization propertyListWithData:data
                        options:NSPropertyListImmutable format:&format error:&plistError];
        expect("propertyListWithData:options:format:error:",
               [[parsed objectForKey:@"k"] isEqualToString:@"v"] && plistError == nil);
        plistError = nil;
        id junk = [NSPropertyListSerialization
                      propertyListWithData:[@"not a plist" dataUsingEncoding:NSASCIIStringEncoding]
                                   options:NSPropertyListImmutable format:&format error:&plistError];
        expect("propertyListWithData: reports an error",
               junk == nil && plistError != nil && [[plistError domain] isEqualToString:NSCocoaErrorDomain]);

        /* NSGraphicsContext, the 10.10 rename. */
        {
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef bitmap = CGBitmapContextCreate(NULL, 8, 8, 8, 0, space,
                                                        kCGImageAlphaPremultipliedFirst);
            NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:bitmap
                                                                            flipped:NO];
            expect("graphicsContextWithCGContext:flipped:",
                   gc != nil && [gc graphicsPort] == bitmap && ![gc isFlipped]);
            CGContextRelease(bitmap);
            CGColorSpaceRelease(space);
        }
    }

    {
        expect("NSCursor +contextualMenuCursor", [NSCursor contextualMenuCursor] != nil);
        expect("NSCursor +dragCopyCursor", [NSCursor dragCopyCursor] != nil);
        [[NSSpellChecker sharedSpellChecker] updatePanels];
        expect("NSSpellChecker -updatePanels returns", YES);
        {
            NSNumber *seven = [[NSNumber alloc] initWithInteger:-7];
            expect("NSNumber -initWithInteger:", [seven integerValue] == -7);
#if !__has_feature(objc_arc)
            [seven release];
#endif
        }
    }

    /* ---- NSScreen ---- */
    {
        NSScreen *main = [NSScreen mainScreen];
        expect("NSScreen -backingScaleFactor is 1",
               main != nil && [main backingScaleFactor] == 1.0f);
        expect("dot syntax on NSScreen", main.backingScaleFactor == 1.0f);
        NSEdgeInsets insets = [main safeAreaInsets];
        expect("NSScreen -safeAreaInsets is all zero",
               insets.top == 0 && insets.left == 0
               && insets.bottom == 0 && insets.right == 0);
        /* Every screen, not just the main one: PlatformScreenMac walks them. */
        BOOL allOne = YES;
        NSEnumerator *e = [[NSScreen screens] objectEnumerator];
        NSScreen *each;
        while ((each = [e nextObject]) != nil) {
            if ([each backingScaleFactor] != 1.0f)
                allOne = NO;
        }
        expect("every NSScreen reports scale 1", allOne);
    }

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

#if !__has_feature(objc_arc)
    [view release];
    [window release];
#endif

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
    @autoreleasepool {
        setbuf(stdout, NULL);

        [NSApplication sharedApplication];

        /* Run the checks from inside a live run loop, which is the state
           WebKit's AppKit code actually runs in. */
        TigerAppKitProbe *probe = [[TigerAppKitProbe alloc] init];
        [probe performSelector:@selector(runChecks) withObject:nil afterDelay:0.0];
        [NSApp run];
#if !__has_feature(objc_arc)
        [probe release];
#endif
    }
    return failures ? 1 : 0;
}
