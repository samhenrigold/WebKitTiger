/* @available / __builtin_available support. Clang lowers these to compiler-rt's
 * __isOSVersionAtLeast / __isPlatformVersionAtLeast, whose os_version_check.c needs dispatch_once and
 * 10.7-era CF, so it is not in our builtins archive. Read the version straight out of the plist instead. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#define PLATFORM_MACOS 1 /* from os_version_check.c / <mach-o/loader.h> */

static uint32_t osMajor, osMinor, osSubminor;
static pthread_once_t osOnce = PTHREAD_ONCE_INIT;

static void tigerReadOSVersion(void)
{
    /* Conservative default: the oldest thing we ever run on. */
    osMajor = 10; osMinor = 4; osSubminor = 0;

    FILE *f = fopen("/System/Library/CoreServices/SystemVersion.plist", "r");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    const char *key = strstr(buf, "<key>ProductVersion</key>");
    const char *val = key ? strstr(key, "<string>") : NULL;
    if (!val) return;
    unsigned a = 0, b = 0, c = 0;
    if (sscanf(val + strlen("<string>"), "%u.%u.%u", &a, &b, &c) >= 2) {
        osMajor = a; osMinor = b; osSubminor = c;
    }
}

int32_t __isOSVersionAtLeast(int32_t major, int32_t minor, int32_t subminor)
{
    pthread_once(&osOnce, tigerReadOSVersion);
    if (osMajor != (uint32_t)major) return osMajor > (uint32_t)major;
    if (osMinor != (uint32_t)minor) return osMinor > (uint32_t)minor;
    return osSubminor >= (uint32_t)subminor;
}

int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor, uint32_t subminor)
{
    if (platform != PLATFORM_MACOS) return 0; /* this toolchain only ever targets macOS */
    return __isOSVersionAtLeast((int32_t)major, (int32_t)minor, (int32_t)subminor);
}
