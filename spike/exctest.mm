// ObjC exceptions, C++ exceptions, and the two mixed, under ARC on the fragile runtime.
#import <Foundation/Foundation.h>
#include <stdexcept>
#include <string>
#include <cstdio>

static int failures = 0;
static void T(const char *what, bool ok) { std::printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

static int guards = 0;
struct Guard { ~Guard() { ++guards; } };

// An ObjC frame with @try/@finally that a C++ exception has to pass through.
static void objcFrameThatRethrows(void (*fn)(), int *finallyRan)
{
    @try {
        fn();
    } @finally {
        ++*finallyRan;
    }
}
static void throwCxx() { Guard g; throw std::runtime_error("cxx through objc"); }

@interface Thrower : NSObject
+ (void)raise;
@end
@implementation Thrower
+ (void)raise { @throw [NSException exceptionWithName:@"TigerTest" reason:@"boom" userInfo:nil]; }
@end

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    @autoreleasepool {
        // --- plain ObjC exceptions ---
        bool caught = false;
        @try { [Thrower raise]; }
        @catch (NSException *e) { caught = [[e name] isEqualToString:@"TigerTest"] && [[e reason] isEqualToString:@"boom"]; }
        T("@throw / @catch NSException", caught);

        int finallyRan = 0;
        @try { @try { [Thrower raise]; } @finally { finallyRan = 1; } }
        @catch (NSException *) { }
        T("@finally runs while unwinding", finallyRan == 1);

        caught = false;
        @try { @throw [NSException exceptionWithName:@"Inner" reason:@"r" userInfo:nil]; }
        @catch (NSString *) { }
        @catch (NSException *e) { caught = [[e name] isEqualToString:@"Inner"]; }
        T("@catch picks the matching type", caught);

        // --- C++ exceptions in an ObjC++ TU ---
        caught = false;
        guards = 0;
        try { throwCxx(); } catch (const std::runtime_error &e) { caught = std::string(e.what()) == "cxx through objc"; }
        T("C++ throw/catch in an ObjC++ file", caught && guards == 1);

        // --- C++ exception through an ObjC frame ---
        caught = false;
        guards = 0;
        finallyRan = 0;
        try { objcFrameThatRethrows(&throwCxx, &finallyRan); }
        catch (const std::runtime_error &) { caught = true; }
        // Fragile-ABI ObjC exceptions are setjmp/longjmp based, so DWARF unwinding walks straight
        // past @try/@finally regions: the C++ exception and its destructors survive, @finally does not.
        T("C++ exception passes through an ObjC frame", caught && guards == 1);
        T("known limit: @finally skipped by C++ throw", finallyRan == 0);

        // --- ObjC exception through a C++ frame with a destructor ---
        caught = false;
        guards = 0;
        @try {
            Guard g;
            [Thrower raise];
        } @catch (NSException *) { caught = true; }
        // The mirror image: @throw longjmps out, so C++ destructors in between never run.
        T("ObjC exception caught across a C++ frame", caught);
        T("known limit: C++ dtors skipped by @throw", guards == 0);
    }
    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
