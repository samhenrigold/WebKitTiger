/* Can the 32-bit side hand the shared buffer straight to OpenGL? Times glTexSubImage2D from the
 * mapped pointer, then the GL_APPLE_client_storage + GL_APPLE_texture_range path that is supposed
 * to let the driver read our memory instead of copying it. */
#include "common.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdlib.h>

static CGLContextObj makeContext(const char **how)
{
    CGLPixelFormatObj pf = NULL;
    GLint npix = 0;

    /* Preferred: an accelerated pbuffer. Needs a window server connection, which an ssh session
     * does not have, so fall back to the offscreen software renderer. */
    CGLPixelFormatAttribute accel[] = {
        kCGLPFAAccelerated, kCGLPFAPBuffer, kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        (CGLPixelFormatAttribute)0
    };
    if (CGLChoosePixelFormat(accel, &pf, &npix) == kCGLNoError && pf) {
        CGLContextObj ctx = NULL;
        if (CGLCreateContext(pf, NULL, &ctx) == kCGLNoError && ctx) {
            CGLPBufferObj pbuf = NULL;
            if (CGLCreatePBuffer(IPC_FRAME_W, IPC_FRAME_H, GL_TEXTURE_RECTANGLE_EXT, GL_RGBA, 0, &pbuf) == kCGLNoError
                && CGLSetPBuffer(ctx, pbuf, 0, 0, 0) == kCGLNoError
                && CGLSetCurrentContext(ctx) == kCGLNoError) {
                CGLDestroyPixelFormat(pf);
                *how = "accelerated pbuffer";
                return ctx;
            }
            CGLDestroyContext(ctx);
        }
        CGLDestroyPixelFormat(pf);
    }

    CGLPixelFormatAttribute off[] = {
        kCGLPFAOffScreen, kCGLPFAColorSize, (CGLPixelFormatAttribute)32,
        (CGLPixelFormatAttribute)0
    };
    if (CGLChoosePixelFormat(off, &pf, &npix) != kCGLNoError || !pf) return NULL;
    CGLContextObj ctx = NULL;
    if (CGLCreateContext(pf, NULL, &ctx) != kCGLNoError || !ctx) { CGLDestroyPixelFormat(pf); return NULL; }
    CGLDestroyPixelFormat(pf);
    static void *fb;
    if (!fb) fb = malloc(IPC_FRAME_W * IPC_FRAME_H * 4);
    if (CGLSetOffScreen(ctx, IPC_FRAME_W, IPC_FRAME_H, IPC_FRAME_W * 4, fb) != kCGLNoError
        || CGLSetCurrentContext(ctx) != kCGLNoError) { CGLDestroyContext(ctx); return NULL; }
    *how = "offscreen software renderer";
    return ctx;
}

/* Draw a full-screen textured quad. Without an actual draw, GL_APPLE_client_storage reports
 * absurd "upload" rates: it does not copy at specification time, so glFinish after glTexImage2D
 * forces nothing. Consuming the texture is what makes the two paths comparable. */
static void drawQuad(void)
{
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_RECTANGLE_EXT);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);                         glVertex2f(0, 0);
    glTexCoord2f(IPC_FRAME_W, 0);               glVertex2f(IPC_FRAME_W, 0);
    glTexCoord2f(IPC_FRAME_W, IPC_FRAME_H);     glVertex2f(IPC_FRAME_W, IPC_FRAME_H);
    glTexCoord2f(0, IPC_FRAME_H);               glVertex2f(0, IPC_FRAME_H);
    glEnd();
    glDisable(GL_TEXTURE_RECTANGLE_EXT);
}

void glUploadBenchmark(const uint8_t *pixels)
{
    const char *how = "?";
    CGLContextObj ctx = makeContext(&how);
    if (!ctx) { printf("RESULT gl_upload UNAVAILABLE (no CGL context: no window session)\n"); return; }

    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    int hasClientStorage = exts && strstr(exts, "GL_APPLE_client_storage") != NULL;
    int hasTextureRange = exts && strstr(exts, "GL_APPLE_texture_range") != NULL;
    printf("parent32: GL context via %s, renderer=%s\n", how, renderer ? renderer : "?");
    printf("parent32: GL_APPLE_client_storage=%d GL_APPLE_texture_range=%d\n", hasClientStorage, hasTextureRange);

    glViewport(0, 0, IPC_FRAME_W, IPC_FRAME_H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, IPC_FRAME_W, 0, IPC_FRAME_H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    const double mb = (double)IPC_FRAME_BYTES / (1024.0 * 1024.0);
    const int iterations = 60;
    double t0, t1;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_RECTANGLE_EXT, tex);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, IPC_FRAME_W, IPC_FRAME_H, 0,
                 GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    drawQuad();
    glFinish();

    /* Baseline: draw only, so the upload cost is the difference rather than draw plus upload. */
    t0 = ipcNowSeconds();
    for (int i = 0; i < iterations; ++i) { drawQuad(); glFinish(); }
    t1 = ipcNowSeconds();
    double drawOnly = (t1 - t0) * 1000.0 / iterations;
    printf("RESULT gl_draw_only_ms %.2f\n", drawOnly);

    t0 = ipcNowSeconds();
    for (int i = 0; i < iterations; ++i) {
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, 0, 0, IPC_FRAME_W, IPC_FRAME_H,
                        GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        drawQuad();
        glFinish();
    }
    t1 = ipcNowSeconds();
    double sub = (t1 - t0) * 1000.0 / iterations;
    printf("RESULT gl_texsubimage_ms %.2f   upload_only_ms %.2f   upload_mb_s %.1f   (err=0x%lx)\n",
           sub, sub - drawOnly, mb * 1000.0 / (sub - drawOnly), (unsigned long)glGetError());

    if (hasClientStorage) {
        GLuint tex2 = 0;
        glGenTextures(1, &tex2);
        glBindTexture(GL_TEXTURE_RECTANGLE_EXT, tex2);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        if (hasTextureRange) {
            glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_EXT, IPC_FRAME_BYTES, (GLvoid *)pixels);
            glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
        }
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, IPC_FRAME_W, IPC_FRAME_H, 0,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        drawQuad();
        glFinish();

        t0 = ipcNowSeconds();
        for (int i = 0; i < iterations; ++i) {
            glTexImage2D(GL_TEXTURE_RECTANGLE_EXT, 0, GL_RGBA, IPC_FRAME_W, IPC_FRAME_H, 0,
                         GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
            drawQuad();
            glFinish();
        }
        t1 = ipcNowSeconds();
        double cs = (t1 - t0) * 1000.0 / iterations;
        printf("RESULT gl_client_storage_ms %.2f   upload_only_ms %.2f   upload_mb_s %.1f   (err=0x%lx)\n",
               cs, cs - drawOnly, mb * 1000.0 / (cs - drawOnly), (unsigned long)glGetError());
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
        glDeleteTextures(1, &tex2);
    }

    glDeleteTextures(1, &tex);
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(ctx);
}
