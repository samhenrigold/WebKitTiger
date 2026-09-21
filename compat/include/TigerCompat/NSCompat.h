/* TigerCompat/NSCompat.h -- Foundation API that Mac OS X 10.4 lacks.
 *
 * Include AFTER <Foundation/Foundation.h>. Safe from ObjC++ (C++23) TUs and from
 * both MRR and ARC translation units.
 *
 * Implemented in compat/nscompat.m, linked from libtigercompat.a.
 * Survey and rationale: compat/NSCOMPAT-SURVEY.md
 */

#ifndef TIGERCOMPAT_NSCOMPAT_H
#define TIGERCOMPAT_NSCOMPAT_H

#import <Foundation/Foundation.h>

#ifdef __cplusplus
#define TIGER_NS_EXTERN extern "C"
#else
#define TIGER_NS_EXTERN extern
#endif

/* -------------------------------------------------------------------------
 * 10.5 scalar types. The 10.4u SDK has none of these.
 *
 * These belong in the SDK overlay, not here: a TU that uses NSInteger or for-in
 * without including this header still needs them. Kept here too so that
 * libtigercompat builds standalone, under Apple's own guard macros so the
 * overlay and this header can coexist.
 * ------------------------------------------------------------------------- */
#if !defined(NSINTEGER_DEFINED)
typedef int NSInteger;              /* ILP32 only; this whole tree is i386. */
typedef unsigned int NSUInteger;
#define NSIntegerMax    2147483647
#define NSIntegerMin    (-NSIntegerMax - 1)
#define NSUIntegerMax   4294967295U
#define NSINTEGER_DEFINED 1
#endif

/* __kindof, 10.11. A refinement of `id` in the type system and nothing more;
 * 17 uses in WebKit. clang only accepts the keyword when it has the feature. */
#if !defined(__kindof) && !__has_feature(objc_kindof)
#define __kindof
#endif

/* -------------------------------------------------------------------------
 * Fast enumeration (10.5). Required by `for (x in collection)`.
 * ------------------------------------------------------------------------- */
#if !defined(TIGER_NSFASTENUMERATION_DEFINED)
#define TIGER_NSFASTENUMERATION_DEFINED 1
typedef struct {
    unsigned long state;
    __unsafe_unretained id *itemsPtr;
    unsigned long *mutationsPtr;
    unsigned long extra[5];
} NSFastEnumerationState;

@protocol NSFastEnumeration
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len;
@end
#endif

/* Conformance has to be declared on the classes themselves or clang rejects the
 * for-in loop before it ever looks for the method. */
@interface NSArray (TigerFastEnumeration) <NSFastEnumeration>
@end
@interface NSSet (TigerFastEnumeration) <NSFastEnumeration>
@end
@interface NSDictionary (TigerFastEnumeration) <NSFastEnumeration>
@end
@interface NSEnumerator (TigerFastEnumeration) <NSFastEnumeration>
@end

/* -------------------------------------------------------------------------
 * Block-typed API. Only declared when the TU was compiled with blocks; a plain
 * C++ TU without -fblocks can still include this header.
 * ------------------------------------------------------------------------- */
#if __has_extension(blocks)
#define TIGER_NS_BLOCKS 1
#endif

#if TIGER_NS_BLOCKS
typedef NSComparisonResult (^NSComparator)(id obj1, id obj2);
#endif

#if !defined(TIGER_NSSTRINGCOMPAREOPTIONS_DEFINED)
#define TIGER_NSSTRINGCOMPAREOPTIONS_DEFINED 1
typedef NSUInteger NSStringCompareOptions;   /* 10.5; Tiger spells it `unsigned`. */
#endif

typedef NSUInteger NSEnumerationOptions;
enum { NSEnumerationConcurrent = 1UL << 0, NSEnumerationReverse = 1UL << 1 };

/* -------------------------------------------------------------------------
 * Subscripting (10.8 syntax, 10.6 methods).
 * ------------------------------------------------------------------------- */
@interface NSArray (TigerSubscripting)
- (id)objectAtIndexedSubscript:(NSUInteger)index;
@end

@interface NSMutableArray (TigerSubscripting)
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index;
@end

