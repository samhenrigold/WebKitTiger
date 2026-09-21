// GPUReplay -- the GPU process's job, end to end, in one process.
//
// Build a WebCore display list the way the 64-bit web process would record one,
// replay it through the CoreGraphics backend into an ImageBuffer, check the
// pixels against the same drawing done by hand in CoreGraphics, and hand the
// finished tile to a CALayer through spike/CAHost's scene applier.
//
// The point is the seam, not the picture. If this draws, then:
//   - WebCore's display-list items reach Tiger's real CoreGraphics through
//     GraphicsContextCG with the compat layer underneath,
//   - ImageBufferCGBitmapBackend gives back pixels in the layout the compositor
//     wants, and
//   - tigerca::Scene puts those pixels on screen with no copy.
//
// Run with no arguments for the headless pixel compare (exit 0 on a match, 1
// otherwise, and it prints the worst channel delta either way). Run with -show
// to also open the window and composite the tile.

#import "config.h"

#import <WebCore/Color.h>
#import <WebCore/DisplayList.h>
#import <WebCore/DisplayListItems.h>
#import <WebCore/FloatRect.h>
#import <WebCore/GraphicsContext.h>
#import <WebCore/GraphicsContextCG.h>
#import <WebCore/ImageBuffer.h>
#import <WebCore/ImageBufferCGBitmapBackend.h>
#import <WebCore/IntRect.h>
#import <WebCore/Path.h>
#import <WebCore/ImageBufferBackend.h>
#import <WebCore/ImageBufferParameters.h>
#import <WebCore/PixelBuffer.h>
#import <WebCore/PixelBufferFormat.h>
#import <wtf/MainThread.h>
#import <wtf/RetainPtr.h>

// Same two lines CASceneTest.mm opens with: the Apple TV QuartzCore's headers
// are Leopard's, and its deprecation macro would otherwise fire on 10.4.
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CoreAnimation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>

#import "CAScene.h"

using namespace WebCore;

static const int kTile = 256;

// ---------------------------------------------------------------- the content
//
// One description, used twice: once as display-list items, once as direct
// CoreGraphics. Keeping them next to each other is the whole point -- if they
// drift, the comparison stops meaning anything.

struct Content {
    FloatRect background { 0, 0, kTile, kTile };
    FloatRect blueRect { 24, 24, 120, 80 };
    FloatRect strokedRect { 40, 150, 170, 70 };
    FloatRect clipRect { 150, 20, 90, 110 };
    FloatRect clippedFill { 120, 0, 200, 200 };
    float lineWidth { 6 };
};

static Ref<const DisplayList::DisplayList> recordContent(const Content& c)
{
    Vector<DisplayList::Item> items;

    items.append(DisplayList::SetInlineFillColor { SRGBA<uint8_t> { 255, 255, 255, 255 } });
    items.append(DisplayList::FillRect { c.background, GraphicsContext::RequiresClipToRect::No });

    items.append(DisplayList::SetInlineFillColor { SRGBA<uint8_t> { 32, 96, 220, 255 } });
    items.append(DisplayList::FillRect { c.blueRect, GraphicsContext::RequiresClipToRect::No });

    // A clipped fill, so the replay has to carry clip state and not just paint.
    items.append(DisplayList::Save { });
    items.append(DisplayList::Clip { c.clipRect });
    items.append(DisplayList::SetInlineFillColor { SRGBA<uint8_t> { 240, 160, 20, 255 } });
    items.append(DisplayList::FillRect { c.clippedFill, GraphicsContext::RequiresClipToRect::No });
    items.append(DisplayList::Restore { });

    // A stroke, so line width and stroke colour go through too.
    items.append(DisplayList::SetInlineStroke { c.lineWidth });
    items.append(DisplayList::SetInlineStroke { SRGBA<uint8_t> { 20, 140, 60, 255 } });
    items.append(DisplayList::StrokeRect { c.strokedRect, c.lineWidth });

    return DisplayList::DisplayList::create(WTF::move(items));
}

