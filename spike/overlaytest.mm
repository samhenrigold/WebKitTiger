// Verifies compat/sdk-overlay: the generics-patched collection headers, the
// modern Foundation macros, the 10.12 NSEvent/NSWindow renames, @available and
// NSInteger, all against the 2005 SDK.
//
// Build (see compat/sdk-overlay/README.md for the flag order):
//   spike/run-overlaytest.sh
//
// Compiles in MRR and ARC with -Wall, links, and runs on the Tiger box.

// FoundationCompat.h is what toolchain/tiger.cmake force-includes ahead of
// every Objective-C translation unit, so this is the real build's header
// configuration. It supplies YES/NO as __objc_yes/__objc_no and the NS_ENUM,
// nullability and annotation macros, then imports Foundation and NSCompat.h.
#import <TigerCompat/FoundationCompat.h>
#import <AppKit/AppKit.h>

#include <cstdio>

static int failures;
static void expect(const char *name, bool ok)
{
    std::printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

// ---- lightweight generics, at file scope so they are real declarations ----
static NSArray<NSString *> *gStrings;
static NSDictionary<NSString *, NSNumber *> *gMap;
static NSSet<NSString *> *gSet;
static NSMutableArray<NSString *> *gMutableStrings;
static NSMutableDictionary<NSString *, NSNumber *> *gMutableMap;
static NSMutableSet<NSString *> *gMutableSet;
static __kindof NSObject *gKindOf;

// ---- NS_ENUM / NS_OPTIONS ----
typedef NS_ENUM(NSInteger, TigerColour) { TigerRed, TigerGreen, TigerBlue };
typedef NS_OPTIONS(NSUInteger, TigerMask) { TigerNone = 0, TigerOne = 1 << 0, TigerTwo = 1 << 1 };

// ---- annotation macros have to parse on a declaration ----
@interface TigerOverlayProbe : NSObject
- (instancetype)init NS_DESIGNATED_INITIALIZER;
- (void)takesBlock:(NS_NOESCAPE void (^)(void))block;
+ (NSString *)name NS_SWIFT_NAME(probeName());
@end

@implementation TigerOverlayProbe
- (instancetype)init { return [super init]; }
- (void)takesBlock:(void (^)(void))block { if (block) block(); }
+ (NSString *)name { return @"probe"; }
@end

int main(int, char **)
{
    @autoreleasepool {

    // ---- generics ----
    gStrings = [NSArray arrayWithObjects:@"a", @"b", nil];
    gMap = [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:1] forKey:@"k"];
    gSet = [NSSet setWithObject:@"s"];
    gMutableStrings = [NSMutableArray arrayWithArray:gStrings];
    gMutableMap = [NSMutableDictionary dictionary];
    gMutableSet = [NSMutableSet set];
    gKindOf = gStrings;
    expect("NSArray<NSString *>", [gStrings count] == 2);
    expect("NSDictionary<NSString *, NSNumber *>", [[gMap objectForKey:@"k"] intValue] == 1);
    expect("NSSet<NSString *>", [gSet count] == 1);
    expect("mutable generic collections",
           gMutableStrings != nil && gMutableMap != nil && gMutableSet != nil);
    expect("__kindof", gKindOf != nil);

    // A specialized element still converts to its type without a cast.
    NSString *first = gStrings[0];
    expect("specialized element type", [first isEqualToString:@"a"]);

    // ---- NSInteger ----
    NSInteger i = NSIntegerMax;
    NSUInteger u = NSUIntegerMax;
    expect("NSInteger is 32-bit here", sizeof(NSInteger) == 4 && i == 2147483647);
    expect("NSUIntegerMax", u == 4294967295U);

    // ---- NS_ENUM / NS_OPTIONS ----
    expect("NS_ENUM", TigerBlue == 2 && sizeof(TigerColour) == sizeof(NSInteger));
    expect("NS_OPTIONS", (TigerOne | TigerTwo) == 3);

    // ---- annotation macros ----
    {
        TigerOverlayProbe *probe = [[TigerOverlayProbe alloc] init];
        __block bool ran = false;
        [probe takesBlock:^{ ran = true; }];
        expect("NS_DESIGNATED_INITIALIZER / NS_NOESCAPE", probe != nil && ran);
        expect("NS_SWIFT_NAME", [[TigerOverlayProbe name] isEqualToString:@"probe"]);
#if !__has_feature(objc_arc)
        [probe release];
#endif
    }

    // ---- nullability ----
    NS_ASSUME_NONNULL_BEGIN
    NSString *nonNullString = @"x";
    NS_ASSUME_NONNULL_END
    expect("NS_ASSUME_NONNULL_BEGIN/END", nonNullString != nil);

    // ---- NSEvent 10.12 renames: the new spelling must equal the old value ----
    expect("NSEventTypeLeftMouseDown", NSEventTypeLeftMouseDown == NSLeftMouseDown);
    expect("NSEventTypeKeyDown", NSEventTypeKeyDown == NSKeyDown);
    expect("NSEventTypeScrollWheel", NSEventTypeScrollWheel == NSScrollWheel);
    expect("NSEventTypeOtherMouseDragged", NSEventTypeOtherMouseDragged == NSOtherMouseDragged);
    expect("NSEventMaskAny", NSEventMaskAny == NSAnyEventMask);
    expect("NSEventMaskFlagsChanged", NSEventMaskFlagsChanged == NSFlagsChangedMask);
    expect("NSEventModifierFlagCommand", NSEventModifierFlagCommand == NSCommandKeyMask);
    expect("NSEventModifierFlagControl", NSEventModifierFlagControl == NSControlKeyMask);
    expect("NSEventModifierFlagDeviceIndependentFlagsMask",
           NSEventModifierFlagDeviceIndependentFlagsMask == NSDeviceIndependentModifierFlagsMask);
    {
        NSEventModifierFlags flags = NSEventModifierFlagShift | NSEventModifierFlagOption;
        expect("NSEventModifierFlags type", (flags & NSShiftKeyMask) != 0);
        NSEventPhase phase = NSEventPhaseNone;
        expect("NSEventPhase", phase == 0 && NSEventPhaseMayBegin == 0x20);
    }

    // ---- NSWindowStyleMask renames ----
    expect("NSWindowStyleMaskTitled", NSWindowStyleMaskTitled == NSTitledWindowMask);
    expect("NSWindowStyleMaskBorderless", NSWindowStyleMaskBorderless == NSBorderlessWindowMask);
    expect("NSWindowStyleMaskUtilityWindow", NSWindowStyleMaskUtilityWindow == NSUtilityWindowMask);
    {
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
        expect("NSWindowStyleMask type", (mask & NSClosableWindowMask) != 0);
    }

    // ---- availability ----
    {
        bool sierraOrNewer = false;
        if (@available(macOS 10.12, *))
            sierraOrNewer = true;
        expect("@available(macOS 10.12) is false on Tiger", !sierraOrNewer);
        bool tigerOrNewer = false;
        if (@available(macOS 10.4, *))
            tigerOrNewer = true;
        expect("@available(macOS 10.4) is true on Tiger", tigerOrNewer);
    }
#if __MAC_OS_X_VERSION_MIN_REQUIRED == 1040
    expect("__MAC_OS_X_VERSION_MIN_REQUIRED == 1040", true);
#else
    expect("__MAC_OS_X_VERSION_MIN_REQUIRED == 1040", false);
#endif

    // ---- CGFloat and CFErrorRef from the overlay, not TigerCompat ----
    expect("CGFloat is float on i386", sizeof(CGFloat) == 4);
    { CFErrorRef error = NULL; expect("CFErrorRef", error == NULL); }

    // ---- NSMapTable is a class now, not the C typedef ----
    expect("NSMapTable class", [NSMapTable strongToStrongObjectsMapTable] != nil);
    { NSMapTableCStruct *c = NSCreateMapTable(NSObjectMapKeyCallBacks,
                                              NSObjectMapValueCallBacks, 0);
      expect("NSMapTableCStruct C API", c != NULL);
      NSFreeMapTable(c); }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    }
    return failures ? 1 : 0;
}
