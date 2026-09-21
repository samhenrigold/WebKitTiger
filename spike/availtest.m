#import <Foundation/Foundation.h>
#include <stdio.h>
extern int32_t __isOSVersionAtLeast(int32_t, int32_t, int32_t);
extern int32_t __isPlatformVersionAtLeast(uint32_t, uint32_t, uint32_t, uint32_t);
static int failures = 0;
static void T(const char *what, int ok) { printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }
int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    T("@available(macOS 10.4, *) is true",  __builtin_available(macOS 10.4, *) ? 1 : 0);
    T("@available(macOS 10.4.11, *) is true", __builtin_available(macOS 10.4.11, *) ? 1 : 0);
    T("@available(macOS 10.5, *) is false", __builtin_available(macOS 10.5, *) ? 0 : 1);
    T("@available(macOS 11, *) is false",   __builtin_available(macOS 11, *) ? 0 : 1);
    T("__isOSVersionAtLeast(10,4,11)",   __isOSVersionAtLeast(10, 4, 11) == 1);
    T("__isOSVersionAtLeast(10,4,12)",   __isOSVersionAtLeast(10, 4, 12) == 0);
    T("__isPlatformVersionAtLeast macOS", __isPlatformVersionAtLeast(1, 10, 4, 0) == 1);
    T("__isPlatformVersionAtLeast other", __isPlatformVersionAtLeast(2, 1, 0, 0) == 0);
    printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
