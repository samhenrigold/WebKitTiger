/* cfabitest -- behavioural backstop for the CoreFoundation ABI screen
 * (logs/abi-screen-cf.md). The screen says every CF entry point WebKit calls
 * has the same i386 stack footprint on Tiger as in the modern SDK. A matching
 * footprint still does not prove the arguments are interpreted the same way,
 * so this exercises the cases where a difference would actually bite:
 * by-value doubles, CFRange passed by value, and an 8-byte struct returned in
 * EAX:EDX. Build with tiger-clang, run on the box. Prints PASS/FAIL, exits 0.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

/* 10.5+ in the public headers, but Tiger's CoreFoundation exports it and the
 * screen says its footprint already matches. WTF calls it from
 * TextBreakIteratorCFCharacterCluster.h. */
extern CFRange CFStringGetRangeOfCharacterClusterAtIndex(CFStringRef, CFIndex, int);

static int failures;
static int g_fired;
static void timerFired(CFRunLoopTimerRef, void *);

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
    if (!ok)
        failures++;
}

/* --- by-value double: CFRunLoopRunInMode(mode, double seconds, Boolean) --- */
static void testRunLoopRunInMode(void)
{
    /* An empty mode returns kCFRunLoopRunFinished immediately and never
     * consults the timeout at all, so the mode needs a source before the
     * timeout means anything. A timer far in the future is the cheapest one. */
    CFRunLoopTimerRef keepAlive = CFRunLoopTimerCreate(kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + 3600.0, 0, 0, 0, timerFired, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), keepAlive, kCFRunLoopDefaultMode);

    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    SInt32 r = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
    CFAbsoluteTime elapsed = CFAbsoluteTimeGetCurrent() - t0;

    /* If the double landed at the wrong offset the timeout is garbage: either
     * it returns immediately or it blocks far too long. */
    check(r == kCFRunLoopRunTimedOut,
        "CFRunLoopRunInMode times out rather than finishing early");
    check(elapsed > 0.15 && elapsed < 2.0,
        "CFRunLoopRunInMode honours a 0.25 s by-value double timeout");
    printf("      (elapsed %.3f s, result %d)\n", (double)elapsed, (int)r);

    CFRunLoopTimerInvalidate(keepAlive);
    CFRelease(keepAlive);
}

/* --- two by-value doubles: CFRunLoopTimerCreate(alloc, fire, interval, ...) - */
static void timerFired(CFRunLoopTimerRef t, void *info) { (void)t; (void)info; g_fired++; }

static void testRunLoopTimer(void)
{
    CFAbsoluteTime start = CFAbsoluteTimeGetCurrent() + 0.10;
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault, start, 0.05,
        0, 0, timerFired, NULL);
    check(timer != NULL, "CFRunLoopTimerCreate with two by-value doubles returns a timer");
    if (!timer)
        return;

    /* The fire date must survive the round trip through the second double. */
    CFAbsoluteTime got = CFRunLoopTimerGetNextFireDate(timer);
    check(got > start - 0.05 && got < start + 0.05,
        "CFRunLoopTimerGetNextFireDate returns the date that was passed in");

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    while (g_fired < 3 && CFAbsoluteTimeGetCurrent() - t0 < 5.0)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    check(g_fired >= 3, "repeating CFRunLoopTimer fires on its by-value interval");

    CFAbsoluteTime next = CFAbsoluteTimeGetCurrent() + 30.0;
    CFRunLoopTimerSetNextFireDate(timer, next);
    CFAbsoluteTime readback = CFRunLoopTimerGetNextFireDate(timer);
    check(readback > next - 1.0 && readback < next + 1.0,
        "CFRunLoopTimerSetNextFireDate stores the by-value double it was given");

    CFRunLoopTimerInvalidate(timer);
    CFRelease(timer);
}

/* --- CFRange by value ---------------------------------------------------- */
static void testCFRangeByValue(void)
{
    CFStringRef s = CFSTR("abcdefghij");

    UniChar buf[16];
    memset(buf, 0, sizeof(buf));
    CFStringGetCharacters(s, CFRangeMake(3, 4), buf);
    check(buf[0] == 'd' && buf[3] == 'g' && buf[4] == 0,
        "CFStringGetCharacters reads the by-value CFRange {3,4}");

    CFMutableStringRef m = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, CFSTR("aXbXc"));
    CFIndex n = CFStringFindAndReplace(m, CFSTR("X"), CFSTR("-"),
        CFRangeMake(0, CFStringGetLength(m)), 0);
    check(n == 2 && CFStringCompare(m, CFSTR("a-b-c"), 0) == kCFCompareEqualTo,
        "CFStringFindAndReplace honours the by-value CFRange");
    CFRelease(m);

    /* CFRange as the last argument of a longer list. */
    UInt8 bytes[16];
    CFIndex used = 0;
    CFIndex converted = CFStringGetBytes(s, CFRangeMake(2, 3), kCFStringEncodingASCII,
        0, false, bytes, sizeof(bytes), &used);
    check(converted == 3 && used == 3 && bytes[0] == 'c' && bytes[2] == 'e',
        "CFStringGetBytes converts exactly the by-value CFRange {2,3}");

    /* CFRange in the middle of the list, after an array argument. */
    CFMutableArrayRef dst = CFArrayCreateMutable(kCFAllocatorDefault, 0, NULL);
    const void *vals[4] = { CFSTR("0"), CFSTR("1"), CFSTR("2"), CFSTR("3") };
    CFArrayRef src = CFArrayCreate(kCFAllocatorDefault, vals, 4, NULL);
    CFArrayAppendArray(dst, src, CFRangeMake(1, 2));
    check(CFArrayGetCount(dst) == 2
            && CFArrayGetValueAtIndex(dst, 0) == vals[1]
            && CFArrayGetValueAtIndex(dst, 1) == vals[2],
        "CFArrayAppendArray appends exactly the by-value CFRange {1,2}");
    CFRelease(src);
    CFRelease(dst);
}

/* --- 8-byte struct returned in EAX:EDX ----------------------------------- */
static void testCFRangeReturn(void)
{
    CFStringRef s = CFSTR("abcdef");
    CFRange r = CFStringGetRangeOfComposedCharactersAtIndex(s, 2);
    check(r.location == 2 && r.length == 1,
        "CFStringGetRangeOfComposedCharactersAtIndex returns CFRange in registers");

    /* The 10.5+ symbol Tiger nonetheless exports; WTF calls it. */
    CFRange c = CFStringGetRangeOfCharacterClusterAtIndex(s, 3, 0);
    check(c.location <= 3 && c.length >= 1 && c.location + c.length <= 6,
        "CFStringGetRangeOfCharacterClusterAtIndex returns a sane CFRange");
    printf("      (cluster at 3 = {%ld, %ld})\n", (long)c.location, (long)c.length);
}

/* --- double return ------------------------------------------------------- */
static void testDoubleReturn(void)
{
    CFAbsoluteTime a = CFAbsoluteTimeGetCurrent();
    check(a > 1.6e8 && a < 1.0e9,
        "CFAbsoluteTimeGetCurrent returns a plausible double");
    printf("      (CFAbsoluteTime %.1f)\n", (double)a);
}

int main(void)
{
    printf("CoreFoundation i386 ABI spike\n");
    testDoubleReturn();
    testCFRangeByValue();
    testCFRangeReturn();
    testRunLoopRunInMode();
    testRunLoopTimer();
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
