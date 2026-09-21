// CASceneTest - drive the CAScene applier with a synthetic WC-shaped delta
// stream and measure it on the box.
//
// Builds a page-like tree (root, a scrolling layer with a 3x16 tile grid, and a
// crowd of composited sublayers), then issues one commit per frame at 60 Hz
// carrying a few hundred property changes and 20 tile updates, the way a real
// DrawingAreaWC commit would. Tile pixels live in an mmap'd file carved into
// fixed slots, standing in for WC's per-tile ShareableBitmap handles.
//
// Build: make -f Makefile CASceneTest.
//
//   CAS_SECONDS=12     how long to measure before printing the summary
//   CAS_MUTATE=1       keep writing into the mapping for one tile WITHOUT
//                      issuing a tile update, to see whether CA copied
//   CAS_QUIET=1        no per-second lines

#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CoreAnimation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mach/mach.h>

#include "CAScene.h"

using namespace tigerca;

@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
@end

// ---------------------------------------------------------------- the tree

#define TILE_PX      256
#define TILE_COLS    3
#define TILE_ROWS    16
#define TILE_COUNT   (TILE_COLS * TILE_ROWS)
#define SLOT_COUNT   64                    // more slots than tiles, so updates rotate
#define PAGE_W       (TILE_COLS * TILE_PX)
#define PAGE_H       (TILE_ROWS * TILE_PX)
#define CONTENT_N    120                   // composited sublayers
#define PROPS_PER_COMMIT 200
static int gTilesPerCommit = 20;   // CAS_TILES overrides
#define TILES_PER_COMMIT gTilesPerCommit

#define ID_ROOT   1
#define ID_SCROLL 2
#define ID_CONTENT_BASE 100

static size_t residentBytes()
{
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count)
        != KERN_SUCCESS)
        return 0;
    return info.resident_size;
}

static uint32_t rnd()
{
    static uint32_t s = 22222;
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

// One mmap'd file carved into fixed-size slots, each one tile's pixels. This is
// the stand-in for a ShareableBitmap handle: the producer would write here, the
// applier maps it once and never copies.
static TileStore gStore;
static int gStoreFD = -1;

static void paintSlot(uint32_t slot, int seed)
{
    unsigned char *p = gStore.base + (size_t)slot * gStore.slotBytes;
    for (uint32_t y = 0; y < gStore.tilePixels; y++) {
        unsigned char *row = p + y * gStore.bytesPerRow;
        for (uint32_t x = 0; x < gStore.tilePixels; x++) {
            int band = ((x + y + seed * 13) / 16) & 3;
            unsigned char r = band == 0 ? 245 : (band == 1 ? 90 : 200);
            unsigned char g = band == 2 ? 200 : (unsigned char)(60 + seed * 7 % 160);
            unsigned char b = band == 3 ? 230 : 110;
            row[x * 4 + 0] = 255;          // kCGImageAlphaNoneSkipFirst, host order
            row[x * 4 + 1] = r;
            row[x * 4 + 2] = g;
            row[x * 4 + 3] = b;
        }
    }
}

static bool openTileStore(const char *path)
{
    gStore.tilePixels = TILE_PX;
    gStore.bytesPerRow = TILE_PX * 4;
    gStore.slotBytes = gStore.bytesPerRow * TILE_PX;
    gStore.slots = SLOT_COUNT;
    size_t total = gStore.slotBytes * gStore.slots;

    gStoreFD = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (gStoreFD < 0 || ftruncate(gStoreFD, total) != 0)
        return false;
    void *m = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, gStoreFD, 0);
    if (m == MAP_FAILED)
        return false;
    gStore.base = (unsigned char *)m;
    for (uint32_t i = 0; i < gStore.slots; i++)
        paintSlot(i, i);
    fprintf(stderr, "tile store: %s, %u slots x %zu bytes = %.1f MB mapped\n",
            path, gStore.slots, gStore.slotBytes, total / 1048576.0);
    return true;
}

// ---- the generator

static void fillIdentity(Matrix &m)
{
    for (int i = 0; i < 16; i++) m.m[i] = (i % 5) ? 0 : 1;
}

