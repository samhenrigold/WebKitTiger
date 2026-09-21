// Load the Apple TV 3.0.2 QuartzCore (Core Animation) on Mac OS X 10.4.11 and drive it.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <objc/objc-runtime.h>
#include <ApplicationServices/ApplicationServices.h>

static int fails = 0;
#define CHECK(c) do { if (c) printf("ok    %s\n", #c); else { printf("FAIL  %s\n", #c); fails++; } } while (0)

// Hand-rolled decls: we message classes fetched via objc_getClass, so no headers needed.
typedef id (*msg_id)(id, SEL);
typedef id (*msg_id_id)(id, SEL, id);
typedef void (*msg_v_rect)(id, SEL, CGRect);
typedef void (*msg_v_ptr)(id, SEL, void *);
typedef void (*msg_v_flt)(id, SEL, float);

int main(void) {
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    const char *path = "/tmp/QuartzCore.framework/QuartzCore";

    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL  dlopen: %s\n", dlerror()); return 1; }
    printf("ok    dlopen %s\n", path);

    Class LayerCls = objc_getClass("CALayer");
    Class TxnCls = objc_getClass("CATransaction");
    Class AnimCls = objc_getClass("CABasicAnimation");
    Class FilterCls = objc_getClass("CAFilter");
    CHECK(LayerCls != nil);
    CHECK(TxnCls != nil);
    CHECK(AnimCls != nil);
    CHECK(FilterCls != nil);
    CHECK(objc_getClass("CATiledLayer") != nil);
    CHECK(objc_getClass("CATransformLayer") != nil);
    CHECK(objc_getClass("CAShapeLayer") != nil);
    CHECK(objc_getClass("LKLayer") == nil);   // naming is CA-, not LK-
    if (!LayerCls) return 1;

    double (*mediaTime)(void) = dlsym(h, "CACurrentMediaTime");
    CHECK(mediaTime != NULL);
    if (mediaTime) printf("      CACurrentMediaTime() = %f\n", mediaTime());

    id layer = ((msg_id)objc_msgSend)((id)LayerCls, sel_registerName("layer"));
    CHECK(layer != nil);
    printf("      layer class = %s\n", layer ? object_getClassName(layer) : "(nil)");

    // bounds
    CGRect r = CGRectMake(0, 0, 320, 240);
    ((msg_v_rect)objc_msgSend)(layer, sel_registerName("setBounds:"), r);
    printf("ok    setBounds:\n");

    // backgroundColor (CGColorRef)
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    float comps[4] = {1, 0, 0, 1};
    CGColorRef red = CGColorCreate(cs, comps);
    ((msg_v_ptr)objc_msgSend)(layer, sel_registerName("setBackgroundColor:"), (void *)red);
    printf("ok    setBackgroundColor:\n");

    ((msg_v_flt)objc_msgSend)(layer, sel_registerName("setOpacity:"), 0.5f);
    ((msg_v_ptr)objc_msgSend)(layer, sel_registerName("setName:"), @"tigerlayer");
    id name = ((msg_id)objc_msgSend)(layer, sel_registerName("name"));
    CHECK([(NSString *)name isEqualToString:@"tigerlayer"]);

    // sublayer tree
    id child = ((msg_id)objc_msgSend)((id)LayerCls, sel_registerName("layer"));
    ((msg_v_ptr)objc_msgSend)(layer, sel_registerName("addSublayer:"), (void *)child);
    id subs = ((msg_id)objc_msgSend)(layer, sel_registerName("sublayers"));
    CHECK([(NSArray *)subs count] == 1);

    // animation
    id anim = ((msg_id_id)objc_msgSend)((id)AnimCls,
        sel_registerName("animationWithKeyPath:"), @"opacity");
    CHECK(anim != nil);
    if (anim) {
        ((msg_v_ptr)objc_msgSend)(anim, sel_registerName("setToValue:"),
            (void *)[NSNumber numberWithFloat:0.0f]);
        ((msg_v_ptr)objc_msgSend)(layer, sel_registerName("addAnimation:forKey:"), (void *)anim);
    }

    // transaction round trip
    ((msg_id)objc_msgSend)((id)TxnCls, sel_registerName("begin"));
    ((msg_id)objc_msgSend)((id)TxnCls, sel_registerName("commit"));
    printf("ok    CATransaction begin/commit\n");

    // CAFilter (used heavily by WebCore for CSS filters)
    id f = ((msg_id_id)objc_msgSend)((id)FilterCls,
        sel_registerName("filterWithType:"), @"gaussianBlur");
    printf("%s  CAFilter filterWithType:gaussianBlur -> %s\n",
           f ? "ok  " : "note", f ? object_getClassName(f) : "(nil)");

    CGColorRelease(red); CGColorSpaceRelease(cs);
    printf("\n%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
    [pool release];
    return fails ? 1 : 0;
}
