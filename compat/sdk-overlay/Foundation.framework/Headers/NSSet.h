/* TIGER SDK OVERLAY: NSSet.h, from the Mac OS X 10.4u SDK, with Objective-C
   lightweight generics added.
 *
 * The 10.4 classes are not parameterized, so "NSSet<NSString *> *" does not parse,
 * and a category cannot retrofit a type parameter. Only the @interface lines
 * change here: the type parameter is declared on the class and repeated on each
 * of its categories, which is what clang requires. Method signatures keep their
 * original "id" types -- id converts to and from the specialized type, so the
 * parameter does its job (letting the specialization parse and be checked at
 * the use site) without rewriting a 1990s header. */
/*	NSSet.h
	Copyright (c) 1994-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSObject.h>

@class NSArray, NSDictionary, NSEnumerator, NSString;

/****************	Immutable Set	****************/

@interface NSSet<__covariant ObjectType> : NSObject <NSCopying, NSMutableCopying, NSCoding>

- (unsigned)count;
- (id)member:(id)object;
- (NSEnumerator *)objectEnumerator;

@end

@interface NSSet<ObjectType> (NSExtendedSet)

- (NSArray *)allObjects;
- (id)anyObject;
- (BOOL)containsObject:(id)anObject;
- (NSString *)description;
- (NSString *)descriptionWithLocale:(NSDictionary *)locale;
- (BOOL)intersectsSet:(NSSet *)otherSet;
- (BOOL)isEqualToSet:(NSSet *)otherSet;
- (BOOL)isSubsetOfSet:(NSSet *)otherSet;

- (void)makeObjectsPerformSelector:(SEL)aSelector;
- (void)makeObjectsPerformSelector:(SEL)aSelector withObject:(id)argument;

@end

@interface NSSet<ObjectType> (NSSetCreation)

+ (id)set;
+ (id)setWithArray:(NSArray *)array;
+ (id)setWithObject:(id)object;
+ (id)setWithObjects:(id)firstObj, ...;
- (id)initWithArray:(NSArray *)array;
- (id)initWithObjects:(id *)objects count:(unsigned)count;
- (id)initWithObjects:(id)firstObj, ...;
- (id)initWithSet:(NSSet *)set;
- (id)initWithSet:(NSSet *)set copyItems:(BOOL)flag;

+ (id)setWithSet:(NSSet *)set;
+ (id)setWithObjects:(id *)objs count:(unsigned)cnt;

@end

/****************	Mutable Set	****************/

@interface NSMutableSet<ObjectType> : NSSet<ObjectType>

- (void)addObject:(id)object;
- (void)removeObject:(id)object;

@end

@interface NSMutableSet<ObjectType> (NSExtendedMutableSet)

- (void)addObjectsFromArray:(NSArray *)array;
- (void)intersectSet:(NSSet *)otherSet;
- (void)minusSet:(NSSet *)otherSet;
- (void)removeAllObjects;
- (void)unionSet:(NSSet *)otherSet;

- (void)setSet:(NSSet *)otherSet;

@end

@interface NSMutableSet<ObjectType> (NSMutableSetCreation)

+ (id)setWithCapacity:(unsigned)numItems;
- (id)initWithCapacity:(unsigned)numItems;
    
@end

/****************	Counted Set	****************/

@interface NSCountedSet<ObjectType> : NSMutableSet<ObjectType> {
    @private
    void *_table;
    void *_reserved;
}

- (id)initWithCapacity:(unsigned)numItems; // designated initializer

- (id)initWithArray:(NSArray *)array;
- (id)initWithSet:(NSSet *)set;

- (unsigned)countForObject:(id)object;

- (NSEnumerator *)objectEnumerator;
- (void)addObject:(id)object;
- (void)removeObject:(id)object;

@end