static void rotationZ(Matrix &m, double a)
{
    fillIdentity(m);
    m.m[0] = cos(a); m.m[1] = sin(a); m.m[4] = -sin(a); m.m[5] = cos(a);
}

static void rotation3D(Matrix &m, double a)
{
    fillIdentity(m);
    double c = cos(a), s = sin(a);
    m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
    m.m[11] = -1.0 / 900.0;                // perspective, makes it non-affine
}

// Commit 0: the whole tree, the way a first flush would arrive.
static void buildInitialCommit(SceneUpdate &u, float vw, float vh)
{
    u.clear();
    u.viewportW = vw; u.viewportH = vh;
    u.rootLayer = ID_ROOT;

    u.addedLayers.push_back(ID_ROOT);
    u.addedLayers.push_back(ID_SCROLL);
    for (int i = 0; i < CONTENT_N; i++)
        u.addedLayers.push_back(ID_CONTENT_BASE + i);

    LayerUpdate root;
    root.id = ID_ROOT;
    root.changes = ChangeSize | ChangePosition | ChangeAnchorPoint
                 | ChangeMasksToBounds | ChangeSolidColor | ChangeChildren;
    root.sizeW = vw; root.sizeH = vh;
    root.position.x = 0; root.position.y = 0;
    root.anchorPoint.x = 0; root.anchorPoint.y = 0;
    root.masksToBounds = true;
    root.solidColor.r = 0.10f; root.solidColor.g = 0.11f;
    root.solidColor.b = 0.14f; root.solidColor.a = 1;
    root.children.push_back(ID_SCROLL);
    u.changedLayers.push_back(root);

    LayerUpdate scroll;
    scroll.id = ID_SCROLL;
    scroll.changes = ChangeSize | ChangePosition | ChangeAnchorPoint
                   | ChangeBackground | ChangeChildren;
    scroll.sizeW = PAGE_W; scroll.sizeH = PAGE_H;
    scroll.position.x = 0; scroll.position.y = 0;
    scroll.anchorPoint.x = 0; scroll.anchorPoint.y = 0;
    scroll.background.hasBackingStore = true;
    scroll.background.backingStoreW = PAGE_W;
    scroll.background.backingStoreH = PAGE_H;
    scroll.background.color.r = 1; scroll.background.color.g = 0.99f;
    scroll.background.color.b = 0.96f; scroll.background.color.a = 1;
    for (int r = 0; r < TILE_ROWS; r++)
        for (int c = 0; c < TILE_COLS; c++) {
            TileUpdate t;
            t.indexX = c; t.indexY = r; t.willRemove = false;
            t.slot = (r * TILE_COLS + c) % SLOT_COUNT;
            t.dirtyRect.x = 0; t.dirtyRect.y = 0;
            t.dirtyRect.w = TILE_PX; t.dirtyRect.h = TILE_PX;
            scroll.background.tileUpdates.push_back(t);
        }
    for (int i = 0; i < CONTENT_N; i++)
        scroll.children.push_back(ID_CONTENT_BASE + i);
    u.changedLayers.push_back(scroll);

    for (int i = 0; i < CONTENT_N; i++) {
        LayerUpdate c;
        c.id = ID_CONTENT_BASE + i;
        c.changes = ChangeSize | ChangePosition | ChangeAnchorPoint
                  | ChangeSolidColor | ChangeOpacity | ChangeMasksToBounds;
        c.sizeW = 60 + (i % 5) * 20; c.sizeH = 40;
        c.position.x = 30 + (i % 4) * 150;
        c.position.y = 80 + (i / 4) * 130;
        c.anchorPoint.x = 0.5f; c.anchorPoint.y = 0.5f;
        c.solidColor.r = 0.2f + (i % 7) * 0.1f;
        c.solidColor.g = 0.35f; c.solidColor.b = 0.9f - (i % 5) * 0.1f;
        c.solidColor.a = 1;
        c.opacity = 0.85f;
        c.masksToBounds = getenv("CAS_NOMASK") == NULL;
        // Two of them carry a real 3D transform, to exercise the non-affine path.
        if (i == 3 || i == 40) {
            c.changes |= ChangeTransform;
            rotation3D(c.transform, 0.5);
        }
        u.changedLayers.push_back(c);
    }
}