@interface NSDictionary (TigerSubscripting)
- (id)objectForKeyedSubscript:(id)key;
@end

@interface NSMutableDictionary (TigerSubscripting)
- (void)setObject:(id)object forKeyedSubscript:(id)key;
@end

/* ------------------------------------------------------------------------- */
@interface NSArray (TigerCompat)
- (id)firstObject;
#if TIGER_NS_BLOCKS
- (void)enumerateObjectsUsingBlock:(void (^)(id obj, NSUInteger idx, BOOL *stop))block;
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)opts
                         usingBlock:(void (^)(id obj, NSUInteger idx, BOOL *stop))block;
- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(id obj, NSUInteger idx, BOOL *stop))predicate;
- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(id obj, NSUInteger idx, BOOL *stop))predicate;
- (NSArray *)sortedArrayUsingComparator:(NSComparator)cmptr;
#endif
@end

#if TIGER_NS_BLOCKS
@interface NSMutableArray (TigerCompat)
- (void)sortUsingComparator:(NSComparator)cmptr;
@end

@interface NSDictionary (TigerCompatBlocks)
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(id key, id obj, BOOL *stop))block;
@end

@interface NSSet (TigerCompatBlocks)
- (void)enumerateObjectsUsingBlock:(void (^)(id obj, BOOL *stop))block;
@end

@interface NSIndexSet (TigerCompat)
- (void)enumerateIndexesUsingBlock:(void (^)(NSUInteger idx, BOOL *stop))block;
@end
#endif

/* ------------------------------------------------------------------------- */
@interface NSString (TigerCompat)
- (BOOL)containsString:(NSString *)str;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement
                                           options:(NSStringCompareOptions)options
                                             range:(NSRange)searchRange;
- (NSRange)rangeOfString:(NSString *)str
                 options:(NSStringCompareOptions)options
                   range:(NSRange)range
                  locale:(NSLocale *)locale;
@end

/* ------------------------------------------------------------------------- */
#if TIGER_NS_BLOCKS
@interface NSData (TigerCompatBlocks)
/* 10.9 */
- (id)initWithBytesNoCopy:(void *)bytes
                   length:(NSUInteger)length
              deallocator:(void (^)(void *bytes, NSUInteger length))deallocator;
@end
#endif

typedef NSUInteger NSDataBase64EncodingOptions;
enum {
    NSDataBase64Encoding64CharacterLineLength = 1UL << 0,
    NSDataBase64Encoding76CharacterLineLength = 1UL << 1,
    NSDataBase64EncodingEndLineWithCarriageReturn = 1UL << 4,
    NSDataBase64EncodingEndLineWithLineFeed = 1UL << 5
};

@interface NSData (TigerCompat)
/* 10.9. Tiger's Foundation has no base64 of any kind. */
- (NSString *)base64EncodedStringWithOptions:(NSDataBase64EncodingOptions)options;
@end

/* NSFileWriteFileExistsError is 10.5. Same value as the real one. */
#ifndef NSFileWriteFileExistsError
#define NSFileWriteFileExistsError 516
#endif

/* ------------------------------------------------------------------------- */
@interface NSNumber (TigerCompat)
+ (NSNumber *)numberWithInteger:(NSInteger)value;
+ (NSNumber *)numberWithUnsignedInteger:(NSUInteger)value;
- (NSInteger)integerValue;
- (NSUInteger)unsignedIntegerValue;
@end

/* ------------------------------------------------------------------------- */
@interface NSThread (TigerCompat)
+ (BOOL)isMainThread;
+ (NSThread *)mainThread;
- (BOOL)isMainThread;
@end

/* ------------------------------------------------------------------------- */
typedef struct {
    NSInteger majorVersion;
    NSInteger minorVersion;
    NSInteger patchVersion;
} NSOperatingSystemVersion;

@interface NSProcessInfo (TigerCompat)
- (NSOperatingSystemVersion)operatingSystemVersion;
- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version;
- (NSUInteger)processorCount;
- (NSUInteger)activeProcessorCount;
- (unsigned long long)physicalMemory;
@end