static void drawContentDirectly(CGContextRef cg, const Content& c)
{
    CGContextSetRGBFillColor(cg, 1, 1, 1, 1);
    CGContextFillRect(cg, c.background);

    CGContextSetRGBFillColor(cg, 32 / 255.0f, 96 / 255.0f, 220 / 255.0f, 1);
    CGContextFillRect(cg, c.blueRect);

    CGContextSaveGState(cg);
    CGContextClipToRect(cg, c.clipRect);
    CGContextSetRGBFillColor(cg, 240 / 255.0f, 160 / 255.0f, 20 / 255.0f, 1);
    CGContextFillRect(cg, c.clippedFill);
    CGContextRestoreGState(cg);

    CGContextSetRGBStrokeColor(cg, 20 / 255.0f, 140 / 255.0f, 60 / 255.0f, 1);
    CGContextSetLineWidth(cg, c.lineWidth);
    CGContextStrokeRect(cg, c.strokedRect);
}

// ------------------------------------------------------------------ the check

struct Compare {
    unsigned differingPixels;
    unsigned worstChannelDelta;
};

static Compare comparePixels(const uint8_t* a, const uint8_t* b, size_t rowBytes, int w, int h)
{
    Compare result { 0, 0 };
    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + (size_t)y * rowBytes;
        const uint8_t* rb = b + (size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            unsigned worst = 0;
            for (int ch = 0; ch < 4; ++ch) {
                int d = (int)ra[x * 4 + ch] - (int)rb[x * 4 + ch];
                if (d < 0)
                    d = -d;
                if ((unsigned)d > worst)
                    worst = (unsigned)d;
            }
            if (worst) {
                ++result.differingPixels;
                if (worst > result.worstChannelDelta)
                    result.worstChannelDelta = worst;
            }
        }
    }
    return result;
}

// ------------------------------------------------------------------- the host
//
// A one-tile version of CASceneTest: the same tigerca::Scene applier, the same
// CARenderer in an NSOpenGLView, with the tile store pointing at the pixels the
// replay produced instead of at a synthetic pattern.

@interface GPUReplayView : NSOpenGLView {
    CARenderer* _renderer;
    tigerca::Scene* _scene;
    tigerca::TileStore _store;
    BOOL _built;
}
- (void)setTileStore:(tigerca::TileStore)store;
@end

@implementation GPUReplayView

- (BOOL)isOpaque { return YES; }
- (void)setTileStore:(tigerca::TileStore)store { _store = store; }

- (void)prepareOpenGL
{
    [super prepareOpenGL];
    [[self openGLContext] makeCurrentContext];

    _renderer = [[CARenderer rendererWithCGLContext:(CGLContextObj)[[self openGLContext] CGLContextObj]
                                            options:nil] retain];
    _scene = new tigerca::Scene();
    _scene->setTileStore(_store);

    tigerca::SceneUpdate update;
    update.clear();
    update.viewportW = kTile;
    update.viewportH = kTile;
    update.rootLayer = 1;
    update.addedLayers.push_back(1);

    tigerca::LayerUpdate layer;
    layer.reset();
    layer.id = 1;
    layer.changes = tigerca::ChangePosition | tigerca::ChangeAnchorPoint | tigerca::ChangeSize
        | tigerca::ChangeBackground;
    layer.position.x = 0;
    layer.position.y = 0;
    layer.anchorPoint.x = 0;
    layer.anchorPoint.y = 0;
    layer.sizeW = kTile;
    layer.sizeH = kTile;
    layer.background.hasBackingStore = true;
    layer.background.backingStoreW = kTile;
    layer.background.backingStoreH = kTile;

    tigerca::TileUpdate tile;
    tile.indexX = 0;
    tile.indexY = 0;
    tile.willRemove = false;
    tile.slot = 0;
    tile.dirtyRect.x = 0;
    tile.dirtyRect.y = 0;
    tile.dirtyRect.w = kTile;
    tile.dirtyRect.h = kTile;
    layer.background.tileUpdates.push_back(tile);

    update.changedLayers.push_back(layer);

    CALayer* root = _scene->apply(update);
    [_renderer setLayer:root];
    _built = YES;
}