// Every later commit: a scroll step, a pile of property changes, some tiles.
static void buildFrameCommit(SceneUpdate &u, float vw, float vh, double t, int frame)
{
    u.clear();
    u.viewportW = vw; u.viewportH = vh;
    u.rootLayer = ID_ROOT;

    // CAS_STATIC: emit an empty commit, so the only thing that can change on
    // screen is the tile store being written behind CA's back. That is the
    // control for "does CA keep reading the mapped pages".
    if (getenv("CAS_STATIC"))
        return;

    // The scroll layer's bounds origin, as a real scroll would arrive.
    LayerUpdate scroll;
    scroll.id = ID_SCROLL;
    scroll.changes = ChangeBackground;
    scroll.background.hasBackingStore = true;
    scroll.background.backingStoreW = PAGE_W;
    scroll.background.backingStoreH = PAGE_H;
    scroll.background.color.r = 1; scroll.background.color.g = 0.99f;
    scroll.background.color.b = 0.96f; scroll.background.color.a = 1;
    for (int k = 0; k < TILES_PER_COMMIT; k++) {
        int idx = (frame * TILES_PER_COMMIT + k) % TILE_COUNT;
        TileUpdate tu;
        tu.indexX = idx % TILE_COLS; tu.indexY = idx / TILE_COLS;
        tu.willRemove = false;
        tu.slot = (idx + frame) % SLOT_COUNT;
        tu.dirtyRect.x = 0; tu.dirtyRect.y = 0;
        tu.dirtyRect.w = TILE_PX; tu.dirtyRect.h = TILE_PX;
        scroll.background.tileUpdates.push_back(tu);
    }
    u.changedLayers.push_back(scroll);

    // Scrolling arrives as the root's bounds origin. Under geometryFlipped a
    // positive origin.y scrolls DOWN; the unflipped intuition has it backwards
    // and moves the page off screen silently.
    LayerUpdate root;
    root.id = ID_ROOT;
    root.changes = ChangeBoundsOrigin;
    root.boundsOriginX = 0;
    root.boundsOriginY = getenv("CAS_NOSCROLL") ? 0
                       : (float)((int)(t * 220.0) % (PAGE_H - (int)vh));
    u.changedLayers.push_back(root);

    // A few hundred property changes spread over the composited sublayers, the
    // mix a page with animations and scrolling content would produce.
    int per = PROPS_PER_COMMIT / 4;
    for (int k = 0; k < per; k++) {
        LayerUpdate c;
        c.id = ID_CONTENT_BASE + (rnd() % CONTENT_N);
        c.changes = ChangeOpacity;
        c.opacity = 0.35f + (rnd() % 60) / 100.0f;
        u.changedLayers.push_back(c);
    }
    for (int k = 0; k < per; k++) {
        int i = rnd() % CONTENT_N;
        LayerUpdate c;
        c.id = ID_CONTENT_BASE + i;
        c.changes = ChangePosition;
        c.position.x = 30 + (i % 4) * 150 + (float)(8.0 * sin(t * 2 + i));
        c.position.y = 80 + (i / 4) * 130;
        u.changedLayers.push_back(c);
    }
    for (int k = 0; k < per; k++) {
        LayerUpdate c;
        c.id = ID_CONTENT_BASE + (rnd() % CONTENT_N);
        c.changes = ChangeTransform;
        rotationZ(c.transform, getenv("CAS_NOXFORM") ? 0 : sin(t + c.id) * 0.35);
        u.changedLayers.push_back(c);
    }
    for (int k = 0; k < per; k++) {
        LayerUpdate c;
        c.id = ID_CONTENT_BASE + (rnd() % CONTENT_N);
        c.changes = ChangeSolidColor | ChangeContentsVisible;
        c.solidColor.r = 0.2f + (rnd() % 60) / 100.0f;
        c.solidColor.g = 0.35f; c.solidColor.b = 0.8f; c.solidColor.a = 1;
        c.contentsVisible = true;
        u.changedLayers.push_back(c);
    }
}

// ---------------------------------------------------------------- the host