/* ------------------------------------------------------------------------- */
@interface NSLocale (TigerCompat)
+ (NSArray *)preferredLanguages;
+ (NSLocale *)localeWithLocaleIdentifier:(NSString *)ident;
/* 10.12 */
- (NSString *)languageCode;
- (NSString *)scriptCode;
- (NSString *)countryCode;
@end

/* ------------------------------------------------------------------------- */
@interface NSFileManager (TigerCompat)
- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error;
- (NSDictionary *)attributesOfItemAtPath:(NSString *)path error:(NSError **)error;
- (BOOL)setAttributes:(NSDictionary *)attributes ofItemAtPath:(NSString *)path error:(NSError **)error;
- (BOOL)createDirectoryAtPath:(NSString *)path
  withIntermediateDirectories:(BOOL)createIntermediates
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error;
- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error;
- (BOOL)removeItemAtURL:(NSURL *)url error:(NSError **)error;
- (BOOL)moveItemAtPath:(NSString *)src toPath:(NSString *)dst error:(NSError **)error;
- (BOOL)copyItemAtPath:(NSString *)src toPath:(NSString *)dst error:(NSError **)error;
- (BOOL)createDirectoryAtURL:(NSURL *)url
 withIntermediateDirectories:(BOOL)createIntermediates
                  attributes:(NSDictionary *)attributes
                       error:(NSError **)error;
@end

/* ------------------------------------------------------------------------- */
@interface NSURL (TigerCompat)
+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDir;
+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDir relativeToURL:(NSURL *)base;
- (NSURL *)URLByAppendingPathComponent:(NSString *)component;
- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)isDir;
- (NSURL *)URLByAppendingPathExtension:(NSString *)ext;
- (NSURL *)URLByDeletingLastPathComponent;
- (NSURL *)URLByDeletingPathExtension;
- (NSURL *)URLByStandardizingPath;
- (NSURL *)URLByResolvingSymlinksInPath;
/* Tiger has no resource-value store. The getter reports "not available" and the
 * setter succeeds without doing anything; the only caller in WebKit is the
 * NSURLIsExcludedFromBackupKey write, which is meaningless here. */
- (BOOL)getResourceValue:(id *)value forKey:(NSString *)key error:(NSError **)error;
- (BOOL)setResourceValue:(id)value forKey:(NSString *)key error:(NSError **)error;
@end

TIGER_NS_EXTERN NSString *const NSURLIsExcludedFromBackupKey;

/* -------------------------------------------------------------------------
 * NSUUID (10.8), over uuid_t.
 * ------------------------------------------------------------------------- */
@interface NSUUID : NSObject <NSCopying> {
@private
    unsigned char _uuid[16];
}
+ (id)UUID;
- (id)init;
- (id)initWithUUIDString:(NSString *)string;
- (id)initWithUUIDBytes:(const unsigned char *)bytes;
- (void)getUUIDBytes:(unsigned char *)uuid;
- (NSString *)UUIDString;
@end

/* -------------------------------------------------------------------------
 * NSMapTable (10.5), over Tiger's NSCreateMapTable C API.
 *
 * Weak memory is not available on the fragile runtime, so every "weak" option
 * degrades to non-retained. Entries therefore outlive their keys instead of
 * being zeroed out: a JSC wrapper cache keyed on a dead object keeps the value
 * alive until the entry is removed explicitly.
 * ------------------------------------------------------------------------- */
typedef NSUInteger NSPointerFunctionsOptions;
enum {
    NSPointerFunctionsStrongMemory       = 0,
    NSPointerFunctionsZeroingWeakMemory  = 1 << 0,
    NSPointerFunctionsOpaqueMemory       = 2,
    NSPointerFunctionsMallocMemory       = 3,
    NSPointerFunctionsMachVirtualMemory  = 4,
    NSPointerFunctionsWeakMemory         = 5,

    NSPointerFunctionsObjectPersonality      = 0 << 8,
    NSPointerFunctionsOpaquePersonality      = 1 << 8,
    NSPointerFunctionsObjectPointerPersonality = 2 << 8,
    NSPointerFunctionsCStringPersonality     = 3 << 8,
    NSPointerFunctionsStructPersonality      = 4 << 8,
    NSPointerFunctionsIntegerPersonality     = 5 << 8,

    NSPointerFunctionsCopyIn              = 1 << 16
};

