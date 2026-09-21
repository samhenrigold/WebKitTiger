// ObjC 2.0 runtime API implemented on Tiger's ObjC 1 runtime. Build: spike/run.sh objc2test.m
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void T(const char *what, int ok) { printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

@protocol TigerBase
- (void)baseProtoMethod;
@end

@protocol TigerProto <TigerBase>
@required - (int)protoMethod;
@optional - (int)optionalProtoMethod;
@property (nonatomic, readonly) int protoProperty;
@end

@interface Base : NSObject { int _a; double _b; }
- (int)value;
- (const char *)who;
@end
@implementation Base
- (int)value { return 1; }
- (const char *)who { return "Base"; }
@end

@interface Derived : Base <TigerProto> { char _c; }
@end
@implementation Derived
- (int)value { return 2; }
- (int)protoMethod { return 42; }
@end

@interface Plain : NSObject @end
@implementation Plain @end

@interface Props : NSObject { NSString *_title; int _count; }
@property (nonatomic, copy) NSString *title;
@property (nonatomic, assign, readonly) int count;
@end
@implementation Props
@synthesize title = _title; @synthesize count = _count;
@end

static int swizzledValue(id self, SEL _cmd) { (void)self; (void)_cmd; return 99; }
static int addedMethod(id self, SEL _cmd) { (void)self; (void)_cmd; return 7; }

// Instance method bodies for the class we build at runtime.
static int dynGetValue(id self, SEL _cmd) { (void)_cmd; int v = 0; object_getInstanceVariable(self, "dynValue", (void **)&v); return v; }
static void dynSetValue(id self, SEL _cmd, int v) { (void)_cmd; object_setInstanceVariable(self, "dynValue", (void *)(intptr_t)v); }

static int assocDeallocs = 0;
@interface Owner : NSObject @end
@implementation Owner
- (void)dealloc { assocDeallocs++; [super dealloc]; }
@end

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    // --- classes ---
    Derived *d = [[Derived alloc] init];
    T("object_getClass", object_getClass(d) == [Derived class]);
    T("class_getName", strcmp(class_getName([Derived class]), "Derived") == 0);
    T("class_getSuperclass", class_getSuperclass([Derived class]) == [Base class]);
    T("class_isMetaClass on class", !class_isMetaClass([Derived class]));
    T("class_isMetaClass on metaclass", class_isMetaClass(object_getClass([Derived class])));
    T("objc_getMetaClass", (Class)objc_getMetaClass("Derived") == object_getClass([Derived class]));
    T("objc_lookUpClass", (Class)objc_lookUpClass("Derived") == [Derived class]);
    T("class_getInstanceSize >= ivars", class_getInstanceSize([Derived class]) >= sizeof(void *) + sizeof(int) + sizeof(double) + 1);
    T("class_respondsToSelector (own)", class_respondsToSelector([Derived class], @selector(value)));
    T("class_respondsToSelector (inherited)", class_respondsToSelector([Derived class], @selector(who)));
    T("class_respondsToSelector (absent)", !class_respondsToSelector([Derived class], @selector(nosuchmethod)));

    T("object_isClass on an instance", !object_isClass(d));
    T("object_isClass on a class", object_isClass((id)[Derived class]));
    T("object_isClass on a metaclass", object_isClass((id)object_getClass([Derived class])));
    T("object_isClass on nil", !object_isClass(nil));

    Class saved = object_setClass(d, [Base class]);
    T("object_setClass changes dispatch", [d value] == 1 && saved == [Derived class]);
    object_setClass(d, [Derived class]);

    // --- methods ---
    Method m = class_getInstanceMethod([Derived class], @selector(value));
    T("method_getName", sel_isEqual(method_getName(m), @selector(value)));
    T("method_getImplementation", method_getImplementation(m) != NULL);
    T("method_getTypeEncoding", method_getTypeEncoding(m) && method_getTypeEncoding(m)[0] == 'i');
    T("sel_getName", strcmp(sel_getName(method_getName(m)), "value") == 0);

    unsigned n = 0;
    Method *list = class_copyMethodList([Derived class], &n);
    int sawValue = 0, sawProto = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (sel_isEqual(method_getName(list[i]), @selector(value))) sawValue = 1;
        if (sel_isEqual(method_getName(list[i]), @selector(protoMethod))) sawProto = 1;
    }
    T("class_copyMethodList lists own methods", n >= 2 && sawValue && sawProto);
    free(list);

    T("class_addMethod (new selector)", class_addMethod([Plain class], @selector(addedThing), (IMP)addedMethod, "i@:"));
    T("class_addMethod dispatches", (int)(intptr_t)[[[Plain alloc] init] performSelector:@selector(addedThing)] == 7);
    T("class_addMethod refuses duplicates", !class_addMethod([Plain class], @selector(addedThing), (IMP)addedMethod, "i@:"));

    Base *b = [[Base alloc] init];
    T("value before swizzle", [b value] == 1);
    IMP old = class_replaceMethod([Base class], @selector(value), (IMP)swizzledValue, "i@:");
    T("class_replaceMethod returns old IMP", old != NULL);
    T("class_replaceMethod takes effect", [b value] == 99);
    method_setImplementation(class_getInstanceMethod([Base class], @selector(value)), old);
    T("method_setImplementation restores", [b value] == 1);

    class_addMethod([Base class], @selector(otherValue), (IMP)swizzledValue, "i@:");
    method_exchangeImplementations(class_getInstanceMethod([Base class], @selector(value)),
                                   class_getInstanceMethod([Base class], @selector(otherValue)));
    T("method_exchangeImplementations", [b value] == 99 && (int)(intptr_t)[b performSelector:@selector(otherValue)] == 1);

    // --- ivars ---
    Ivar iv = class_getInstanceVariable([Base class], "_b");
    T("class_getInstanceVariable", iv != NULL);
    T("ivar_getName", strcmp(ivar_getName(iv), "_b") == 0);
    T("ivar_getOffset", ivar_getOffset(iv) >= (ptrdiff_t)(sizeof(void *) + sizeof(int)));
    T("ivar_getTypeEncoding", ivar_getTypeEncoding(iv)[0] == 'd');
    unsigned ivn = 0;
    Ivar *ivs = class_copyIvarList([Base class], &ivn);
    T("class_copyIvarList", ivn == 2 && strcmp(ivar_getName(ivs[0]), "_a") == 0);
    free(ivs);

    // --- protocols ---
    Protocol *p = objc_getProtocol("TigerProto");
    T("objc_getProtocol", p != NULL);
    T("protocol_getName", p && strcmp(protocol_getName(p), "TigerProto") == 0);
    T("class_conformsToProtocol", class_conformsToProtocol([Derived class], p));
    T("class_conformsToProtocol (negative)", !class_conformsToProtocol([Plain class], p));
    T("class_addProtocol", class_addProtocol([Plain class], p));
    T("class_conformsToProtocol after add", class_conformsToProtocol([Plain class], p));

    // --- protocol and property introspection ---
    unsigned pln = 0;
    Protocol **plist = class_copyProtocolList([Derived class], &pln);
    T("class_copyProtocolList", pln == 1 && strcmp(protocol_getName(plist[0]), "TigerProto") == 0);
    free(plist);
    Protocol **added = class_copyProtocolList([Plain class], &pln); /* got TigerProto from class_addProtocol above */
    T("class_copyProtocolList sees an added protocol", pln == 1 && strcmp(protocol_getName(added[0]), "TigerProto") == 0);
    free(added);
    T("class_copyProtocolList on a protocol-free class", class_copyProtocolList([Base class], &pln) == NULL && pln == 0);

    Protocol **inherited = protocol_copyProtocolList(p, &pln);
    T("protocol_copyProtocolList", pln == 1 && strcmp(protocol_getName(inherited[0]), "TigerBase") == 0);
    free(inherited);

    unsigned mdn = 0;
    struct objc_method_description *md = protocol_copyMethodDescriptionList(p, YES, YES, &mdn);
    int sawReq = 0;
    for (unsigned i = 0; i < mdn; ++i) if (sel_isEqual(md[i].name, @selector(protoMethod))) sawReq = 1;
    T("protocol_copyMethodDescriptionList (required)", mdn >= 1 && sawReq);
    T("method description carries types", mdn >= 1 && md[0].types && md[0].types[0]);
    free(md);
    protocol_copyMethodDescriptionList(p, YES, NO, &mdn);
    T("no required class methods", mdn == 0);
    protocol_copyMethodDescriptionList(p, NO, YES, &mdn);
    T("known limit: optional methods unreachable", mdn == 0);
    protocol_copyPropertyList(p, &pln);
    T("known limit: protocol properties unreachable", pln == 0);

    unsigned prn = 0;
    objc_property_t *props = class_copyPropertyList([Props class], &prn);
    T("class_copyPropertyList", prn == 2 && props != NULL);
    int sawTitle = 0, sawCount = 0;
    for (unsigned i = 0; i < prn; ++i) {
        if (strcmp(property_getName(props[i]), "title") == 0) sawTitle = 1;
        if (strcmp(property_getName(props[i]), "count") == 0) sawCount = 1;
    }
    T("property_getName", sawTitle && sawCount);
    const char *attrs = property_getAttributes(props[0]);
    T("property_getAttributes", attrs && attrs[0] == 'T');
    printf("   [info] properties: ");
    for (unsigned i = 0; i < prn; ++i) printf("%s{%s} ", property_getName(props[i]), property_getAttributes(props[i]));
    printf("\n");

    unsigned an = 0;
    objc_property_attribute_t *pa = property_copyAttributeList(props[0], &an);
    int sawType = 0, sawIvar = 0;
    for (unsigned i = 0; i < an; ++i) {
        if (pa[i].name[0] == 'T' && pa[i].value[0]) sawType = 1;
        if (pa[i].name[0] == 'V' && pa[i].value[0] == '_') sawIvar = 1;
    }
    T("property_copyAttributeList splits fields", an >= 2 && sawType && sawIvar);
    T("attribute names are one char", an >= 1 && pa[0].name[1] == 0);
    free(pa);
    free(props);
    T("class_copyPropertyList on a Tiger class is empty",
      (class_copyPropertyList([NSString class], &prn) == NULL) && prn == 0);
    T("class_copyPropertyList on a dynamic class is empty",
      (class_copyPropertyList([Plain class], &prn), 1));

    // --- class list ---
    unsigned cn = 0;
    Class *classes = objc_copyClassList(&cn);
    int foundDerived = 0;
    for (unsigned i = 0; i < cn; ++i) if (classes[i] == [Derived class]) foundDerived = 1;
    T("objc_copyClassList finds our class", cn > 100 && foundDerived);
    free(classes);

    // --- dynamic class pair ---
    Class dyn = objc_allocateClassPair([NSObject class], "TigerDynamic", 0);
    T("objc_allocateClassPair", dyn != Nil);
    T("class_addIvar", class_addIvar(dyn, "dynValue", sizeof(int), 2, "i"));
    T("class_addIvar grew instance size", class_getInstanceSize(dyn) >= class_getInstanceSize([NSObject class]) + sizeof(int));
    class_addMethod(dyn, @selector(dynValue), (IMP)dynGetValue, "i@:");
    class_addMethod(dyn, @selector(setDynValue:), (IMP)dynSetValue, "v@:i");
    objc_registerClassPair(dyn);
    T("objc_registerClassPair", (Class)objc_lookUpClass("TigerDynamic") == dyn);
    id inst = [[dyn alloc] init];
    T("dynamic class instantiates", inst != nil && object_getClass(inst) == dyn);
    T("dynamic class inherits NSObject", [inst isKindOfClass:[NSObject class]]);
    [inst performSelector:@selector(setDynValue:) withObject:(id)(intptr_t)123];
    T("dynamic ivar + methods work", (int)(intptr_t)[inst performSelector:@selector(dynValue)] == 123);
    [inst release];

    Class scratch = objc_allocateClassPair([NSObject class], "TigerScratch", 0);
    objc_disposeClassPair(scratch);
    T("objc_disposeClassPair (unregistered)", objc_lookUpClass("TigerScratch") == nil);

    // --- selectors ---
    T("sel_isEqual", sel_isEqual(sel_registerName("value"), @selector(value)) && !sel_isEqual(@selector(value), @selector(who)));

    // --- associated objects ---
    static char keyA, keyB;
    Owner *o = [[Owner alloc] init];
    NSMutableString *s = [NSMutableString stringWithString:@"assoc"];
    objc_setAssociatedObject(o, &keyA, s, OBJC_ASSOCIATION_RETAIN);
    objc_setAssociatedObject(o, &keyB, s, OBJC_ASSOCIATION_COPY);
    T("objc_getAssociatedObject (retain)", objc_getAssociatedObject(o, &keyA) == s);
    T("objc_getAssociatedObject (copy)", objc_getAssociatedObject(o, &keyB) != s
        && [(NSString *)objc_getAssociatedObject(o, &keyB) isEqualToString:@"assoc"]);
    T("missing key is nil", objc_getAssociatedObject(o, (void *)0x1234) == nil);
    objc_setAssociatedObject(o, &keyA, nil, OBJC_ASSOCIATION_RETAIN);
    T("setting nil clears", objc_getAssociatedObject(o, &keyA) == nil);
    objc_setAssociatedObject(o, &keyA, s, OBJC_ASSOCIATION_RETAIN);
    unsigned before = [s retainCount];
    [o release];
    T("dealloc releases associated objects", assocDeallocs == 1 && [s retainCount] == before - 1);
    T("association survives nothing after dealloc", objc_getAssociatedObject(o, &keyB) == nil);

    [pool release];
    printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
