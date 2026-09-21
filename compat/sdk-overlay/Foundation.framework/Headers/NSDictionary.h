/* TIGER SDK OVERLAY: NSDictionary.h, from the Mac OS X 10.4u SDK, with Objective-C
   lightweight generics added.
 *
 * The 10.4 classes are not parameterized, so "NSDictionary<NSString *, NSNumber *> *" does not parse,
 * and a category cannot retrofit a type parameter. Only the @interface lines
 * change here: the type parameter is declared on the class and repeated on each
 * of its categories, which is what clang requires. Method signatures keep their
 * original "id" types -- id converts to and from the specialized type, so the
 * parameter does its job (letting the specialization parse and be checked at
 * the use site) without rewriting a 1990s header. */
/*	NSDictionary.h
	Copyright (c) 1994-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSObject.h>

@class NSArray, NSEnumerator, NSString, NSURL;

/****************	Immutable Dictionary	****************/

@interface NSDictionary<__covariant KeyType, __covariant ObjectType> : NSObject <NSCopying, NSMutableCopying, NSCoding>

- (unsigned)count;
- (NSEnumerator *)keyEnumerator;
- (id)objectForKey:(id)aKey;

@end

@interface NSDictionary<KeyType, ObjectType> (NSExtendedDictionary)

- (NSArray *)allKeys;
- (NSArray *)allKeysForObject:(id)anObject;    
- (NSArray *)allValues;
- (NSString *)description;
- (NSString *)descriptionInStringsFileFormat;
- (NSString *)descriptionWithLocale:(NSDictionary *)locale;
- (NSString *)descriptionWithLocale:(NSDictionary *)locale indent:(unsigned)level;
- (BOOL)isEqualToDictionary:(NSDictionary *)otherDictionary;
- (NSEnumerator *)objectEnumerator;
- (NSArray *)objectsForKeys:(NSArray *)keys notFoundMarker:(id)marker;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)useAuxiliaryFile;
- (BOOL)writeToURL:(NSURL *)url atomically:(BOOL)atomically; // the atomically flag is ignored if url of a type that cannot be written atomically.

- (NSArray *)keysSortedByValueUsingSelector:(SEL)comparator;

@end

@interface NSDictionary<KeyType, ObjectType> (NSDictionaryCreation)

+ (id)dictionary;
+ (id)dictionaryWithContentsOfFile:(NSString *)path;
+ (id)dictionaryWithContentsOfURL:(NSURL *)url;
+ (id)dictionaryWithObjects:(NSArray *)objects forKeys:(NSArray *)keys;
+ (id)dictionaryWithObjects:(id *)objects forKeys:(id *)keys count:(unsigned)count;
+ (id)dictionaryWithObjectsAndKeys:(id)firstObject, ...;
- (id)initWithContentsOfFile:(NSString *)path;
- (id)initWithContentsOfURL:(NSURL *)url;
- (id)initWithObjects:(NSArray *)objects forKeys:(NSArray *)keys;
- (id)initWithObjects:(id *)objects forKeys:(id *)keys count:(unsigned)count;
- (id)initWithObjectsAndKeys:(id)firstObject, ...;
- (id)initWithDictionary:(NSDictionary *)otherDictionary;

+ (id)dictionaryWithDictionary:(NSDictionary *)dict;
+ (id)dictionaryWithObject:(id)object forKey:(id)key;
- (id)initWithDictionary:(NSDictionary *)otherDictionary copyItems:(BOOL)aBool;

@end

/****************	Mutable Dictionary	****************/

@interface NSMutableDictionary<KeyType, ObjectType> : NSDictionary<KeyType, ObjectType>

- (void)removeObjectForKey:(id)aKey;
- (void)setObject:(id)anObject forKey:(id)aKey;

@end

@interface NSMutableDictionary<KeyType, ObjectType> (NSExtendedMutableDictionary)

- (void)addEntriesFromDictionary:(NSDictionary *)otherDictionary;
- (void)removeAllObjects;
- (void)removeObjectsForKeys:(NSArray *)keyArray;
- (void)setDictionary:(NSDictionary *)otherDictionary;

@end

@interface NSMutableDictionary<KeyType, ObjectType> (NSMutableDictionaryCreation)

+ (id)dictionaryWithCapacity:(unsigned)numItems;
- (id)initWithCapacity:(unsigned)numItems;

@end

