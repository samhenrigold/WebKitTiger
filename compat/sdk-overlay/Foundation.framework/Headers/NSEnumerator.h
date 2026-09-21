/* TIGER SDK OVERLAY: NSEnumerator.h, from the Mac OS X 10.4u SDK, with Objective-C
   lightweight generics added.
 *
 * The 10.4 classes are not parameterized, so "NSEnumerator<NSString *> *" does not parse,
 * and a category cannot retrofit a type parameter. Only the @interface lines
 * change here: the type parameter is declared on the class and repeated on each
 * of its categories, which is what clang requires. Method signatures keep their
 * original "id" types -- id converts to and from the specialized type, so the
 * parameter does its job (letting the specialization parse and be checked at
 * the use site) without rewriting a 1990s header. */
/*	NSEnumerator.h
	Copyright (c) 1995-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSObject.h>

@class NSArray;

/****************	Abstract Enumerator	****************/

@interface NSEnumerator<__covariant ObjectType> : NSObject

- (id)nextObject;

@end

@interface NSEnumerator<ObjectType> (NSExtendedEnumerator)

- (NSArray *)allObjects;

@end

