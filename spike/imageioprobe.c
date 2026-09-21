/*
 * imageioprobe.c -- bounded runtime probe comparing Tiger's ImageIO decoding
 * results against modern ImageIO on this Mac, doing what WebCore's
 * ImageDecoderCG.cpp does: incremental CGImageSource feeding, property
 * reads, per-frame CGImage creation, and drawing into the premultiplied-
 * first 32-bit BGRA bitmap WebCore uses. Prints a canonical text dump;
 * meant to be built and run natively and with tiger-clang, then diffed.
 *
 * Native build (this Mac):
 *   clang -O1 -o build/imageioprobe-native spike/imageioprobe.c \
 *     -framework CoreFoundation -framework CoreGraphics -framework ImageIO
 *
 * Tiger build (from repo root, bash, after `make -C compat install`):
 *   toolchain/bin/tiger-clang -O1 -o build/imageioprobe-tiger spike/imageioprobe.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 *
 * Run:
 *   ./build/imageioprobe-native spike/imageioprobe/images/*.{png,jpg,gif,bmp,ico,tiff} > native.txt
 *   scp -O build/imageioprobe-tiger spike/imageioprobe/images/* tiger:/tmp/
 *   ssh tiger '/tmp/imageioprobe-tiger /tmp/*.png /tmp/*.jpg ...' > tiger.txt
 *   diff native.txt tiger.txt
 *
 * Only symbols declared AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER in the
 * 10.4u SDK's CGImageProperties.h are used, so the same source compiles
 * unmodified against both the Tiger overlay and the modern SDK.
 */

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- CRC32 */

