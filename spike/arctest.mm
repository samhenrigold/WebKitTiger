// ARC + blocks on Tiger's fragile runtime. Build: see spike/run.sh
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>

static int failures = 0;
static void T(const char *what, bool ok) { printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

// ARC forbids writing [x retain] directly. Return uintptr_t so ARC does not treat the
// result as a +0 object and insert objc_retainAutoreleasedReturnValue on it.
static uintptr_t msgv(id r, const char *sel) { typedef uintptr_t (*F)(id, SEL); return ((F)objc_msgSend)(r, sel_registerName(sel)); }
static id msg0(id r, const char *sel) { return (__bridge id)(void *)msgv(r, sel); }

static int deallocs = 0;

@interface Node : NSObject { NSString *_name; Node *_child; __weak Node *_parent; }
@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) Node *child;
@property (nonatomic, weak) Node *parent;
@end
@implementation Node
@synthesize name = _name; @synthesize child = _child; @synthesize parent = _parent;
- (void)dealloc { deallocs++; }
@end

typedef void (^Thunk)(void);
static Thunk makeThunk(Node *n) { return ^{ printf("  block sees %s\n", [n.name UTF8String]); }; }

int main()
{
    setvbuf(stdout, 0, _IONBF, 0);
    __weak Node *weakRoot = nil;
    Thunk t;
    @autoreleasepool {
        Node *root = [Node new]; root.name = @"root";
        Node *kid = [Node new]; kid.name = @"kid"; kid.parent = root; root.child = kid;
        weakRoot = root;
        t = makeThunk(kid);
        T("weak property round-trips", [kid.parent.name isEqualToString:@"root"]);
        T("weak local round-trips", [weakRoot.name isEqualToString:@"root"]);
    }
    T("weak ref zeroed after owner died", weakRoot == nil);
    T("root+kid still retained by block", deallocs == 1);
    t();
    t = nil;
    T("block release drops captured object", deallocs == 2);

    // objc_storeStrong via an ARC __strong out-parameter pattern.
    @autoreleasepool {
        deallocs = 0;
        Node *a = [Node new]; a.name = @"a";
        Node *slot = a;
        slot = nil;
        T("objc_storeStrong balanced", deallocs == 0);
        a = nil;
        T("last strong ref deallocs", deallocs == 1);
    }

    // Blocks must be real objects: Foundation holds and messages them.
    @autoreleasepool {
        __block int calls = 0;
        Thunk heap = ^{ calls++; };
        NSArray *arr = [NSArray arrayWithObject:heap];
        T("block is an NSObject subclass", [heap isKindOfClass:[NSObject class]]);
        T("block class name", strncmp(object_getClassName(heap), "__NS", 4) == 0);
        Thunk fromArray = (Thunk)[arr objectAtIndex:0];
        fromArray();
        T("block survived NSArray retain", calls == 1);
        Thunk copied = [heap copy];
        copied();
        T("-copy on a block works", calls == 2);
        id retained = msg0(heap, "retain");
        msgv(retained, "release");
        T("-retain/-release on a block balance", calls == 2);
        NSMutableArray *m = [NSMutableArray array];
        [m addObject:heap];
        [m removeAllObjects];
        T("NSMutableArray add/remove of block", calls == 2);
        ((Thunk)heap)();
        T("block alive after array released it", calls == 3);
    }

    // A global block (captures nothing) must also be messageable.
    @autoreleasepool {
        Thunk g = ^{ };
        T("global block -retain is identity", msg0(g, "retain") == (id)g);
        T("global block in NSArray", [[NSArray arrayWithObject:g] objectAtIndex:0] == (id)g);
    }

    printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