- (void)reshape
{
    NSRect b = [self bounds];
    [[self openGLContext] makeCurrentContext];
    glViewport(0, 0, (GLsizei)b.size.width, (GLsizei)b.size.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, b.size.width, 0, b.size.height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    [_renderer setBounds:CGRectMake(0, 0, b.size.width, b.size.height)];
}

- (void)drawRect:(NSRect)r
{
    if (!_built)
        return;
    [[self openGLContext] makeCurrentContext];
    glClearColor(0.2f, 0.2f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
    [_renderer addUpdateRect:[_renderer bounds]];
    [_renderer render];
    [_renderer endFrame];
    [[self openGLContext] flushBuffer];
}

@end

@interface GPUReplayDelegate : NSObject
@end
@implementation GPUReplayDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { (void)a; return YES; }
@end

// -------------------------------------------------------------------- driver

int main(int argc, const char** argv)
{
    bool show = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-show"))
            show = true;
    }

    WTF::initializeMainThread();

    Content content;

    // 1. Replay the display list through the CoreGraphics backend.
    ImageBufferParameters parameters {
        FloatSize { kTile, kTile },
        1,
        ColorSpace::SRGB(),
        ImageBufferFormat { PixelFormat::BGRA8 },
        RenderingPurpose::Unspecified
    };
    ImageBufferCreationContext creationContext;
    auto backend = ImageBufferCGBitmapBackend::create(parameters, creationContext);
    if (!backend) {
        fprintf(stderr, "gpureplay: ImageBufferCGBitmapBackend::create failed\n");
        return 1;
    }
    auto buffer = ImageBuffer::create(parameters, creationContext, WTF::move(backend));
    if (!buffer) {
        fprintf(stderr, "gpureplay: ImageBuffer::create failed\n");
        return 1;
    }

    auto displayList = recordContent(content);
    buffer->context().drawDisplayList(displayList.get());
    buffer->flushDrawingContext();

    PixelBufferFormat format { AlphaPremultiplication::Premultiplied, PixelFormat::BGRA8, ColorSpace::SRGB() };
    auto replayed = buffer->getPixelBuffer(format, IntRect { 0, 0, kTile, kTile });
    if (!replayed) {
        fprintf(stderr, "gpureplay: getPixelBuffer failed\n");
        return 1;
    }

    // 2. The same drawing, by hand, into a plain CGBitmapContext.
    size_t rowBytes = (size_t)kTile * 4;
    uint8_t* referenceBits = (uint8_t*)calloc(rowBytes * kTile, 1);
    RetainPtr<CGColorSpaceRef> sRGB = adoptCF(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    RetainPtr<CGContextRef> reference = adoptCF(CGBitmapContextCreate(referenceBits, kTile, kTile, 8, rowBytes,
        sRGB.get(), kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little));
    if (!reference) {
        fprintf(stderr, "gpureplay: reference CGBitmapContext failed\n");
        return 1;
    }
    // ImageBuffer's context has the y-flip baked into its base transform; the
    // reference has to be given the same one or every rect lands mirrored.
    CGContextTranslateCTM(reference.get(), 0, kTile);
    CGContextScaleCTM(reference.get(), 1, -1);
    drawContentDirectly(reference.get(), content);

    auto span = replayed->bytes();
    Compare cmp = comparePixels(span.data(), referenceBits, rowBytes, kTile, kTile);
    printf("gpureplay: %u differing pixels of %d, worst channel delta %u\n",
        cmp.differingPixels, kTile * kTile, cmp.worstChannelDelta);

    int status = cmp.differingPixels ? 1 : 0;

    if (!show) {
        free(referenceBits);
        return status;
    }

    // 3. Hand the replayed pixels to a CALayer through the scene applier.
    tigerca::TileStore store;
    store.base = (unsigned char*)malloc(rowBytes * kTile);
    store.slotBytes = rowBytes * kTile;
    store.slots = 1;
    store.tilePixels = kTile;
    store.bytesPerRow = rowBytes;
    memcpy(store.base, span.data(), rowBytes * kTile);

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:0];
    NSRect frame = NSMakeRect(200, 200, kTile, kTile);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSTitledWindowMask | NSClosableWindowMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:@"gpureplay"];
    NSOpenGLPixelFormatAttribute attributes[] = { NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, (NSOpenGLPixelFormatAttribute)0 };
    NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
    GPUReplayView* view = [[GPUReplayView alloc] initWithFrame:frame pixelFormat:pixelFormat];
    [view setTileStore:store];
    [window setContentView:view];
    [window makeKeyAndOrderFront:nil];
    [NSApp setDelegate:[[GPUReplayDelegate alloc] init]];
    [NSApp run];

    free(referenceBits);
    return status;
}
