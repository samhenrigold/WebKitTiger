/*	NSNotification.h -- TIGER SDK OVERLAY
	The 10.4u SDK's header plus NSNotificationName.

	10.10 gave notification names their own type so they could be annotated
	and typed in Swift. It is a plain alias for NSString *, with no runtime or
	ABI consequence. PAL/pal/spi/mac/NSWindowSPI.h declares five notification
	names with it, and 28 sites across the tree name the type. */

/*	NSNotification.h
	Copyright (c) 1994-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSObject.h>

@class NSString, NSDictionary;

/****************	Notifications	****************/

@interface NSNotification : NSObject <NSCopying, NSCoding>

- (NSString *)name;
- (id)object;
- (NSDictionary *)userInfo;

@end

@interface NSNotification (NSNotificationCreation)

+ (id)notificationWithName:(NSString *)aName object:(id)anObject;
+ (id)notificationWithName:(NSString *)aName object:(id)anObject userInfo:(NSDictionary *)aUserInfo;

@end

/****************	Notification Center	****************/

@interface NSNotificationCenter : NSObject {
    @protected
    void *_impl;
    uintptr_t _counter;
    void *_pad[11];
}

+ (id)defaultCenter;
    
- (void)addObserver:(id)observer selector:(SEL)aSelector name:(NSString *)aName object:(id)anObject;

- (void)postNotification:(NSNotification *)notification;
- (void)postNotificationName:(NSString *)aName object:(id)anObject;
- (void)postNotificationName:(NSString *)aName object:(id)anObject userInfo:(NSDictionary *)aUserInfo;

- (void)removeObserver:(id)observer;
- (void)removeObserver:(id)observer name:(NSString *)aName object:(id)anObject;

@end


/* ===== TIGER: additions below this line, not SDK content ================ */

#if !defined(TIGER_NSNOTIFICATIONNAME_DEFINED)
#define TIGER_NSNOTIFICATIONNAME_DEFINED 1
typedef NSString *NSNotificationName;
#endif
