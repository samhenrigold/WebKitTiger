// CAScene - apply a coordinated-graphics layer-tree delta to real CALayers.
//
// This is the Tiger counterpart of WCScene (Source/WebKit/GPUProcess/graphics/
// wc/WCScene.cpp, 305 lines), which applies a WCUpdateInfo to TextureMapper
// layers. Per logs/render-process-survey.md the 32-bit UI process owns the layer
// tree, so the same delta arrives here and builds real CALayers instead.
//
// The vocabulary below mirrors WCLayerChange / WCLayerUpdateInfo / WCUpdateInfo
// (Source/WebKit/WebProcess/WebPage/wc/WCUpdateInfo.h) field for field where a
// CALayer has a counterpart, and says so where it does not.
//
// Two semantics carried over from WCScene and relied on by the generator:
//   - `children` is a FULL ordered list, never incremental. Every id in it must
//     already exist or appear in `addedLayers` of the same delta.
//   - `removedLayers` is processed LAST, so a layer can be reparented away and
//     destroyed in one delta.

#ifndef CASCENE_H
#define CASCENE_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <unordered_map>

#ifdef __OBJC__
@class CALayer;
#else
typedef struct objc_object CALayer;
#endif

namespace tigerca {

typedef uint32_t LayerID;
static const LayerID kNoLayer = 0;

// Mirrors WCLayerChange. The numbering is WC's declaration order; the entries
// WC has and CA has no counterpart for are kept so the wire format stays the
// same shape, and are listed in kUnsupportedChanges below.
enum LayerChange : uint32_t {
    ChangeChildren            = 1u << 0,
    ChangeMaskLayer           = 1u << 1,
    ChangeReplicaLayer        = 1u << 2,   // CA has no replica; see notes
    ChangePosition            = 1u << 3,
    ChangeAnchorPoint         = 1u << 4,
    ChangeSize                = 1u << 5,
    ChangeBoundsOrigin        = 1u << 6,
    ChangeMasksToBounds       = 1u << 7,
    ChangeContentsRectClipsDescendants = 1u << 8,
    ChangeShowDebugBorder     = 1u << 9,
    ChangeShowRepaintCounter  = 1u << 10,
    ChangeContentsVisible     = 1u << 11,
    ChangeBackfaceVisibility  = 1u << 12,
    ChangePreserves3D         = 1u << 13,
    ChangeSolidColor          = 1u << 14,
    ChangeDebugBorderColor    = 1u << 15,
    ChangeOpacity             = 1u << 16,
    ChangeDebugBorderWidth    = 1u << 17,
    ChangeRepaintCount        = 1u << 18,
    ChangeContentsRect        = 1u << 19,
    ChangeBackground          = 1u << 20,  // the tile updates live here
    ChangeTransform           = 1u << 21,
    ChangeChildrenTransform   = 1u << 22,
    ChangeFilters             = 1u << 23,
    ChangeBackdropFilters     = 1u << 24,
    ChangeBackdropFiltersRect = 1u << 25,
    ChangeContentsClippingRect = 1u << 26,
    ChangePlatformLayer       = 1u << 27,  // WebGL; no GCGL here
    ChangeRemoteFrame         = 1u << 28
};

struct Rect { float x, y, w, h; };
struct Point { float x, y, z; };
struct Color { float r, g, b, a; };
struct Matrix { double m[16]; };            // column major, as CATransform3D

// Mirrors WCTileUpdate. WC carries the pixels as a WCBackingStore holding a
// ShareableBitmap handle; here the equivalent is a slot in one mmap'd region,
// which is the same thing minus the handshake we do not need in-process.
struct TileUpdate {
    int32_t indexX, indexY;
    bool willRemove;
    uint32_t slot;                          // index into the shared tile store
    Rect dirtyRect;
};

// Mirrors WCLayerUpdateInfo::BackgroundChanges.
struct BackgroundChanges {
    Color color;
    bool hasBackingStore;
    float backingStoreW, backingStoreH;
    std::vector<TileUpdate> tileUpdates;
};

// Mirrors WCLayerUpdateInfo. Only the fields whose bit is set are read, exactly
// like WC's [OptionalTupleBit] serialization.
struct LayerUpdate {
    LayerID id;
    uint32_t changes;

