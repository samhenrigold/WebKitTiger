// Test for -fobjc-fragile-extension-ivars: ivars declared in a class extension
// and in an @implementation block, plus auto-synthesized @property ivars, on
// the fragile (Mac OS X 10.4 i386) Objective-C ABI.
#import <Foundation/Foundation.h>
#import <objc/objc-class.h>
#include <stdio.h>
#include <string.h>

#if __has_feature(objc_arc)
#define VOIDP(x) ((__bridge void *)(x))
#define RELEASE(x) do {} while (0)
#else
#define VOIDP(x) ((void *)(x))
#define RELEASE(x) [(x) release]
#endif

static unsigned instanceSize(Class c) {
    return (unsigned)((struct objc_class *)VOIDP(c))->instance_size;
}

// Offset of an ivar as recorded in the class's emitted fragile-ABI ivar list.
// Also proves extension / @implementation ivars reach the runtime metadata.
static unsigned runtimeIvarOffset(Class c, const char *name) {
    struct objc_ivar_list *l = ((struct objc_class *)VOIDP(c))->ivars;
    if (l)
        for (int i = 0; i < l->ivar_count; ++i)
            if (strcmp(l->ivar_list[i].ivar_name, name) == 0)
                return (unsigned)l->ivar_list[i].ivar_offset;
    printf("  !! ivar %s missing from runtime ivar list of %s\n", name,
           ((struct objc_class *)VOIDP(c))->name);
    return ~0u;
}

struct Slot { const char *name; unsigned off, size; };

// Returns 0 on success. Slots must not overlap and must fit in instSize.
static int checkSlots(const char *cls, const Slot *s, unsigned n, unsigned instSize) {
    int bad = 0;
    printf("%s: instance_size=%u\n", cls, instSize);
    for (unsigned i = 0; i < n; ++i) {
        printf("  %-10s off=%3u size=%2u\n", s[i].name, s[i].off, s[i].size);
        if (s[i].off + s[i].size > instSize) {
            printf("  !! %s runs past instance_size\n", s[i].name);
            bad = 1;
        }
        for (unsigned j = 0; j < i; ++j)
            if (s[i].off < s[j].off + s[j].size && s[j].off < s[i].off + s[i].size) {
                printf("  !! %s overlaps %s\n", s[i].name, s[j].name);
                bad = 1;
            }
    }
    return bad;
}

#define SLOT(f) { #f, (unsigned)((char *)&self->f - (char *)VOIDP(self)), (unsigned)sizeof(self->f) }
// ARC forbids casting the address of a __strong ivar, so ask the runtime.
#define RSLOT(c, n) { n, runtimeIvarOffset([c class], n), (unsigned)sizeof(void *) }

struct Cxx { int v; double d; Cxx() : v(0), d(0) {} };

@interface Base : NSObject {
    int ifaceInt;
    double ifaceDouble;
}
@property (nonatomic, assign) int pInt;             // auto-synthesized ivar
@property (nonatomic, copy) NSString *pStr;         // auto-synthesized ivar
- (void)fill;
- (int)verify;
@end

// (b) ivars in a class extension
@interface Base () {
    int extInt;
    char extBuf[7];
}
@property (nonatomic, assign) long pExtLong;        // auto-synthesized, extension
@end

// (a) ivars in an @implementation block
@implementation Base {
    long implLong;
    Cxx implCxx;
}
- (void)fill {
    ifaceInt = 11; ifaceDouble = 22.5;
    extInt = 33; strcpy(extBuf, "ext");
    implLong = 44; implCxx.v = 55; implCxx.d = 66.5;
    self.pInt = 77; self.pStr = @"str"; self.pExtLong = 88;
}
- (int)verify {
    int bad = 0;
#define CHK(c) do { if (!(c)) { printf("  !! failed: %s\n", #c); bad = 1; } } while (0)
    CHK(ifaceInt == 11); CHK(ifaceDouble == 22.5);
    CHK(extInt == 33); CHK(strcmp(extBuf, "ext") == 0);
    CHK(implLong == 44); CHK(implCxx.v == 55); CHK(implCxx.d == 66.5);
    CHK(self.pInt == 77); CHK([self.pStr isEqualToString:@"str"]); CHK(self.pExtLong == 88);
    CHK((unsigned)((char *)&self->extInt - (char *)VOIDP(self)) ==
        runtimeIvarOffset([Base class], "extInt"));
    CHK((unsigned)((char *)&self->implLong - (char *)VOIDP(self)) ==
        runtimeIvarOffset([Base class], "implLong"));
    Slot slots[] = { SLOT(ifaceInt), SLOT(ifaceDouble), SLOT(extInt), SLOT(extBuf),
                     SLOT(implLong), SLOT(implCxx), SLOT(_pInt),
                     RSLOT(Base, "_pStr"), SLOT(_pExtLong) };
    bad |= checkSlots("Base", slots, sizeof(slots) / sizeof(slots[0]),
                      instanceSize([Base class]));
    return bad;
}
#if !__has_feature(objc_arc)
- (void)dealloc { [_pStr release]; [super dealloc]; }
#endif
@end

@interface Derived : Base {
    int derInt;
}
@property (nonatomic, assign) float pFloat;         // auto-synthesized ivar
@end

@interface Derived () {
    short derExtShort;
}
@end

@implementation Derived {
    char derImplChar;
}
- (void)fill {
    [super fill];
    derInt = 111; derExtShort = 222; derImplChar = 'z'; self.pFloat = 3.5f;
}
- (int)verify {
    int bad = [super verify];
    CHK(derInt == 111); CHK(derExtShort == 222); CHK(derImplChar == 'z');
    CHK(self.pFloat == 3.5f);
    unsigned baseSize = instanceSize([Base class]);
    Slot slots[] = { SLOT(derInt), SLOT(derExtShort), SLOT(derImplChar), SLOT(_pFloat) };
    for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i)
        if (slots[i].off < baseSize) {
            printf("  !! Derived ivar %s at %u overlaps Base (size %u)\n",
                   slots[i].name, slots[i].off, baseSize);
            bad = 1;
        }
    bad |= checkSlots("Derived", slots, sizeof(slots) / sizeof(slots[0]),
                      instanceSize([Derived class]));
    return bad;
}
@end

int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int bad = 0;
#if __has_feature(objc_arc)
    printf("mode: ARC\n");
#else
    printf("mode: MRR\n");
#endif
    {
        Base *b = [[Base alloc] init];
        [b fill];
        bad |= [b verify];
        RELEASE(b);
    }
    {
        Derived *d = [[Derived alloc] init];
        [d fill];
        bad |= [d verify];
        RELEASE(d);
    }
    printf(bad ? "FAIL\n" : "PASS\n");
    RELEASE(pool);
    return bad;
}
