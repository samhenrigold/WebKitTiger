// CAScene - the applier. See CAScene.h for the vocabulary.

#include <AvailabilityMacros.h>
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED
#define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER_BUT_DEPRECATED

#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <QuartzCore/CoreAnimation.h>

#include "CAScene.h"

@interface CALayer (TigerCAPrivate)
- (void)setGeometryFlipped:(BOOL)b;
@end

namespace tigerca {

static CGColorSpaceRef deviceRGB()
{
    static CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    return cs;
}

static CGColorRef makeCGColor(const Color &c)
{
    float comps[4] = { c.r, c.g, c.b, c.a };
    return CGColorCreate(deviceRGB(), comps);
}

// Implicit animations have to be off on every layer the applier creates: a
// commit is a state snapshot, not something to interpolate towards, and CA would
// otherwise cross-fade every tile and ease every reparent.
static NSDictionary *noActions()
{
    static NSDictionary *d = [[NSDictionary alloc] initWithObjectsAndKeys:
        [NSNull null], @"contents",  [NSNull null], @"position",
        [NSNull null], @"bounds",    [NSNull null], @"transform",
        [NSNull null], @"opacity",   [NSNull null], @"hidden",
        [NSNull null], @"sublayers", [NSNull null], @"sublayerTransform",
        [NSNull null], @"backgroundColor", [NSNull null], @"contentsRect",
        [NSNull null], @"onOrderIn", [NSNull null], @"onOrderOut", nil];
    return d;
}

// CGFloat is float in 32 bits, so CATransform3D is 16 floats while the delta
// carries 16 doubles, the way WebCore's TransformationMatrix does. memcpy'ing
// one onto the other reads half the matrix as garbage and collapses the layer to
// a degenerate frame of +-FLT_MAX, which renders as nothing at all and looks
// like the layer was never created. Convert element by element.
static CATransform3D toCATransform(const Matrix &m)
{
    CATransform3D t;
    CGFloat *out = &t.m11;
    for (int i = 0; i < 16; i++)
        out[i] = (CGFloat)m.m[i];
    return t;
}

static CALayer *newLayer()
{
    CALayer *l = [[CALayer alloc] init];
    [l setActions:noActions()];
    [l setAnchorPoint:CGPointMake(0, 0)];
    return l;
}

Scene::Scene()
    : lastApplySeconds(0), lastTileUpdates(0), lastPropertyWrites(0)
{
    m_store.base = 0; m_store.slotBytes = 0; m_store.slots = 0;
    m_store.tilePixels = 0; m_store.bytesPerRow = 0;
}

Scene::~Scene()
{
    for (std::unordered_map<LayerID, CALayer *>::iterator it = m_layers.begin();
         it != m_layers.end(); ++it)
        [it->second release];
}

CALayer *Scene::layerForID(LayerID layerID) const
{
    std::unordered_map<LayerID, CALayer *>::const_iterator it = m_layers.find(layerID);
    return it == m_layers.end() ? nil : it->second;
}

// The tile pixels never leave the mapped region: a data provider straight over
// the slot, then a CGImage over that, then -setContents:. Nothing here copies.
// Whether CA copies on its side is the measurement CASceneTest makes.
void Scene::applyBackground(LayerID layerID, const BackgroundChanges &bg)
{
    CALayer *owner = layerForID(layerID);
    if (!owner)
        return;

    CGColorRef c = makeCGColor(bg.color);
    [owner setBackgroundColor:c];
    CGColorRelease(c);

    if (!bg.hasBackingStore || !m_store.base)
        return;

    // CA has no sparse backing store, so WC's tile grid inside one layer becomes
    // a container layer with one child per live tile. That is the same shape
    // spike/CAHost had to build by hand, because CATiledLayer's own tiles never
    // reach a CARenderer-driven tree.
    CALayer *container = nil;
    std::unordered_map<LayerID, CALayer *>::iterator ci = m_tileContainers.find(layerID);
    if (ci == m_tileContainers.end()) {
        container = newLayer();
        [container setName:@"tiles"];
        // The backing store belongs BEHIND the layer's own children. WC keeps it
        // inside the layer so the question never comes up; with a container layer
        // it has to be inserted at the bottom, and re-inserted there whenever a
        // Children change replaces the sublayer list.
        [owner insertSublayer:container atIndex:0];
        [container release];
        m_tileContainers[layerID] = container;
    } else
        container = ci->second;

    const uint32_t px = m_store.tilePixels;
    for (size_t i = 0; i < bg.tileUpdates.size(); i++) {
        const TileUpdate &t = bg.tileUpdates[i];
        TileKey key; key.layer = layerID; key.x = t.indexX; key.y = t.indexY;
        std::unordered_map<TileKey, CALayer *, TileKeyHash>::iterator ti =
            m_tileLayers.find(key);

        if (t.willRemove) {
            if (ti != m_tileLayers.end()) {
                [ti->second removeFromSuperlayer];
                [ti->second release];
                m_tileLayers.erase(ti);
            }
            continue;
        }
        if (t.slot >= m_store.slots)
            continue;

        CALayer *tile;
        if (ti == m_tileLayers.end()) {
            tile = newLayer();
            [tile setBounds:CGRectMake(0, 0, px, px)];
            [tile setPosition:CGPointMake(t.indexX * (float)px, t.indexY * (float)px)];
            [container addSublayer:tile];
            [tile release];
            m_tileLayers[key] = tile;
        } else
            tile = ti->second;

        unsigned char *bytes = m_store.base + (size_t)t.slot * m_store.slotBytes;
        CGDataProviderRef provider =
            CGDataProviderCreateWithData(NULL, bytes, m_store.slotBytes, NULL);
        CGImageRef img = CGImageCreate(px, px, 8, 32, m_store.bytesPerRow,
                                       deviceRGB(),
                                       kCGImageAlphaNoneSkipFirst |
                                       kCGBitmapByteOrder32Host,
                                       provider, NULL, false,
                                       kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        if (img) {
            [tile setContents:(id)(void *)img];
            CGImageRelease(img);
        }
        lastTileUpdates++;
    }
}

void Scene::applyOne(const LayerUpdate &u)
{
    CALayer *layer = layerForID(u.id);
    if (!layer)
        return;
    const uint32_t ch = u.changes;
    unsigned writes = 0;

    // Same order as WCScene::update's property loop.
    if (ch & ChangeChildren) {
        // Full ordered list, wholesale, exactly like setChildren in WCScene.
        // The tile container, which the applier owns rather than the delta, has
        // to survive that, so it is re-appended after.
        NSMutableArray *subs = [NSMutableArray arrayWithCapacity:u.children.size()];
        for (size_t i = 0; i < u.children.size(); i++) {
            CALayer *c = layerForID(u.children[i]);
            if (c)
                [subs addObject:c];
        }
        std::unordered_map<LayerID, CALayer *>::iterator ci = m_tileContainers.find(u.id);
        if (ci != m_tileContainers.end())
            [subs insertObject:ci->second atIndex:0];
        [layer setSublayers:subs];
        writes++;
    }
    if (ch & ChangeMaskLayer) {
        [layer setMask:layerForID(u.maskLayer)];
        writes++;
    }
    // ChangeReplicaLayer: CA has no replica layer. WebCore's own CA port
    // implements replicas by cloning the subtree in GraphicsLayerCA, above this
    // level, so nothing to do here.
    if (ch & ChangePosition) {
        [layer setPosition:CGPointMake(u.position.x, u.position.y)];
        writes++;
    }
    if (ch & ChangeAnchorPoint) {
        [layer setAnchorPoint:CGPointMake(u.anchorPoint.x, u.anchorPoint.y)];
        writes++;
    }
    if (ch & (ChangeSize | ChangeBoundsOrigin)) {
        CGRect b = [layer bounds];
        if (ch & ChangeSize) { b.size.width = u.sizeW; b.size.height = u.sizeH; }
        if (ch & ChangeBoundsOrigin) { b.origin.x = u.boundsOriginX; b.origin.y = u.boundsOriginY; }
        [layer setBounds:b];
        writes++;
    }
    if (ch & ChangePreserves3D) {
        // CA expresses this with a CATransformLayer, which cannot be swapped in
        // after the fact. GraphicsLayerCA picks the layer type up front; the
        // applier would need the type in the create record to do the same.
        writes++;
    }
    if (ch & ChangeContentsRect) {
        [layer setContentsRect:CGRectMake(u.contentsRect.x, u.contentsRect.y,
                                          u.contentsRect.w, u.contentsRect.h)];
        writes++;
    }
    if (ch & ChangeContentsVisible) {
        [layer setHidden:!u.contentsVisible];
        writes++;
    }
    if (ch & ChangeBackfaceVisibility) {
        [layer setDoubleSided:u.backfaceVisibility];
        writes++;
    }
    if (ch & ChangeMasksToBounds) {
        [layer setMasksToBounds:u.masksToBounds];
        writes++;
    }
    if (ch & ChangeBackground) {
        applyBackground(u.id, u.background);
        writes++;
    }
    if (ch & ChangeSolidColor) {
        CGColorRef c = makeCGColor(u.solidColor);
        [layer setBackgroundColor:c];
        CGColorRelease(c);
        writes++;
    }
    if (ch & ChangeOpacity) {
        [layer setOpacity:u.opacity];
        writes++;
    }
    if (ch & ChangeTransform) {
        CATransform3D t = toCATransform(u.transform);
        [layer setTransform:t];
        // Sibling layers carrying a non-affine transform are depth sorted by
        // CA 1.6 rather than painted in sublayer order, so half of any rotation
        // renders behind its own siblings unless the layer carries a zPosition.
        bool affine = u.transform.m[2] == 0 && u.transform.m[6] == 0
                   && u.transform.m[8] == 0 && u.transform.m[9] == 0
                   && u.transform.m[11] == 0 && u.transform.m[14] == 0
                   && u.transform.m[10] == 1;
        if (!affine)
            [layer setZPosition:u.position.z != 0 ? u.position.z : 200];
        writes++;
    }
    if (ch & ChangeChildrenTransform) {
        CATransform3D t = toCATransform(u.childrenTransform);
        [layer setSublayerTransform:t];
        writes++;
    }
    // Filters / backdrop filters / platform layer / remote frame: not in this
    // prototype. Filters map onto CAFilter, which this QuartzCore has.

    lastPropertyWrites += writes;
}

CALayer *Scene::apply(const SceneUpdate &update)
{
    lastTileUpdates = 0;
    lastPropertyWrites = 0;
    CFTimeInterval t0 = CACurrentMediaTime();

    // One CATransaction per commit, actions off. Without this CA would run its
    // own implicit animation for every property the delta touches.
    [CATransaction begin];
    [CATransaction setValue:(id)kCFBooleanTrue forKey:kCATransactionDisableActions];

    // 1. create
    for (size_t i = 0; i < update.addedLayers.size(); i++) {
        LayerID born = update.addedLayers[i];
        if (m_layers.find(born) != m_layers.end())
            continue;
        CALayer *l = newLayer();
        [l setName:[NSString stringWithFormat:@"layer %u", born]];
        m_layers[born] = l;
    }

    // 2. properties
    for (size_t i = 0; i < update.changedLayers.size(); i++)
        applyOne(update.changedLayers[i]);

    // 3. delete, last, so a layer can be reparented away and destroyed in one
    //    commit - the same ordering WCScene relies on.
    for (size_t i = 0; i < update.removedLayers.size(); i++) {
        LayerID dead = update.removedLayers[i];
        std::unordered_map<LayerID, CALayer *>::iterator it = m_layers.find(dead);
        if (it == m_layers.end())
            continue;
        std::unordered_map<LayerID, CALayer *>::iterator ci = m_tileContainers.find(dead);
        if (ci != m_tileContainers.end()) {
            for (std::unordered_map<TileKey, CALayer *, TileKeyHash>::iterator ti =
                     m_tileLayers.begin(); ti != m_tileLayers.end(); ) {
                if (ti->first.layer == dead) {
                    [ti->second release];
                    m_tileLayers.erase(ti++);
                } else
                    ++ti;
            }
            m_tileContainers.erase(ci);
        }
        [it->second removeFromSuperlayer];
        [it->second release];
        m_layers.erase(it);
    }

    [CATransaction commit];
    lastApplySeconds = CACurrentMediaTime() - t0;

    return update.rootLayer ? layerForID(update.rootLayer) : nil;
}

} // namespace tigerca