@interface CASceneView : NSOpenGLView
{
    CARenderer *_renderer;
    Scene *_scene;
    NSTimer *_timer;

    CFTimeInterval _started;
    int _frameIndex;   // NSView already has an ivar called _frame
    BOOL _summarised;

    double _applyAccum, _renderAccum;
    unsigned _frames;
    double _applyTotal, _renderTotal;
    unsigned _framesTotal;
    double _applyMax;
    size_t _rssBeforeTiles, _rssAfterTiles;
}
- (void)tick;
@end

@implementation CASceneView

- (BOOL)isOpaque { return YES; }

- (void)prepareOpenGL
{
    [super prepareOpenGL];
    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    GLint swap = 1;
    CGLSetParameter(cgl, kCGLCPSwapInterval, &swap);
    CGLSetCurrentContext(cgl);
    fprintf(stderr, "GL_RENDERER = %s\n", glGetString(GL_RENDERER));

    _renderer = [[CARenderer rendererWithCGLContext:cgl options:nil] retain];
    if (!_renderer) {
        fprintf(stderr, "FATAL: no CARenderer\n");
        exit(1);
    }

    _scene = new Scene();
    _scene->setTileStore(gStore);

    NSRect b = [self bounds];
    SceneUpdate u;
    buildInitialCommit(u, b.size.width, b.size.height);

    _rssBeforeTiles = residentBytes();
    CALayer *root = _scene->apply(u);
    _rssAfterTiles = residentBytes();

    fprintf(stderr, "initial commit: %zu layers + %zu tile layers, %.2f ms, "
                    "%u tile updates, %u property writes\n",
            _scene->layerCount(), _scene->tileLayerCount(),
            _scene->lastApplySeconds * 1000.0, _scene->lastTileUpdates,
            _scene->lastPropertyWrites);
    fprintf(stderr, "rss %.1f MB -> %.1f MB across %d tiles of %zu bytes "
                    "(%.1f MB of pixels). CA %s the mapped pages.\n",
            _rssBeforeTiles / 1048576.0, _rssAfterTiles / 1048576.0, TILE_COUNT,
            gStore.slotBytes, TILE_COUNT * gStore.slotBytes / 1048576.0,
            (_rssAfterTiles - _rssBeforeTiles) > (size_t)(TILE_COUNT * gStore.slotBytes / 2)
                ? "COPIED" : "did not copy");

    {
        CALayer *scroll = _scene->layerForID(ID_SCROLL);
        CALayer *c0 = _scene->layerForID(ID_CONTENT_BASE);
        CGRect rb = [root bounds], sb = [scroll bounds], cf = [c0 frame];
        fprintf(stderr, "root %.0fx%.0f subs=%d | scroll %.0fx%.0f subs=%d |"
                        " content0 (%.0f,%.0f %.0fx%.0f) opacity %.2f hidden=%d"
                        " super=%s\n",
                rb.size.width, rb.size.height, (int)[[root sublayers] count],
                sb.size.width, sb.size.height, (int)[[scroll sublayers] count],
                cf.origin.x, cf.origin.y, cf.size.width, cf.size.height,
                [c0 opacity], (int)[c0 isHidden],
                [[[c0 superlayer] name] UTF8String]);
        NSArray *subs = [scroll sublayers];
        fprintf(stderr, "scroll sublayer[0] = %s, [1] = %s, [120] = %s\n",
                [[[subs objectAtIndex:0] name] UTF8String],
                [[[subs objectAtIndex:1] name] UTF8String],
                [[[subs objectAtIndex:[subs count]-1] name] UTF8String]);
    }
    [root setGeometryFlipped:YES];
    [_renderer setLayer:root];
    [self reshape];

    _started = CACurrentMediaTime();
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60.0 target:self
                                   selector:@selector(tick) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_timer
                                 forMode:(NSString *)kCFRunLoopCommonModes];
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

