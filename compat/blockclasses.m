/* Tiger's Foundation predates blocks, so nothing turns the _NSConcrete*Block placeholder arrays in
 * BlocksRuntime/data.c into real ObjC classes. We do it here (the trick PLBlocks used on 10.5):
 * write a compiled class struct over each placeholder so a block's isa is a usable Class and
 * Foundation can -retain/-copy/-release blocks like any other object. */
#import <Foundation/Foundation.h>
#import <objc/objc-class.h>
#include <string.h>
#include <limits.h>
#include "Block.h"
#include "Block_private.h"

/* These replace BlocksRuntime/data.c: keeping the placeholders in the same object file as the
 * classes means any program that uses a block drags this member (and its constructor) out of the
 * static archive. In data.c's own object file the constructor would never be linked in. */
BLOCK_ABI void *_NSConcreteStackBlock[32] = { 0 };
BLOCK_ABI void *_NSConcreteGlobalBlock[32] = { 0 };
BLOCK_ABI void *_NSConcreteMallocBlock[32] = { 0 };
BLOCK_ABI void *_NSConcreteAutoBlock[32] = { 0 };
BLOCK_ABI void *_NSConcreteFinalizingBlock[32] = { 0 };
BLOCK_ABI void *_NSConcreteWeakBlockVariable[32] = { 0 };

/* Common behaviour. _Block_copy/_Block_release already branch on the block's flags, so one
 * implementation is nearly enough; only -retain differs per kind. */
@interface NSBlock : NSObject
@end
@implementation NSBlock
- (id)copy { return (id)_Block_copy(self); }
- (id)copyWithZone:(NSZone *)zone { (void)zone; return (id)_Block_copy(self); }
- (id)mutableCopyWithZone:(NSZone *)zone { (void)zone; return (id)_Block_copy(self); }
- (id)retain { return (id)_Block_copy(self); }
- (oneway void)release { _Block_release(self); }
- (unsigned)retainCount
{
    int32_t flags = ((struct Block_layout *)self)->flags;
    return (flags & BLOCK_NEEDS_FREE) ? (unsigned)((flags & BLOCK_REFCOUNT_MASK) >> 1) : 1u;
}
@end

@interface __NSGlobalBlock__ : NSBlock
@end
@implementation __NSGlobalBlock__
- (id)retain { return self; }
- (id)copyWithZone:(NSZone *)zone { (void)zone; return self; }
- (unsigned)retainCount { return UINT_MAX; }
@end

/* Foundation's -retain on a stack block returns self rather than copying; match it so
 * retain/release stay balanced. ARC copies via objc_retainBlock before anything can retain. */
@interface __NSStackBlock__ : NSBlock
@end
@implementation __NSStackBlock__
- (id)retain { return self; }
@end

@interface __NSMallocBlock__ : NSBlock
@end
@implementation __NSMallocBlock__
@end

__attribute__((constructor)) static void tigerInstallBlockClasses(void)
{
    memcpy(_NSConcreteStackBlock, [__NSStackBlock__ class], sizeof(struct objc_class));
    memcpy(_NSConcreteGlobalBlock, [__NSGlobalBlock__ class], sizeof(struct objc_class));
    memcpy(_NSConcreteMallocBlock, [__NSMallocBlock__ class], sizeof(struct objc_class));
}
