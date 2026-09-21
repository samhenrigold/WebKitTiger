/* TIGER SDK OVERLAY: NSArray.h, from the Mac OS X 10.4u SDK, with Objective-C
   lightweight generics added.
 *
 * The 10.4 classes are not parameterized, so "NSArray<NSString *> *" does not parse,
 * and a category cannot retrofit a type parameter. Only the @interface lines
 * change here: the type parameter is declared on the class and repeated on each
 * of its categories, which is what clang requires. Method signatures keep their
 * original "id" types -- id converts to and from the specialized type, so the
 * parameter does its job (letting the specialization parse and be checked at
 * the use site) without rewriting a 1990s header. */
/*	NSArray.h
	Copyright (c) 1994-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSObject.h>
#import <Foundation/NSRange.h>

@class NSData, NSDictionary, NSEnumerator, NSIndexSet, NSString, NSURL;

/****************	Immutable Array		****************/

@interface NSArray<__covariant ObjectType> : NSObject <NSCopying, NSMutableCopying, NSCoding>

- (unsigned)count;
- (id)objectAtIndex:(unsigned)index;
    
@end

@interface NSArray<ObjectType> (NSExtendedArray)

- (NSArray *)arrayByAddingObject:(id)anObject;
- (NSArray *)arrayByAddingObjectsFromArray:(NSArray *)otherArray;
- (NSString *)componentsJoinedByString:(NSString *)separator;
- (BOOL)containsObject:(id)anObject;
- (NSString *)description;
- (NSString *)descriptionWithLocale:(NSDictionary *)locale;
- (NSString *)descriptionWithLocale:(NSDictionary *)locale indent:(unsigned)level;
- (id)firstObjectCommonWithArray:(NSArray *)otherArray;
- (void)getObjects:(id *)objects;
- (void)getObjects:(id *)objects range:(NSRange)range;
- (unsigned)indexOfObject:(id)anObject;
- (unsigned)indexOfObject:(id)anObject inRange:(NSRange)range;
- (unsigned)indexOfObjectIdenticalTo:(id)anObject;
- (unsigned)indexOfObjectIdenticalTo:(id)anObject inRange:(NSRange)range;
- (BOOL)isEqualToArray:(NSArray *)otherArray;
- (id)lastObject;
- (NSEnumerator *)objectEnumerator;
- (NSEnumerator *)reverseObjectEnumerator;
- (NSData *)sortedArrayHint;
- (NSArray *)sortedArrayUsingFunction:(int (*)(id, id, void *))comparator context:(void *)context;
- (NSArray *)sortedArrayUsingFunction:(int (*)(id, id, void *))comparator context:(void *)context hint:(NSData *)hint;
- (NSArray *)sortedArrayUsingSelector:(SEL)comparator;
- (NSArray *)subarrayWithRange:(NSRange)range;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)atomically;

- (void)makeObjectsPerformSelector:(SEL)aSelector;
- (void)makeObjectsPerformSelector:(SEL)aSelector withObject:(id)argument;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
- (NSArray *)objectsAtIndexes:(NSIndexSet *)indexes;
#endif

@end

@interface NSArray<ObjectType> (NSArrayCreation)

+ (id)array;
+ (id)arrayWithContentsOfFile:(NSString *)path;
+ (id)arrayWithContentsOfURL:(NSURL *)url;
+ (id)arrayWithObject:(id)anObject;
+ (id)arrayWithObjects:(id)firstObj, ...;
- (id)initWithArray:(NSArray *)array;
#if MAC_OS_X_VERSION_10_2 <= MAC_OS_X_VERSION_MAX_ALLOWED
- (id)initWithArray:(NSArray *)array copyItems:(BOOL)flag;
#endif
- (id)initWithContentsOfFile:(NSString *)path;
- (id)initWithContentsOfURL:(NSURL *)url;
- (id)initWithObjects:(id *)objects count:(unsigned)count;
- (id)initWithObjects:(id)firstObj, ...;

+ (id)arrayWithArray:(NSArray *)array;
+ (id)arrayWithObjects:(id *)objs count:(unsigned)cnt;

@end

/****************	Mutable Array		****************/

@interface NSMutableArray<ObjectType> : NSArray<ObjectType>

- (void)addObject:(id)anObject;
- (void)insertObject:(id)anObject atIndex:(unsigned)index;
- (void)removeLastObject;
- (void)removeObjectAtIndex:(unsigned)index;
- (void)replaceObjectAtIndex:(unsigned)index withObject:(id)anObject;

@end

@interface NSMutableArray<ObjectType> (NSExtendedMutableArray)
    
- (void)addObjectsFromArray:(NSArray *)otherArray;
- (void)exchangeObjectAtIndex:(unsigned)idx1 withObjectAtIndex:(unsigned)idx2;
- (void)removeAllObjects;
- (void)removeObject:(id)anObject inRange:(NSRange)range;
- (void)removeObject:(id)anObject;
- (void)removeObjectIdenticalTo:(id)anObject inRange:(NSRange)range;
- (void)removeObjectIdenticalTo:(id)anObject;
- (void)removeObjectsFromIndices:(unsigned *)indices numIndices:(unsigned)count;
- (void)removeObjectsInArray:(NSArray *)otherArray;
- (void)removeObjectsInRange:(NSRange)range;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray *)otherArray range:(NSRange)otherRange;
- (void)replaceObjectsInRange:(NSRange)range withObjectsFromArray:(NSArray *)otherArray;
- (void)setArray:(NSArray *)otherArray;
- (void)sortUsingFunction:(int (*)(id, id, void *))compare context:(void *)context;
- (void)sortUsingSelector:(SEL)comparator;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
- (void)insertObjects:(NSArray *)objects atIndexes:(NSIndexSet *)indexes;
- (void)removeObjectsAtIndexes:(NSIndexSet *)indexes;
- (void)replaceObjectsAtIndexes:(NSIndexSet *)indexes withObjects:(NSArray *)objects;
#endif

@end

@interface NSMutableArray<ObjectType> (NSMutableArrayCreation)

+ (id)arrayWithCapacity:(unsigned)numItems;
- (id)initWithCapacity:(unsigned)numItems;

@end

