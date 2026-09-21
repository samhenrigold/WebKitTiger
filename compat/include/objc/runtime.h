/* <objc/runtime.h> for the 10.4 SDK: declares the ObjC 2.0 runtime API that libtigercompat
 * implements on top of Tiger's ObjC 1 libobjc. Types stay the fragile-ABI structs. */
#ifndef TIGER_OBJC_RUNTIME_H
#define TIGER_OBJC_RUNTIME_H

#include <objc/objc.h>
#include <objc/objc-class.h>
#include <objc/objc-runtime.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __OBJC__
#import <objc/Protocol.h>   /* Protocol, struct objc_method_description{,_list} */
#else
typedef struct objc_object Protocol;
struct objc_method_description { SEL name; char *types; };
struct objc_method_description_list { int count; struct objc_method_description list[1]; };
#endif

/* ARC refuses a plain Protocol ** ("ill-formed in ARC"); the real runtime.h spells these
 * __unsafe_unretained too, and WTF's ObjCRuntimeExtras expects exactly that. */
#if defined(__has_feature) && __has_feature(objc_arc)
#define TIGER_UNRETAINED __unsafe_unretained
#else
#define TIGER_UNRETAINED
#endif

/* Declared properties exist in the fragile ABI's metadata even though <objc/objc-class.h> has no
 * types for them; libtigercompat digs them out of __OBJC,__class_ext. */
struct objc_property { const char *name; const char *attributes; };
typedef struct objc_property *objc_property_t;
typedef struct { const char *name; const char *value; } objc_property_attribute_t;

Class object_getClass(id obj);
Class object_setClass(id obj, Class cls);
BOOL object_isClass(id obj);

const char *class_getName(Class cls);
Class class_getSuperclass(Class cls);
BOOL class_isMetaClass(Class cls);
size_t class_getInstanceSize(Class cls);
BOOL class_respondsToSelector(Class cls, SEL sel);
BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types);
IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types);
Method *class_copyMethodList(Class cls, unsigned int *outCount);
Ivar *class_copyIvarList(Class cls, unsigned int *outCount);
BOOL class_addIvar(Class cls, const char *name, size_t size, uint8_t alignment, const char *types);
BOOL class_addProtocol(Class cls, Protocol *proto);
BOOL class_conformsToProtocol(Class cls, Protocol *proto);
Protocol * TIGER_UNRETAINED *class_copyProtocolList(Class cls, unsigned int *outCount);
objc_property_t *class_copyPropertyList(Class cls, unsigned int *outCount);

SEL method_getName(Method m);
IMP method_getImplementation(Method m);
const char *method_getTypeEncoding(Method m);
IMP method_setImplementation(Method m, IMP imp);
void method_exchangeImplementations(Method a, Method b);

const char *ivar_getName(Ivar iv);
const char *ivar_getTypeEncoding(Ivar iv);
ptrdiff_t ivar_getOffset(Ivar iv);

Protocol *objc_getProtocol(const char *name);
const char *protocol_getName(Protocol *p);
BOOL protocol_isEqual(Protocol *a, Protocol *b);
Protocol * TIGER_UNRETAINED *protocol_copyProtocolList(Protocol *proto, unsigned int *outCount);
/* Optional methods and protocol properties are recovered from the _OBJC_PROTOCOLEXT_<Name> symbol
   in the defining image; they are empty if that image had its local symbols stripped. */
struct objc_method_description *protocol_copyMethodDescriptionList(Protocol *proto, BOOL isRequiredMethod,
                                                                   BOOL isInstanceMethod, unsigned int *outCount);
objc_property_t *protocol_copyPropertyList(Protocol *proto, unsigned int *outCount);
/* The old ABI has no separate optional-property list, so isRequiredProperty NO returns empty. */
objc_property_t *protocol_copyPropertyList2(Protocol *proto, unsigned int *outCount,
                                            BOOL isRequiredProperty, BOOL isInstanceProperty);

const char *property_getName(objc_property_t property);
const char *property_getAttributes(objc_property_t property);
objc_property_attribute_t *property_copyAttributeList(objc_property_t property, unsigned int *outCount);

BOOL sel_isEqual(SEL a, SEL b);

Class *objc_copyClassList(unsigned int *outCount);
Class objc_allocateClassPair(Class superclass, const char *name, size_t extraBytes);
void objc_registerClassPair(Class cls);
void objc_disposeClassPair(Class cls);

/* ARC weak entry points, implemented in compat/arc.m. Apple declares these in <objc/objc.h>, which
 * the 10.4 SDK predates. WebKit's own wtf/spi/cocoa/objcSPI.h declares five of the seven, but not
 * objc_storeWeak or objc_loadWeak, and WeakObjCPtr.h calls both from its non-ARC branch. Declaring
 * the whole family here keeps them in one place; the overlapping five match objcSPI.h exactly. */
id objc_loadWeak(id *location);
id objc_loadWeakRetained(id *location);
id objc_storeWeak(id *location, id obj);
id objc_initWeak(id *location, id value);
void objc_destroyWeak(id *location);
void objc_copyWeak(id *to, id *from);
void objc_moveWeak(id *to, id *from);

typedef uintptr_t objc_AssociationPolicy;
#define OBJC_ASSOCIATION_ASSIGN           ((objc_AssociationPolicy)0)
#define OBJC_ASSOCIATION_RETAIN_NONATOMIC ((objc_AssociationPolicy)1)
#define OBJC_ASSOCIATION_COPY_NONATOMIC   ((objc_AssociationPolicy)3)
#define OBJC_ASSOCIATION_RETAIN           ((objc_AssociationPolicy)01401)
#define OBJC_ASSOCIATION_COPY             ((objc_AssociationPolicy)01403)

void objc_setAssociatedObject(id object, const void *key, id value, objc_AssociationPolicy policy);
id objc_getAssociatedObject(id object, const void *key);
void objc_removeAssociatedObjects(id object);

#ifdef __cplusplus
}
#endif

#endif
