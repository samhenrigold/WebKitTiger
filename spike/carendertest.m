// Can the Apple TV CARenderer actually composite a layer tree into a CGL context on 10.4.11?
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <objc/objc-runtime.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <ApplicationServices/ApplicationServices.h>

typedef id (*msg_id)(id, SEL);
typedef id (*msg_id_2)(id, SEL, void *, void *);
typedef void (*msg_v_ptr)(id, SEL, void *);
typedef void (*msg_v_rect)(id, SEL, CGRect);
typedef void (*msg_v_d_p)(id, SEL, double, void *);
typedef void (*msg_v_pt)(id, SEL, CGPoint);

int main(void) {
    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    if (!dlopen("/tmp/QuartzCore.framework/QuartzCore", RTLD_NOW | RTLD_LOCAL)) {
        printf("FAIL dlopen: %s\n", dlerror()); return 1;
    }
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAPBuffer, kCGLPFAAccelerated, kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        (CGLPixelFormatAttribute)0
    };
    CGLPixelFormatObj pf = NULL; GLint n = 0;
    if (CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError || !pf) {
        // fall back to software
        CGLPixelFormatAttribute soft[] = { kCGLPFAPBuffer, kCGLPFAColorSize,
            (CGLPixelFormatAttribute)32, (CGLPixelFormatAttribute)0 };
        if (CGLChoosePixelFormat(soft, &pf, &n) != kCGLNoError || !pf) {
            printf("FAIL CGLChoosePixelFormat\n"); return 1; }
    }
    CGLContextObj ctx = NULL;
    if (CGLCreateContext(pf, NULL, &ctx) != kCGLNoError) { printf("FAIL CGLCreateContext\n"); return 1; }
    CGLPBufferObj pbuf = NULL;
    if (CGLCreatePBuffer(256, 256, GL_TEXTURE_RECTANGLE_EXT, GL_RGBA, 0, &pbuf) != kCGLNoError) {
        printf("FAIL CGLCreatePBuffer\n"); return 1; }
    CGLSetCurrentContext(ctx);
    GLint screen = 0; CGLGetVirtualScreen(ctx, &screen);
    if (CGLSetPBuffer(ctx, pbuf, 0, 0, screen) != kCGLNoError) { printf("FAIL CGLSetPBuffer\n"); return 1; }
    printf("ok   CGL pbuffer context, renderer=%s\n", glGetString(GL_RENDERER));

    Class RendererCls = objc_getClass("CARenderer");
    Class LayerCls = objc_getClass("CALayer");
    if (!RendererCls) { printf("FAIL no CARenderer class\n"); return 1; }

    id renderer = ((msg_id_2)objc_msgSend)((id)RendererCls,
        sel_registerName("rendererWithCGLContext:options:"), (void *)ctx, NULL);
    printf("%s CARenderer rendererWithCGLContext:options: -> %p\n", renderer ? "ok  " : "FAIL", renderer);
    if (!renderer) return 1;

    id root = ((msg_id)objc_msgSend)((id)LayerCls, sel_registerName("layer"));
    ((msg_v_rect)objc_msgSend)(root, sel_registerName("setBounds:"), CGRectMake(0, 0, 256, 256));
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    float g[4] = {0, 1, 0, 1};
    CGColorRef green = CGColorCreate(cs, g);
    ((msg_v_ptr)objc_msgSend)(root, sel_registerName("setBackgroundColor:"), (void *)green);
    ((msg_v_pt)objc_msgSend)(root, sel_registerName("setPosition:"), CGPointMake(128, 128));
    ((msg_v_ptr)objc_msgSend)(renderer, sel_registerName("setLayer:"), (void *)root);
    ((msg_v_rect)objc_msgSend)(renderer, sel_registerName("setBounds:"), CGRectMake(0, 0, 256, 256));

    ((msg_id)objc_msgSend)((id)objc_getClass("CATransaction"), sel_registerName("flush"));
    glDrawBuffer(GL_FRONT); glReadBuffer(GL_FRONT);
    glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
    { unsigned char t[4]={0}; glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,t);
      printf("     readback sanity (expect 0,0,255): %d,%d,%d,%d\n",t[0],t[1],t[2],t[3]); }
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    double (*mediaTime)(void) = dlsym(RTLD_DEFAULT, "CACurrentMediaTime");
    ((msg_v_d_p)objc_msgSend)(renderer, sel_registerName("beginFrameAtTime:timeStamp:"),
        mediaTime ? mediaTime() : 0.0, NULL);
    ((msg_v_rect)objc_msgSend)(renderer, sel_registerName("addUpdateRect:"), CGRectMake(0, 0, 256, 256));
    ((msg_id)objc_msgSend)(renderer, sel_registerName("render"));
    ((msg_id)objc_msgSend)(renderer, sel_registerName("endFrame"));
    glFinish();
    printf("ok   beginFrame/render/endFrame, glError=0x%x\n", glGetError());

    unsigned char px[4] = {0};
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("     center pixel RGBA = %d,%d,%d,%d\n", px[0], px[1], px[2], px[3]);
    int ok = (px[1] > 200 && px[0] < 80 && px[2] < 80);
    printf("\n%s: layer actually rasterized green\n", ok ? "PASSED" : "FAILED");
    CGColorRelease(green); CGColorSpaceRelease(cs);
    [pool release];
    return ok ? 0 : 1;
}