static uint32_t crc32_buf(const unsigned char* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int b;
    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        for (b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ---------------------------------------------------------- CF helpers */

static int copyDouble(CFDictionaryRef dict, CFStringRef key, double* out)
{
    CFNumberRef n;
    if (!dict)
        return 0;
    n = (CFNumberRef)CFDictionaryGetValue(dict, key);
    if (!n || CFGetTypeID(n) != CFNumberGetTypeID())
        return 0;
    return CFNumberGetValue(n, kCFNumberDoubleType, out) ? 1 : 0;
}

static int copyBool(CFDictionaryRef dict, CFStringRef key, int* out)
{
    CFBooleanRef b;
    if (!dict)
        return 0;
    b = (CFBooleanRef)CFDictionaryGetValue(dict, key);
    if (!b || CFGetTypeID(b) != CFBooleanGetTypeID())
        return 0;
    *out = CFBooleanGetValue(b) ? 1 : 0;
    return 1;
}

/* Renders a CF string property into a plain C buffer for printing. */
static const char* copyCString(CFDictionaryRef dict, CFStringRef key, char* buf, size_t bufLen)
{
    CFStringRef s;
    if (!dict)
        return NULL;
    s = (CFStringRef)CFDictionaryGetValue(dict, key);
    if (!s || CFGetTypeID(s) != CFStringGetTypeID())
        return NULL;
    if (!CFStringGetCString(s, buf, (CFIndex)bufLen, kCFStringEncodingUTF8))
        return NULL;
    return buf;
}

static const char* statusName(CGImageSourceStatus s)
{
    switch (s) {
    case kCGImageStatusUnexpectedEOF: return "UnexpectedEOF";
    case kCGImageStatusInvalidData: return "InvalidData";
    case kCGImageStatusUnknownType: return "UnknownType";
    case kCGImageStatusReadingHeader: return "ReadingHeader";
    case kCGImageStatusIncomplete: return "Incomplete";
    case kCGImageStatusComplete: return "Complete";
    default: return "?";
    }
}

/* ------------------------------------------------------ pixel sampling */

typedef struct {
    unsigned char* pixels; /* BGRA premultiplied-first, little-endian: byte0=B */
    size_t width, height;
    size_t bytesPerRow;
} Bitmap;

static int renderBitmap(CGImageRef image, Bitmap* bmp)
{
    CGColorSpaceRef rgb;
    CGContextRef ctx;
    memset(bmp, 0, sizeof(*bmp));
    bmp->width = CGImageGetWidth(image);
    bmp->height = CGImageGetHeight(image);
    if (!bmp->width || !bmp->height)
        return 0;
    bmp->bytesPerRow = bmp->width * 4;
    bmp->pixels = (unsigned char*)calloc(bmp->bytesPerRow, bmp->height);
    if (!bmp->pixels)
        return 0;
    rgb = CGColorSpaceCreateDeviceRGB();
    ctx = CGBitmapContextCreate(bmp->pixels, bmp->width, bmp->height, 8, bmp->bytesPerRow, rgb,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(rgb);
    if (!ctx) {
        free(bmp->pixels);
        bmp->pixels = NULL;
        return 0;
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)bmp->width, (CGFloat)bmp->height), image);
    CGContextRelease(ctx);
    return 1;
}

static void samplePoint(const Bitmap* bmp, size_t x, size_t y, char* out, size_t outLen)
{
    const unsigned char* p;
    if (x >= bmp->width) x = bmp->width - 1;
    if (y >= bmp->height) y = bmp->height - 1;
    p = bmp->pixels + y * bmp->bytesPerRow + x * 4;
    /* byte0=B byte1=G byte2=R byte3=A */
    snprintf(out, outLen, "rgba(%3u,%3u,%3u,%3u)", p[2], p[1], p[0], p[3]);
}

static void dumpSamples(const Bitmap* bmp, const char* indent)
{
    size_t w = bmp->width, h = bmp->height;
    char s0[32], s1[32], s2[32], s3[32], s4[32];
    uint32_t crc = crc32_buf(bmp->pixels, bmp->bytesPerRow * bmp->height);
    samplePoint(bmp, 0, 0, s0, sizeof(s0));
    samplePoint(bmp, w - 1, 0, s1, sizeof(s1));
    samplePoint(bmp, 0, h - 1, s2, sizeof(s2));
    samplePoint(bmp, w - 1, h - 1, s3, sizeof(s3));
    samplePoint(bmp, w / 2, h / 2, s4, sizeof(s4));
    printf("%scrc32=%08x TL=%s TR=%s BL=%s BR=%s C=%s\n", indent, crc, s0, s1, s2, s3, s4);
}

/* -------------------------------------------------------- frame dump */

static void dumpTopLevelProperties(CGImageSourceRef src, const char* indent)
{
    CFDictionaryRef props = CGImageSourceCopyProperties(src, NULL);
    double loopCount;
    printf("%stop-level:", indent);
    if (props) {
        CFDictionaryRef gif = (CFDictionaryRef)CFDictionaryGetValue(props, kCGImagePropertyGIFDictionary);
        if (gif && copyDouble(gif, kCGImagePropertyGIFLoopCount, &loopCount))
            printf(" GIFLoopCount=%d", (int)loopCount);
    }
    printf("\n");
    if (props)
        CFRelease(props);
}

static void dumpFrame(CGImageSourceRef src, size_t index, const char* indent)
{
    CGImageSourceStatus st = CGImageSourceGetStatusAtIndex(src, index);
    CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(src, index, NULL);
    double pw = -1, ph = -1, orient = -1, dpiw = -1, dpih = -1, depth = -1, delay = -1;
    int hasAlpha = -1;
    char colorModel[32] = "?";
    CFDictionaryRef gif;

    printf("%s[%zu] status=%s\n", indent, index, statusName(st));
    if (props) {
        copyDouble(props, kCGImagePropertyPixelWidth, &pw);
        copyDouble(props, kCGImagePropertyPixelHeight, &ph);
        copyDouble(props, kCGImagePropertyOrientation, &orient);
        copyDouble(props, kCGImagePropertyDPIWidth, &dpiw);
        copyDouble(props, kCGImagePropertyDPIHeight, &dpih);
        copyDouble(props, kCGImagePropertyDepth, &depth);
        copyBool(props, kCGImagePropertyHasAlpha, &hasAlpha);
        copyCString(props, kCGImagePropertyColorModel, colorModel, sizeof(colorModel));
        gif = (CFDictionaryRef)CFDictionaryGetValue(props, kCGImagePropertyGIFDictionary);
        if (gif)
            copyDouble(gif, kCGImagePropertyGIFDelayTime, &delay);
    }
    printf("%s     w=%g h=%g orient=%g dpi=(%g,%g) depth=%g alpha=%d model=%s",
        indent, pw, ph, orient, dpiw, dpih, depth, hasAlpha, colorModel);
    if (delay >= 0)
        printf(" gifDelay=%g", delay);
    printf("\n");
    if (props)
        CFRelease(props);

    /* Default decode, the path ImageDecoderCG takes. */
    {
        CGImageRef image = CGImageSourceCreateImageAtIndex(src, index, NULL);
        if (!image) {
            printf("%s     CreateImageAtIndex: NULL\n", indent);
        } else {
            Bitmap bmp;
            printf("%s     CreateImageAtIndex: %zux%zu alphaInfo=%d\n", indent,
                CGImageGetWidth(image), CGImageGetHeight(image), (int)CGImageGetAlphaInfo(image));
            if (renderBitmap(image, &bmp)) {
                char line[64];
                snprintf(line, sizeof(line), "%s     draw(default): ", indent);
                printf("%s", line);
                dumpSamples(&bmp, "");
                free(bmp.pixels);
            }
            CGImageRelease(image);
        }
    }

    /* Same, with kCGImageSourceShouldCache explicitly false: WebCore's other call shape. */
    {
        CFStringRef keys[1] = { kCGImageSourceShouldCache };
        CFTypeRef vals[1] = { kCFBooleanFalse };
        CFDictionaryRef opts = CFDictionaryCreate(kCFAllocatorDefault, (const void**)keys,
            (const void**)vals, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CGImageRef image = CGImageSourceCreateImageAtIndex(src, index, opts);
        if (image) {
            Bitmap bmp;
            if (renderBitmap(image, &bmp)) {
                printf("%s     draw(noCache):  ", indent);
                dumpSamples(&bmp, "");
                free(bmp.pixels);
            }
            CGImageRelease(image);
        } else {
            printf("%s     CreateImageAtIndex(noCache): NULL\n", indent);
        }
        CFRelease(opts);
    }
}

static void dumpSource(CGImageSourceRef src, const char* indent)
{
    size_t count = CGImageSourceGetCount(src);
    size_t i;
    printf("%scount=%zu overallStatus=%s\n", indent, count, statusName(CGImageSourceGetStatus(src)));
    dumpTopLevelProperties(src, indent);
    for (i = 0; i < count; ++i)
        dumpFrame(src, i, indent);
}

/* ------------------------------------------------------------- drivers */

static unsigned char* readFile(const char* path, size_t* outLen)
{
    FILE* f = fopen(path, "rb");
    long len;
    unsigned char* buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char*)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *outLen = (size_t)len;
    return buf;
}

static void probeWholeData(const unsigned char* bytes, size_t len)
{
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes, (CFIndex)len);
    CGImageSourceRef src = CGImageSourceCreateWithData(data, NULL);
    printf("  [whole-data]\n");
    if (!src) {
        printf("    CGImageSourceCreateWithData: NULL\n");
    } else {
        dumpSource(src, "    ");
        CFRelease(src);
    }
    CFRelease(data);
}

