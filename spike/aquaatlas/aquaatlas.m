/* Harness for compat/aquacontrols.m: draw every control kind, state and size
 * class through TigerDrawControl and write the results as PNGs, plus the
 * system colour and font metrics the content process needs.
 *
 * This is a test, not the deliverable. The deliverable is the C API that the
 * 32-bit render process calls; this exists so the artwork can be looked at.
 *
 * Must run from inside a .app bundle, and the bundle's first launch only
 * registers it with LaunchServices, so build.sh burns one. Without that the
 * application never becomes active, no window becomes key, and every control
 * draws its window-inactive artwork with nothing to say so.
 */

#import <Cocoa/Cocoa.h>
#import <TigerCompat/AquaControls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gWritten, gSkipped;

static const struct { TigerControlKind kind; const char *name; unsigned w, h; } kKinds[] = {
    { TigerControlButton,                  "button",            80, 24 },
    { TigerControlDefaultButton,           "default-button",    80, 24 },
    { TigerControlSquareButton,            "square-button",     80, 24 },
    { TigerControlCheckbox,                "checkbox",          20, 20 },
    { TigerControlRadio,                   "radio",             20, 20 },
    { TigerControlMenuList,                "menulist",          90, 26 },
    { TigerControlMenuListButton,          "menulist-button",   90, 26 },
    { TigerControlTextField,               "textfield",        120, 24 },
    { TigerControlTextArea,                "textarea",         120, 48 },
    { TigerControlSearchField,             "searchfield",      120, 24 },
    { TigerControlSliderTrackHorizontal,   "slider-track-h",   120, 22 },
    { TigerControlSliderTrackVertical,     "slider-track-v",    22, 120 },
    { TigerControlSliderThumbHorizontal,   "slider-thumb-h",    20, 22 },
    { TigerControlSliderThumbVertical,     "slider-thumb-v",    22, 20 },
    { TigerControlProgressBar,             "progressbar",      140, 18 },
    { TigerControlMeter,                   "meter",            120, 18 },
    { TigerControlInnerSpinButton,         "spinbutton",        16, 26 },
    { TigerControlScrollbarVertical,       "scrollbar-v",       15, 120 },
    { TigerControlScrollbarHorizontal,     "scrollbar-h",      120, 15 },
    { TigerControlFocusRing,               "focus-ring",        60, 26 },
};
static const unsigned kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

static const struct { const char *name; unsigned add; unsigned remove; } kStates[] = {
    { "normal",        0, 0 },
    { "pressed",       TigerControlStatePressed, 0 },
    { "disabled",      0, TigerControlStateEnabled },
    { "checked",       TigerControlStateChecked, 0 },
    { "indeterminate", TigerControlStateIndeterminate, 0 },
    { "focused",       TigerControlStateFocused, 0 },
    { "default",       TigerControlStateDefault, 0 },
    { "readonly",      TigerControlStateReadOnly, 0 },
    { "inactive",      0, TigerControlStateWindowActive },
};
static const unsigned kStateCount = sizeof(kStates) / sizeof(kStates[0]);

/* fontSize picks the size class: 13 regular, 11 small, 9 mini. */
static const struct { const char *name; float fontSize; } kSizes[] = {
    { "regular", 13 }, { "small", 11 }, { "mini", 9 },
};

static void writeOne(NSString *dir, const char *kindName, const char *stateName,
                     const char *sizeName, TigerControlKind kind,
                     const TigerControlStyle *style, unsigned w, unsigned h)
{
    size_t rowBytes = (size_t)w * 4;
    unsigned char *data = calloc(rowBytes, h);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(data, w, h, 8, rowBytes, space,
                                             kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(space);
    if (!ctx) { free(data); ++gSkipped; return; }
    CGContextClearRect(ctx, CGRectMake(0, 0, w, h));

    TigerDrawControl(ctx, kind, style);

    NSString *name = [NSString stringWithFormat:@"%s-%s-%s.png", kindName, sizeName, stateName];
    NSString *path = [dir stringByAppendingPathComponent:name];
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, (CFStringRef)path, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    if (dest) {
        CGImageDestinationAddImage(dest, image, NULL);
        if (CGImageDestinationFinalize(dest)) ++gWritten; else ++gSkipped;
        CFRelease(dest);
    } else
        ++gSkipped;
    CFRelease(url);
    CGImageRelease(image);
    CGContextRelease(ctx);
    free(data);
}

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);

    NSString *dir = (argc > 1) ? [NSString stringWithUTF8String:argv[1]] : @"controls";
    [[NSFileManager defaultManager] createDirectoryAtPath:dir attributes:nil];

    [NSApplication sharedApplication];

    /* One throwaway draw first: AquaControls creates its drawing window lazily,
     * and an application with no window cannot become active, so checking
     * before this point always says inactive. */
    {
        unsigned char px[4];
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(px, 1, 1, 8, 4, space,
                                                 kCGImageAlphaPremultipliedFirst);
        CGColorSpaceRelease(space);
        if (ctx) {
            TigerControlStyle warm;
            TigerControlStyleInit(&warm, CGRectMake(0, 0, 1, 1));
            TigerDrawControl(ctx, TigerControlButton, &warm);
            CGContextRelease(ctx);
        }
    }

    [NSApp activateIgnoringOtherApps:YES];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.4]];

    /* No activation guard here, unlike the earlier atlas generator. Nothing in
     * TigerDrawControl reads the ambient window state any more: the controls
     * with a distinct inactive appearance go through HITheme, which takes the
     * state as an argument, and the rest have none to read. That is the point
     * of the redesign, so the harness no longer has to be a foreground app. */

    for (unsigned k = 0; k < kKindCount; ++k) {
        for (unsigned s = 0; s < 3; ++s) {
            for (unsigned v = 0; v < kStateCount; ++v) {
                TigerControlStyle style;
                TigerControlStyleInit(&style, CGRectMake(0, 0, kKinds[k].w, kKinds[k].h));
                style.fontSize = kSizes[s].fontSize;
                style.states |= kStates[v].add;
                style.states &= ~kStates[v].remove;
                style.value = 0.6;
                writeOne(dir, kKinds[k].name, kStates[v].name, kSizes[s].name,
                         kKinds[k].kind, &style, kKinds[k].w, kKinds[k].h);
            }
        }
    }

    NSString *metrics = [dir stringByAppendingPathComponent:@"metrics.json"];
    int ok = TigerWriteControlMetricsJSON([metrics UTF8String]);
    printf("wrote %d images, skipped %d, metrics %s\n", gWritten, gSkipped, ok ? "ok" : "FAILED");
    [pool release];
    return gWritten ? 0 : 1;
}