    std::vector<LayerID> children;          // full ordered list
    LayerID maskLayer;
    LayerID replicaLayer;

    Point position;
    Point anchorPoint;
    float sizeW, sizeH;
    float boundsOriginX, boundsOriginY;

    bool masksToBounds;
    bool contentsRectClipsDescendants;
    bool contentsVisible;
    bool backfaceVisibility;
    bool preserves3D;

    Color solidColor;
    float opacity;
    Rect contentsRect;

    BackgroundChanges background;

    Matrix transform;
    Matrix childrenTransform;

    LayerUpdate() { reset(); }
    void reset()
    {
        id = kNoLayer; changes = 0; children.clear();
        maskLayer = replicaLayer = kNoLayer;
        position.x = position.y = position.z = 0;
        anchorPoint.x = anchorPoint.y = 0.5f; anchorPoint.z = 0;
        sizeW = sizeH = 0; boundsOriginX = boundsOriginY = 0;
        masksToBounds = contentsRectClipsDescendants = false;
        contentsVisible = true; backfaceVisibility = true; preserves3D = false;
        solidColor.r = solidColor.g = solidColor.b = solidColor.a = 0;
        opacity = 1;
        contentsRect.x = contentsRect.y = 0; contentsRect.w = contentsRect.h = 1;
        background.color = solidColor; background.hasBackingStore = false;
        background.backingStoreW = background.backingStoreH = 0;
        background.tileUpdates.clear();
        for (int i = 0; i < 16; i++) transform.m[i] = childrenTransform.m[i] = (i % 5) ? 0 : 1;
    }
};

// Mirrors WCUpdateInfo. One of these per commit.
struct SceneUpdate {
    float viewportW, viewportH;
    LayerID rootLayer;
    std::vector<LayerID> addedLayers;
    std::vector<LayerID> removedLayers;
    std::vector<LayerUpdate> changedLayers;

    void clear()
    {
        rootLayer = kNoLayer;
        addedLayers.clear(); removedLayers.clear(); changedLayers.clear();
    }
};

// The tile pixels arrive in one mmap'd region carved into fixed slots, standing
// in for WC's per-tile ShareableBitmap handles.
struct TileStore {
    unsigned char *base;
    size_t slotBytes;
    uint32_t slots;
    uint32_t tilePixels;                    // square tiles
    size_t bytesPerRow;
};

class Scene {
public:
    Scene();
    ~Scene();

    void setTileStore(const TileStore &store) { m_store = store; }

    // Applies one commit inside a single CATransaction, the way WCScene::update
    // applies one WCUpdateInfo. Returns the root layer, or null.
    CALayer *apply(const SceneUpdate &update);

    CALayer *layerForID(LayerID layerID) const;
    size_t layerCount() const { return m_layers.size(); }
    size_t tileLayerCount() const { return m_tileLayers.size(); }

    // Counters, for the report.
    double lastApplySeconds;
    unsigned lastTileUpdates;
    unsigned lastPropertyWrites;

private:
    struct TileKey {
        LayerID layer; int32_t x, y;
        bool operator==(const TileKey &o) const
            { return layer == o.layer && x == o.x && y == o.y; }
    };
    struct TileKeyHash {
        size_t operator()(const TileKey &k) const
            { return (size_t)k.layer * 73856093u ^ (size_t)k.x * 19349663u ^ (size_t)k.y * 83492791u; }
    };

    void applyOne(const LayerUpdate &update);
    void applyBackground(LayerID layerID, const BackgroundChanges &bg);

    std::unordered_map<LayerID, CALayer *> m_layers;
    std::unordered_map<LayerID, CALayer *> m_tileContainers;
    std::unordered_map<TileKey, CALayer *, TileKeyHash> m_tileLayers;
    TileStore m_store;
};

} // namespace tigerca

#endif
