/*	NSMapTable.h
	Copyright (c) 1994-2005, Apple, Inc. All rights reserved.
*/

#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>

/****************	Data structure	****************/

/* TIGER SDK OVERLAY: the 10.4 header names the opaque C struct NSMapTable,
   which is also the name of the class Foundation gained in 10.5. Keeping both
   is impossible -- "@interface NSMapTable" against this typedef is
   "redefinition of NSMapTable as a different kind of symbol", and every use
   becomes ambiguous. The C struct is renamed to NSMapTableCStruct, the C
   functions below keep working against it, and TIGER_NSMAPTABLE_TYPEDEF_RENAMED
   tells <TigerCompat/NSCompat.h> that it may declare the class. */
typedef struct _NSMapTable NSMapTableCStruct;
#define TIGER_NSMAPTABLE_TYPEDEF_RENAMED 1
#define NSMapTable NSMapTableCStruct

typedef struct {
    unsigned	(*hash)(NSMapTable *table, const void *);
    BOOL	(*isEqual)(NSMapTable *table, const void *, const void *);
    void	(*retain)(NSMapTable *table, const void *);
    void	(*release)(NSMapTable *table, void *);
    NSString 	*(*describe)(NSMapTable *table, const void *);
    const void	*notAKeyMarker;
} NSMapTableKeyCallBacks;
    
#define NSNotAnIntMapKey	((const void *)0x80000000)
#define NSNotAPointerMapKey	((const void *)0xffffffff)

typedef struct {
    void	(*retain)(NSMapTable *table, const void *);
    void	(*release)(NSMapTable *table, void *);
    NSString 	*(*describe)(NSMapTable *table, const void *);
} NSMapTableValueCallBacks;
    
typedef struct {unsigned _pi; unsigned _si; void *_bs;} NSMapEnumerator;

/****************	Map table operations	****************/

FOUNDATION_EXPORT NSMapTable *NSCreateMapTableWithZone(NSMapTableKeyCallBacks keyCallBacks, NSMapTableValueCallBacks valueCallBacks, unsigned capacity, NSZone *zone);
FOUNDATION_EXPORT NSMapTable *NSCreateMapTable(NSMapTableKeyCallBacks keyCallBacks, NSMapTableValueCallBacks valueCallBacks, unsigned capacity);
FOUNDATION_EXPORT void NSFreeMapTable(NSMapTable *table);
FOUNDATION_EXPORT void NSResetMapTable(NSMapTable *table);
FOUNDATION_EXPORT BOOL NSCompareMapTables(NSMapTable *table1, NSMapTable *table2);
FOUNDATION_EXPORT NSMapTable *NSCopyMapTableWithZone(NSMapTable *table, NSZone *zone);
FOUNDATION_EXPORT BOOL NSMapMember(NSMapTable *table, const void *key, void **originalKey, void **value);
/* TIGER SDK OVERLAY: these three are the accessors modern Foundation hands the
   *class*, not the C struct -- JavaScriptCore's JSVirtualMachine, JSWrapperMap
   and JSManagedValue all call them on an NSMapTable object, and the values they
   store are raw integers rather than objects, which is exactly what the C API
   is for there. So the class-taking spellings are declared in
   <TigerCompat/NSCompat.h> and implemented in compat/nscompat-maptable.m, and
   Tiger's own C entry points keep working here under a CStruct suffix, bound by
   asm label to the symbols Foundation actually exports. The other C functions
   below are untouched: nothing in the tree calls them on a class. */
FOUNDATION_EXPORT void *NSMapGetCStruct(NSMapTable *table, const void *key) __asm("_NSMapGet");
FOUNDATION_EXPORT void NSMapInsertCStruct(NSMapTable *table, const void *key, const void *value) __asm("_NSMapInsert");
FOUNDATION_EXPORT void NSMapRemoveCStruct(NSMapTable *table, const void *key) __asm("_NSMapRemove");
FOUNDATION_EXPORT void NSMapInsertKnownAbsent(NSMapTable *table, const void *key, const void *value);
FOUNDATION_EXPORT void *NSMapInsertIfAbsent(NSMapTable *table, const void *key, const void *value);
FOUNDATION_EXPORT NSMapEnumerator NSEnumerateMapTable(NSMapTable *table);
FOUNDATION_EXPORT BOOL NSNextMapEnumeratorPair(NSMapEnumerator *enumerator, void **key, void **value);
FOUNDATION_EXPORT void NSEndMapTableEnumeration(NSMapEnumerator *enumerator);
FOUNDATION_EXPORT unsigned NSCountMapTable(NSMapTable *table);
FOUNDATION_EXPORT NSString *NSStringFromMapTable(NSMapTable *table);
FOUNDATION_EXPORT NSArray *NSAllMapTableKeys(NSMapTable *table);
FOUNDATION_EXPORT NSArray *NSAllMapTableValues(NSMapTable *table);

/****************	Common map table key callbacks	****************/

FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSIntMapKeyCallBacks;
FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSNonOwnedPointerMapKeyCallBacks;
FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSNonOwnedPointerOrNullMapKeyCallBacks;
FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSNonRetainedObjectMapKeyCallBacks;
FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSObjectMapKeyCallBacks;
FOUNDATION_EXPORT const NSMapTableKeyCallBacks NSOwnedPointerMapKeyCallBacks;

/****************	Common map table value callbacks	****************/

FOUNDATION_EXPORT const NSMapTableValueCallBacks NSIntMapValueCallBacks;
FOUNDATION_EXPORT const NSMapTableValueCallBacks NSNonOwnedPointerMapValueCallBacks;
FOUNDATION_EXPORT const NSMapTableValueCallBacks NSObjectMapValueCallBacks;
FOUNDATION_EXPORT const NSMapTableValueCallBacks NSNonRetainedObjectMapValueCallBacks;
FOUNDATION_EXPORT const NSMapTableValueCallBacks NSOwnedPointerMapValueCallBacks;

/* TIGER SDK OVERLAY: stop renaming, so that NSMapTable means the class from
   here on. Everything above this line is the C API and has been rewritten to
   NSMapTableCStruct by the macro. */
#undef NSMapTable

/* TIGER SDK OVERLAY: forward-declare the class here rather than leaving the
   name undeclared until <TigerCompat/NSCompat.h> is reached.

   AppKit's NSHelpManager.h declares three private instance variables as
   "NSMapTable *". After the #undef above that spelling is an undeclared type,
   so <AppKit/AppKit.h> fails to parse in any translation unit that has not
   already imported NSCompat.h. With the forward declaration those ivars are a
   pointer to the compatibility class instead, which is the same four bytes and
   is never dereferenced by us. */
#ifdef __OBJC__
@class NSMapTable;
#endif
