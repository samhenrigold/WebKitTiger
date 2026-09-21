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
#import <JavaScriptCore/InitializeThreading.h>
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
#import <unistd.h>
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

// The colours have to be set IN THE CONTEXT'S OWN COLOUR SPACE.
// CGContextSetRGBFillColor means device RGB, and CoreGraphics then converts
// device RGB to the context's sRGB, which moves a saturated colour by as much
// as 57 levels per channel. The display-list side sets sRGB components
// directly, so the direct side has to as well or the comparison is measuring
// the colour-space conversion instead of the replay.
static void setFill(CGContextRef cg, CGColorSpaceRef space, float r, float g, float b)
{
    CGFloat components[4] = { r / 255, g / 255, b / 255, 1 };
    RetainPtr<CGColorRef> color = adoptCF(CGColorCreate(space, components));
    CGContextSetFillColorWithColor(cg, color.get());
}

static void setStroke(CGContextRef cg, CGColorSpaceRef space, float r, float g, float b)
{
    CGFloat components[4] = { r / 255, g / 255, b / 255, 1 };
    RetainPtr<CGColorRef> color = adoptCF(CGColorCreate(space, components));
    CGContextSetStrokeColorWithColor(cg, color.get());
}

static void drawContentDirectly(CGContextRef cg, CGColorSpaceRef space, const Content& c)
{
    setFill(cg, space, 255, 255, 255);
    CGContextFillRect(cg, c.background);

    setFill(cg, space, 32, 96, 220);
    CGContextFillRect(cg, c.blueRect);

    CGContextSaveGState(cg);
    CGContextClipToRect(cg, c.clipRect);
    setFill(cg, space, 240, 160, 20);
    CGContextFillRect(cg, c.clippedFill);
    CGContextRestoreGState(cg);

    setStroke(cg, space, 20, 140, 60);
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

// NEVER -[NSOpenGLContext makeCurrentContext] here. On Tiger that path goes
// back through the view, which calls -openGLContext, which calls
// -prepareOpenGL: the recursion eats the main thread's whole 8 MB stack and
// dies in CGLSetCurrentContext with esp exactly at the stack's bottom page.
// spike/CAHost never uses makeCurrentContext for the same reason; CGL directly
// is the recipe.
- (void)prepareOpenGL
{
    [super prepareOpenGL];
    CGLSetCurrentContext((CGLContextObj)[[self openGLContext] CGLContextObj]);

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
    // The rest of the CAHost recipe: the layer tree is y-up, and the renderer
    // has no bounds until -reshape runs -- without that call the first
    // -drawRect: renders into a zero rect and the window stays the clear
    // colour, which looks exactly like a scene that was never built.
    [root setGeometryFlipped:YES];
    [_renderer setLayer:root];
    [self reshape];
    _built = YES;
}

- (void)reshape
{
    NSRect b = [self bounds];
    CGLSetCurrentContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
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
    NSRect b = [self bounds];
    CGLSetCurrentContext((CGLContextObj)[[self openGLContext] CGLContextObj]);
    [CATransaction flush];
    glClearColor(0.2f, 0.2f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
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
    // The pixel buffer getPixelBuffer hands back is a JSC Uint8ClampedArray, so
    // JSC's allocators have to be up even though this spike runs no script.
    JSC::initialize();

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
    drawContentDirectly(reference.get(), sRGB.get(), content);

    auto span = replayed->bytes();
    Compare cmp = comparePixels(span.data(), referenceBits, rowBytes, kTile, kTile);
    printf("gpureplay: %u differing pixels of %d, worst channel delta %u\n",
        cmp.differingPixels, kTile * kTile, cmp.worstChannelDelta);

    // The readback's own shape, which is the first thing to look at if the
    // comparison goes wrong: a PixelBuffer reporting anything but 256x256 and
    // 262144 bytes means this spike and libWebCore disagree about a struct.
    printf("  replay buffer %dx%d, %zu bytes\n",
        replayed->size().width(), replayed->size().height(), span.size());
    // Four sample points, replay against reference: the background, inside the
    // blue rect, inside the clipped fill, and on the stroke. When the two
    // disagree these say at a glance whether the replay drew nothing, drew in
    // the wrong byte order, or drew upside down.
    static const int samplePoints[4][2] = { { 8, 8 }, { 60, 60 }, { 200, 60 }, { 42, 152 } };
    for (int i = 0; i < 4; ++i) {
        const uint8_t* r = span.data() + (size_t)samplePoints[i][1] * rowBytes + samplePoints[i][0] * 4;
        const uint8_t* d = referenceBits + (size_t)samplePoints[i][1] * rowBytes + samplePoints[i][0] * 4;
        printf("  (%3d,%3d) replay %02x %02x %02x %02x   direct %02x %02x %02x %02x\n",
            samplePoints[i][0], samplePoints[i][1], r[0], r[1], r[2], r[3], d[0], d[1], d[2], d[3]);
    }
    fflush(stdout);

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

    // No -setActivationPolicy:, which is 10.6. The bundle's Info.plist is what
    // makes this a foreground application on 10.4.
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    NSApplication* app = [NSApplication sharedApplication];
    // The rest of the CAHost recipe: a menu bar, orderFrontRegardless and
    // activateIgnoringOtherApps, so the window comes up in front of whatever
    // the console session is showing.
    NSMenu* menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem* appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu* appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];
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
    [window orderFrontRegardless];
    [app activateIgnoringOtherApps:YES];
    printf("  window visible %d, screens %d\n", (int)[window isVisible], (int)[[NSScreen screens] count]);
    fflush(stdout);
    [app setDelegate:[[GPUReplayDelegate alloc] init]];
    [app run];

    [pool release];
    free(referenceBits);
    return status;
}