/* Tiger's <Foundation/NSMapTable.h> does `typedef struct _NSMapTable NSMapTable;`,
 * which collides with the class name. The class below is compiled into
 * libtigercompat unconditionally (compat/nscompat-maptable.m renames the typedef
 * locally), but it can only be *declared* here once the SDK overlay has done the
 * same rename for everybody. Overlay contract: rename the C typedef to
 * NSMapTableCStruct and define TIGER_NSMAPTABLE_TYPEDEF_RENAMED. */
typedef NSPointerFunctionsOptions NSMapTableOptions;
enum {
    NSMapTableStrongMemory             = NSPointerFunctionsStrongMemory,
    NSMapTableZeroingWeakMemory        = NSPointerFunctionsZeroingWeakMemory,
    NSMapTableCopyIn                   = NSPointerFunctionsCopyIn,
    NSMapTableObjectPointerPersonality = NSPointerFunctionsObjectPointerPersonality,
    NSMapTableWeakMemory               = NSPointerFunctionsWeakMemory
};

#if defined(TIGER_NSMAPTABLE_TYPEDEF_RENAMED)
@interface NSMapTable : NSObject <NSFastEnumeration> {
@private
    void *_table;               /* NSMapTable (the Tiger C struct) */
    NSUInteger _keyOptions;
    NSUInteger _valueOptions;
}
+ (id)strongToStrongObjectsMapTable;
+ (id)weakToStrongObjectsMapTable;
+ (id)strongToWeakObjectsMapTable;
+ (id)weakToWeakObjectsMapTable;
- (id)initWithKeyOptions:(NSPointerFunctionsOptions)keyOptions
            valueOptions:(NSPointerFunctionsOptions)valueOptions
                capacity:(NSUInteger)initialCapacity;
- (id)objectForKey:(id)key;
- (void)setObject:(id)object forKey:(id)key;
- (void)removeObjectForKey:(id)key;
- (void)removeAllObjects;
- (NSUInteger)count;
- (NSEnumerator *)keyEnumerator;
- (NSEnumerator *)objectEnumerator;
@end
#endif /* TIGER_NSMAPTABLE_TYPEDEF_RENAMED */

/* -------------------------------------------------------------------------
 * NSOperationQueue / NSOperation / NSBlockOperation (10.5), over
 * libtigerdispatch. Just enough for WebCore's async ResourceHandle delegate:
 * serial or concurrent block execution plus a drain barrier. No dependencies,
 * no KVO, no priorities, no cancellation of an already-started operation.
 * ------------------------------------------------------------------------- */
@interface NSOperation : NSObject {
@private
    BOOL _cancelled;
    BOOL _finished;
    BOOL _executing;
}
- (void)start;
- (void)main;
- (void)cancel;
- (BOOL)isCancelled;
- (BOOL)isFinished;
- (BOOL)isExecuting;
- (void)waitUntilFinished;
@end

#if TIGER_NS_BLOCKS
@interface NSBlockOperation : NSOperation {
@private
    NSMutableArray *_blocks;
}
+ (id)blockOperationWithBlock:(void (^)(void))block;
- (void)addExecutionBlock:(void (^)(void))block;
@end
#endif

@interface NSOperationQueue : NSObject {
@private
    void *_queue;               /* dispatch_queue_t */
    void *_group;               /* dispatch_group_t */
    NSInteger _maxConcurrent;
    NSString *_name;
    BOOL _suspended;
}
+ (NSOperationQueue *)mainQueue;
+ (NSOperationQueue *)currentQueue;
- (void)addOperation:(NSOperation *)op;
#if TIGER_NS_BLOCKS
- (void)addOperationWithBlock:(void (^)(void))block;
#endif
- (void)setMaxConcurrentOperationCount:(NSInteger)count;
- (NSInteger)maxConcurrentOperationCount;
- (void)setName:(NSString *)name;
- (NSString *)name;
- (void)setSuspended:(BOOL)suspended;
- (BOOL)isSuspended;
- (void)cancelAllOperations;
- (void)waitUntilAllOperationsAreFinished;
- (NSUInteger)operationCount;
@end

#endif /* TIGERCOMPAT_NSCOMPAT_H */
