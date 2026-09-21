/* blendtest.c -- does Tiger honour the 10.5 Porter-Duff blend modes?
 *
 * Answer, measured on 10.4.11: no, none of the twelve. All composite exactly as
 * kCGBlendModeNormal. CGCompat.h defines the values so WebCore compiles; this
 * is the evidence that defining them is all we can do. See compat/CG-SURVEY.md.
 *
 * Build (from the repo root, in bash):
 *   toolchain/bin/tiger-clang -O1 -o build/blendtest spike/blendtest.c \
 *     -F compat/sdk-overlay \
 *     -F sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
 *     -ltigercompat -framework ApplicationServices
 *
   Each mode composites opaque red over half-alpha green and reports the pixel.
   A mode Tiger does not know should look identical to Normal. */
#include <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
static const struct { int mode; const char* name; } kModes[] = {
    { 0, "Normal" }, { 16, "Clear" }, { 17, "Copy" }, { 18, "SourceIn" },
    { 19, "SourceOut" }, { 20, "SourceAtop" }, { 21, "DestinationOver" },
    { 22, "DestinationIn" }, { 23, "DestinationOut" }, { 24, "DestinationAtop" },
    { 25, "XOR" }, { 26, "PlusDarker" }, { 27, "PlusLighter" },
};
int main(void)
{
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    size_t i;
    unsigned normal = 0;
    for (i = 0; i < sizeof(kModes) / sizeof(kModes[0]); ++i) {
        unsigned char px[4] = { 0, 0, 0, 0 };
        unsigned packed;
        CGContextRef c = CGBitmapContextCreate(px, 1, 1, 8, 4, cs,
            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        /* destination: green at half alpha */
        CGContextSetRGBFillColor(c, 0, 1, 0, 0.5f);
        CGContextFillRect(c, CGRectMake(0, 0, 1, 1));
        /* source: opaque red through the mode under test */
        CGContextSetBlendMode(c, (CGBlendMode)kModes[i].mode);
        CGContextSetRGBFillColor(c, 1, 0, 0, 1);
        CGContextFillRect(c, CGRectMake(0, 0, 1, 1));
        packed = (px[3] << 24) | (px[2] << 16) | (px[1] << 8) | px[0];
        if (!i) normal = packed;
        printf("  %-16s (%2d)  B=%3d G=%3d R=%3d A=%3d   %s\n", kModes[i].name, kModes[i].mode,
            px[0], px[1], px[2], px[3],
            !i ? "" : (packed == normal ? "SAME AS NORMAL -> not honoured" : "distinct"));
        CGContextRelease(c);
    }
    return 0;
}
