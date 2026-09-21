/* ARC entry points for the legacy (fragile) ObjC runtime. Clang is told -fobjc-runtime=macosx-fragile-10.7
 * so it emits direct calls to these; we implement them on retain/release. */
#import <Foundation/Foundation.h>
#import <objc/objc-runtime.h>
#include <pthread.h>
#include "BlocksRuntime/Block.h"
#include "BlocksRuntime/Block_private.h"

id objc_retain(id obj) { return obj ? [obj retain] : nil; }
void objc_release(id obj) { if (obj) [obj release]; }
id objc_autorelease(id obj) { return obj ? [obj autorelease] : nil; }
id objc_retainAutorelease(id obj) { return objc_autorelease(objc_retain(obj)); }
id objc_retainAutoreleaseReturnValue(id obj) { return objc_retainAutorelease(obj); }
id objc_autoreleaseReturnValue(id obj) { return objc_autorelease(obj); }
id objc_retainAutoreleasedReturnValue(id obj) { return objc_retain(obj); }
id objc_unsafeClaimAutoreleasedReturnValue(id obj) { return obj; }
id objc_retainBlock(id b) { return (id)_Block_copy(b); }
void objc_storeStrong(id *location, id obj)
{
    id old = *location;
    *location = objc_retain(obj);
    objc_release(old);
}
void *objc_autoreleasePoolPush(void) { return [[NSAutoreleasePool alloc] init]; }
void objc_autoreleasePoolPop(void *pool) { [(NSAutoreleasePool *)pool drain]; }

/* __weak: side table of (location -> object); cleared from a swizzled -[NSObject dealloc]. */
static pthread_mutex_t weakLock = PTHREAD_MUTEX_INITIALIZER;
static CFMutableDictionaryRef weakTable; /* object -> CFMutableSet of id* locations */

static void clearWeakRefs(id obj)
{
    pthread_mutex_lock(&weakLock);
    if (weakTable) {
        CFMutableSetRef locs = (CFMutableSetRef)CFDictionaryGetValue(weakTable, obj);
        if (locs) {
            CFIndex n = CFSetGetCount(locs); const void **vals = malloc(n * sizeof(void*));
            CFSetGetValues(locs, vals);
            for (CFIndex i = 0; i < n; ++i) *(id *)vals[i] = nil;
            free(vals);
            CFDictionaryRemoveValue(weakTable, obj);
        }
    }
    pthread_mutex_unlock(&weakLock);
}
static void unregisterWeak(id *location, id obj)
{
    if (!obj || !weakTable) return;
    CFMutableSetRef locs = (CFMutableSetRef)CFDictionaryGetValue(weakTable, obj);
    if (locs) { CFSetRemoveValue(locs, location); if (!CFSetGetCount(locs)) CFDictionaryRemoveValue(weakTable, obj); }
}
static void registerWeak(id *location, id obj)
{
    if (!obj) return;
    if (!weakTable) weakTable = CFDictionaryCreateMutable(NULL, 0, NULL, &kCFTypeDictionaryValueCallBacks);
    CFMutableSetRef locs = (CFMutableSetRef)CFDictionaryGetValue(weakTable, obj);
    if (!locs) { locs = CFSetCreateMutable(NULL, 0, NULL); CFDictionarySetValue(weakTable, obj, locs); CFRelease(locs); }
    CFSetAddValue(locs, location);
}
id objc_storeWeak(id *location, id obj)
{
    pthread_mutex_lock(&weakLock);
    unregisterWeak(location, *location);
    *location = obj;
    registerWeak(location, obj);
    pthread_mutex_unlock(&weakLock);
    return obj;
}
id objc_initWeak(id *location, id obj) { *location = nil; return objc_storeWeak(location, obj); }
void objc_destroyWeak(id *location) { objc_storeWeak(location, nil); }
id objc_loadWeakRetained(id *location)
{
    pthread_mutex_lock(&weakLock);
    id obj = [*location retain];
    pthread_mutex_unlock(&weakLock);
    return obj;
}
id objc_loadWeak(id *location) { return objc_autorelease(objc_loadWeakRetained(location)); }
void objc_copyWeak(id *dst, id *src) { id o = objc_loadWeakRetained(src); objc_initWeak(dst, o); objc_release(o); }
void objc_moveWeak(id *dst, id *src) { objc_copyWeak(dst, src); objc_destroyWeak(src); }

/* Hook NSObject's dealloc on the legacy runtime by swapping method_imp. */
static IMP originalDealloc;
extern void objc_removeAssociatedObjects(id); /* objc2compat.m shares this dealloc hook */
static void hookedDealloc(id self, SEL _cmd) { clearWeakRefs(self); objc_removeAssociatedObjects(self); originalDealloc(self, _cmd); }
static void blockRetain(const void *p) { [(id)p retain]; }
static void blockRelease(const void *p) { [(id)p release]; }
/* The Blocks runtime calls destructInstance unconditionally when freeing a heap block; a null here
   is a jump to 0. We have no C++ ivars in blocks, so a no-op is right. */
static void blockDestructInstance(const void *p) { (void)p; }
/* Idempotent and exported: objc2compat.m calls it so that a program using only associated objects
   (and never touching ARC) still drags this object file, and the hook, out of the static archive. */
void _tigerInstallDeallocHook(void)
{
    if (originalDealloc) return;
    Method m = class_getInstanceMethod([NSObject class], @selector(dealloc));
    originalDealloc = m->method_imp;
    m->method_imp = (IMP)hookedDealloc;
}

__attribute__((constructor)) static void tigerARCInit(void)
{
    _tigerInstallDeallocHook();
    _Block_use_RR2(&(Block_callbacks_RR){ sizeof(Block_callbacks_RR), blockRetain, blockRelease, blockDestructInstance });
}
