// The WeakObjCPtr.h scenario: ARC weak entry points called from a NON-ARC translation unit.
#import <Foundation/Foundation.h>

// Verbatim from WebKit's wtf/spi/cocoa/objcSPI.h, to prove our declarations do not clash with it.
extern "C" {
id objc_loadWeakRetained(id*);
id objc_initWeak(id*, id);
void objc_destroyWeak(id*);
void objc_copyWeak(id*, id*);
void objc_moveWeak(id*, id*);
}

#import <objc/runtime.h>
#include <stdio.h>

static int failures = 0;
static void T(const char *what, bool ok) { printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

static int deallocs = 0;
@interface Subject : NSObject @end
@implementation Subject
- (void)dealloc { deallocs++; [super dealloc]; }
@end

/* objc_loadWeak autoreleases its result, which would perturb the retain counts these tests check.
   Compare through loadWeakRetained and drop the reference immediately instead. */
static id weakPeek(id *slot)
{
    id o = objc_loadWeakRetained(slot);
    [o release];
    return o;
}

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    // What WeakObjCPtr does: store on assignment, load on deref.
    id slot = nil;
    Subject *s = [[Subject alloc] init];
    objc_storeWeak(&slot, s);
    T("objc_storeWeak then weak load", weakPeek(&slot) == s);
    id retained = objc_loadWeakRetained(&slot);
    T("objc_loadWeakRetained returns +1", retained == s && [s retainCount] == 2);
    [retained release];

    {   /* objc_loadWeak really does autorelease, so check it inside its own pool. */
        NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
        T("objc_loadWeak returns the object", objc_loadWeak(&slot) == s);
        T("objc_loadWeak autoreleased it", [s retainCount] == 2);
        [inner release];
    }
    T("autorelease balanced after pool", [s retainCount] == 1);

    [s release];
    T("weak slot zeroed on dealloc", deallocs == 1 && weakPeek(&slot) == nil);
    objc_destroyWeak(&slot);

    // initWeak / destroyWeak
    Subject *s2 = [[Subject alloc] init];
    id slot2;
    objc_initWeak(&slot2, s2);
    T("objc_initWeak", weakPeek(&slot2) == s2);
    objc_destroyWeak(&slot2);
    [s2 release];
    T("objc_destroyWeak unregisters", deallocs == 2);

    // copyWeak / moveWeak
    Subject *s3 = [[Subject alloc] init];
    id a = nil, b, c;
    objc_storeWeak(&a, s3);
    objc_copyWeak(&b, &a);
    T("objc_copyWeak", weakPeek(&b) == s3 && weakPeek(&a) == s3);
    objc_moveWeak(&c, &b);
    T("objc_moveWeak", weakPeek(&c) == s3);
    T("objc_moveWeak cleared the source", b == nil);
    [s3 release];
    T("all live copies zeroed on dealloc",
      deallocs == 3 && weakPeek(&a) == nil && weakPeek(&c) == nil);
    objc_destroyWeak(&a);
    objc_destroyWeak(&c);

    [pool release];
    printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