static void probeIncremental(const unsigned char* bytes, size_t len)
{
    /* Feed in 4 chunks, mirroring ImageDecoderCG's incremental path
       (CGImageSourceCreateIncremental + repeated CGImageSourceUpdateData). */
    const int kChunks = 4;
    CGImageSourceRef src = CGImageSourceCreateIncremental(NULL);
    int i;
    printf("  [incremental, %d chunks]\n", kChunks);
    if (!src) {
        printf("    CGImageSourceCreateIncremental: NULL\n");
        return;
    }
    for (i = 0; i < kChunks; ++i) {
        size_t start = (len * (size_t)i) / (size_t)kChunks;
        size_t end = (len * (size_t)(i + 1)) / (size_t)kChunks;
        int atEnd = (i == kChunks - 1);
        CFDataRef chunk = CFDataCreate(kCFAllocatorDefault, bytes, (CFIndex)end);
        (void)start;
        CGImageSourceUpdateData(src, chunk, atEnd);
        CFRelease(chunk);
        printf("    after chunk %d/%d (%zu/%zu bytes, atEnd=%d): status=%s count=%zu\n",
            i + 1, kChunks, end, len, atEnd, statusName(CGImageSourceGetStatus(src)),
            CGImageSourceGetCount(src));
    }
    dumpSource(src, "    ");
    CFRelease(src);
}

int main(int argc, char** argv)
{
    int i;
    if (argc < 2) {
        fprintf(stderr, "usage: %s image...\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        size_t len = 0;
        unsigned char* bytes = readFile(argv[i], &len);
        const char* base = strrchr(argv[i], '/');
        base = base ? base + 1 : argv[i];
        printf("=== %s (%zu bytes) ===\n", base, len);
        if (!bytes) {
            printf("  (could not read file)\n");
            continue;
        }
        probeWholeData(bytes, len);
        probeIncremental(bytes, len);
        free(bytes);
    }
    return 0;
}