- (void)tick
{
    NSRect b = [self bounds];
    CFTimeInterval now = CACurrentMediaTime();
    double t = now - _started;

    SceneUpdate u;
    buildFrameCommit(u, b.size.width, b.size.height, t, _frameIndex);
    _scene->apply(u);
    double applyMs = _scene->lastApplySeconds * 1000.0;
    if (applyMs > _applyMax)
        _applyMax = applyMs;

    // Mutating the mapping with no tile update at all: if CA held onto the
    // mapped pages rather than copying them, the screen changes anyway.
    if (getenv("CAS_MUTATE") && (_frameIndex % 30) == 0)
        paintSlot(0, _frameIndex / 30);

    CGLContextObj cgl = (CGLContextObj)[[self openGLContext] CGLContextObj];
    CGLSetCurrentContext(cgl);
    CFTimeInterval r0 = CACurrentMediaTime();
    [CATransaction flush];
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [_renderer beginFrameAtTime:now timeStamp:NULL];
    [_renderer addUpdateRect:CGRectMake(0, 0, b.size.width, b.size.height)];
    [_renderer render];
    [_renderer endFrame];
    CFTimeInterval r1 = CACurrentMediaTime();
    [[self openGLContext] flushBuffer];

    _applyAccum += _scene->lastApplySeconds;
    _renderAccum += r1 - r0;
    _applyTotal += _scene->lastApplySeconds;
    _renderTotal += r1 - r0;
    _frames++; _framesTotal++; _frameIndex++;

    if (_frames >= 60) {
        if (!getenv("CAS_QUIET"))
            fprintf(stderr, "apply %.2f ms | CA render %.2f ms | %zu layers,"
                            " %zu tiles | rss %.1f MB\n",
                    _applyAccum * 1000.0 / _frames, _renderAccum * 1000.0 / _frames,
                    _scene->layerCount(), _scene->tileLayerCount(),
                    residentBytes() / 1048576.0);
        _applyAccum = _renderAccum = 0;
        _frames = 0;
    }


    double window = getenv("CAS_SECONDS") ? atof(getenv("CAS_SECONDS")) : 12.0;
    if (!_summarised && t >= window) {
        _summarised = YES;
        fprintf(stderr,
                "SUMMARY over %u commits: apply %.2f ms avg (%.2f ms worst),"
                " CA render %.2f ms avg, %d property changes + %d tile updates"
                " per commit, %zu layers + %zu tile layers, rss %.1f MB\n",
                _framesTotal, _applyTotal * 1000.0 / _framesTotal, _applyMax,
                _renderTotal * 1000.0 / _framesTotal,
                PROPS_PER_COMMIT, TILES_PER_COMMIT,
                _scene->layerCount(), _scene->tileLayerCount(),
                residentBytes() / 1048576.0);
    }
}

- (void)drawRect:(NSRect)r { [self tick]; }

@end

// ---------------------------------------------------------------- app

@interface CASDelegate : NSObject
@end
@implementation CASDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
@end

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    if (getenv("CAS_TILES"))
        gTilesPerCommit = atoi(getenv("CAS_TILES"));
    if (!openTileStore("/tmp/cascene-tiles.bin")) {
        fprintf(stderr, "FATAL: could not map the tile store\n");
        return 1;
    }

    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menubar = [[[NSMenu alloc] init] autorelease];
    NSMenuItem *appItem = [[[NSMenuItem alloc] init] autorelease];
    [menubar addItem:appItem];
    NSMenu *appMenu = [[[NSMenu alloc] init] autorelease];
    [appMenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [app setMainMenu:menubar];

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated,
        NSOpenGLPFAColorSize, (NSOpenGLPixelFormatAttribute)32,
        NSOpenGLPFADepthSize, (NSOpenGLPixelFormatAttribute)24,
        (NSOpenGLPixelFormatAttribute)0
    };
    NSOpenGLPixelFormat *pf =
        [[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs] autorelease];

    NSRect frame = NSMakeRect(0, 0, 660, 500);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered defer:NO];
    [win setTitle:@"CASceneTest - WC-shaped layer delta applied to CALayers"];
    CASceneView *v = [[[CASceneView alloc] initWithFrame:frame
                                             pixelFormat:pf] autorelease];
    [win setContentView:v];
    [win setFrameOrigin:NSMakePoint(40, 320)];
    [win makeKeyAndOrderFront:nil];
    [win orderFrontRegardless];

    [app setDelegate:[[[CASDelegate alloc] init] autorelease]];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
