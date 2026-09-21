// ObjC 2.0 runtime entry points that Tiger's legacy libobjc lacks, implemented on the ObjC 1 API.
#import <objc/objc-runtime.h>
#import <Foundation/Foundation.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t propertyLock = PTHREAD_MUTEX_INITIALIZER;

id objc_getProperty(id self, SEL _cmd, ptrdiff_t offset, BOOL atomic)
{
    id *slot = (id *)((char *)self + offset);
    if (!atomic)
        return *slot;
    pthread_mutex_lock(&propertyLock);
    id value = [*slot retain];
    pthread_mutex_unlock(&propertyLock);
    return [value autorelease];
}

void objc_setProperty(id self, SEL _cmd, ptrdiff_t offset, id newValue, BOOL atomic, signed char shouldCopy)
{
    id *slot = (id *)((char *)self + offset);
    if (shouldCopy)
        newValue = (shouldCopy == 2) ? [newValue mutableCopy] : [newValue copy];
    else
        newValue = [newValue retain];
    id oldValue;
    if (atomic) {
        pthread_mutex_lock(&propertyLock);
        oldValue = *slot; *slot = newValue;
        pthread_mutex_unlock(&propertyLock);
    } else {
        oldValue = *slot; *slot = newValue;
    }
    [oldValue release];
}

void objc_setProperty_nonatomic(id self, SEL _cmd, id newValue, ptrdiff_t offset) { objc_setProperty(self, _cmd, offset, newValue, NO, 0); }
void objc_setProperty_atomic(id self, SEL _cmd, id newValue, ptrdiff_t offset) { objc_setProperty(self, _cmd, offset, newValue, YES, 0); }
void objc_setProperty_nonatomic_copy(id self, SEL _cmd, id newValue, ptrdiff_t offset) { objc_setProperty(self, _cmd, offset, newValue, NO, 1); }
void objc_setProperty_atomic_copy(id self, SEL _cmd, id newValue, ptrdiff_t offset) { objc_setProperty(self, _cmd, offset, newValue, YES, 1); }

void objc_copyStruct(void *dest, const void *src, ptrdiff_t size, BOOL atomic, BOOL hasStrong)
{
    if (atomic) pthread_mutex_lock(&propertyLock);
    memcpy(dest, src, size);
    if (atomic) pthread_mutex_unlock(&propertyLock);
}

/* ---------------------------------------------------------------------------
 * ObjC 2.0 runtime API on top of the ObjC 1 API and the fragile struct layouts
 * in <objc/objc-class.h>. Only names Tiger's libobjc does not already export
 * (see logs/api/tiger-libobjc.txt) are defined here.
 * ------------------------------------------------------------------------- */
#import <objc/objc-class.h>
#import <objc/Protocol.h>
#import <objc/runtime.h> /* our own declarations, so a signature mismatch is a build error */
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <stdlib.h>
#include <malloc/malloc.h>

extern void _objc_flush_caches(Class cls); /* private but exported; Nil means "all classes" */

/* --- objects, classes --- */

Class object_getClass(id obj) { return obj ? obj->isa : Nil; }
Class object_setClass(id obj, Class cls) { if (!obj) return Nil; Class old = obj->isa; obj->isa = cls; return old; }
const char *class_getName(Class cls) { return cls ? cls->name : "nil"; }
Class class_getSuperclass(Class cls) { return cls ? cls->super_class : Nil; }
BOOL class_isMetaClass(Class cls) { return (cls && (cls->info & CLS_META)) ? YES : NO; }
/* A class object's isa is its metaclass, an instance's is not; nil yields Nil, hence NO. */
BOOL object_isClass(id obj) { return class_isMetaClass(object_getClass(obj)); }
size_t class_getInstanceSize(Class cls) { return cls ? (size_t)cls->instance_size : 0; }

/* --- methods --- */

SEL method_getName(Method m) { return m ? m->method_name : (SEL)0; }
IMP method_getImplementation(Method m) { return m ? m->method_imp : (IMP)0; }
const char *method_getTypeEncoding(Method m) { return m ? m->method_types : NULL; }

IMP method_setImplementation(Method m, IMP imp)
{
    if (!m) return (IMP)0;
    IMP old = m->method_imp;
    m->method_imp = imp;
    _objc_flush_caches(Nil);
    return old;
}

void method_exchangeImplementations(Method a, Method b)
{
    if (!a || !b || a == b) return;
    IMP t = a->method_imp; a->method_imp = b->method_imp; b->method_imp = t;
    _objc_flush_caches(Nil);
}

/* Methods implemented by cls itself (not inherited), via the ObjC 1 iterator. */
static Method tigerOwnMethod(Class cls, SEL sel)
{
    void *iterator = 0;
    struct objc_method_list *ml;
    while ((ml = class_nextMethodList(cls, &iterator))) {
        for (int i = 0; i < ml->method_count; ++i)
            if (ml->method_list[i].method_name == sel)
                return &ml->method_list[i];
    }
    return NULL;
}

BOOL class_respondsToSelector(Class cls, SEL sel) { return class_getInstanceMethod(cls, sel) != NULL; }

BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types)
{
    if (!cls || !name || !imp) return NO;
    if (tigerOwnMethod(cls, name)) return NO;
    struct objc_method_list *ml = calloc(1, sizeof(struct objc_method_list));
    /* objc4-267.1 objc-class.m: a list whose `obsolete` field is not _OBJC_FIXED_UP ((void *)1771)
       is treated as still holding selector *names*, so the runtime copies the whole list and runs
       every method_name through sel_registerNameNoLock(). Our names are already real SELs, so that
       copy is pure waste (and leaks the list we just built). objc4-437 does the same thing under
       the name fixed_up_method_list. */
    ml->obsolete = (struct objc_method_list *)1771;
    ml->method_count = 1;
    ml->method_list[0].method_name = name;
    ml->method_list[0].method_types = strdup(types ? types : "");
    ml->method_list[0].method_imp = imp;
    class_addMethods(cls, ml); /* takes ownership; also flushes caches */
    return YES;
}

IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types)
{
    Method m = cls ? tigerOwnMethod(cls, name) : NULL;
    if (!m) { class_addMethod(cls, name, imp, types); return (IMP)0; }
    return method_setImplementation(m, imp);
}

Method *class_copyMethodList(Class cls, unsigned int *outCount)
{
    unsigned count = 0, cap = 16;
    Method *out = malloc(cap * sizeof(Method));
    void *iterator = 0;
    struct objc_method_list *ml;
    while (cls && (ml = class_nextMethodList(cls, &iterator))) {
        for (int i = 0; i < ml->method_count; ++i) {
            if (count + 2 > cap) { cap *= 2; out = realloc(out, cap * sizeof(Method)); }
            out[count++] = &ml->method_list[i];
        }
    }
    out[count] = NULL;
    if (outCount) *outCount = count;
    if (!count) { free(out); return NULL; }
    return out;
}

/* --- ivars --- */

const char *ivar_getName(Ivar iv) { return iv ? iv->ivar_name : NULL; }
const char *ivar_getTypeEncoding(Ivar iv) { return iv ? iv->ivar_type : NULL; }
ptrdiff_t ivar_getOffset(Ivar iv) { return iv ? (ptrdiff_t)iv->ivar_offset : 0; }

Ivar *class_copyIvarList(Class cls, unsigned int *outCount)
{
    int n = (cls && cls->ivars) ? cls->ivars->ivar_count : 0;
    if (outCount) *outCount = (unsigned)n;
    if (!n) return NULL;
    Ivar *out = malloc((n + 1) * sizeof(Ivar));
    for (int i = 0; i < n; ++i) out[i] = &cls->ivars->ivar_list[i];
    out[n] = NULL;
    return out;
}

/* --- protocols --- */

/* Layout of a compiled protocol record in __OBJC,__protocol (matches @interface Protocol's ivars). */
struct tiger_protocol {
    Class isa;
    char *protocol_name;
    struct objc_protocol_list *protocol_list;
    void *instance_methods;
    void *class_methods;
};

const char *protocol_getName(Protocol *p) { return p ? ((struct tiger_protocol *)p)->protocol_name : "nil"; }

BOOL protocol_isEqual(Protocol *a, Protocol *b)
{
    if (a == b) return YES;
    if (!a || !b) return NO;
    return strcmp(protocol_getName(a), protocol_getName(b)) == 0;
}

/* No protocol registry exists in ObjC 1, so walk every loaded image's __OBJC,__protocol section. */
Protocol *objc_getProtocol(const char *name)
{
    uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i) {
        const struct mach_header *mh = _dyld_get_image_header(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const struct section *sect = getsectbynamefromheader(mh, "__OBJC", "__protocol");
        if (!sect) continue;
        struct tiger_protocol *p = (struct tiger_protocol *)(sect->addr + slide);
        unsigned long n = sect->size / sizeof(struct tiger_protocol);
        for (unsigned long j = 0; j < n; ++j)
            if (p[j].protocol_name && strcmp(p[j].protocol_name, name) == 0)
                return (Protocol *)&p[j];
    }
    return NULL;
}

static BOOL tigerListHasProtocol(struct objc_protocol_list *list, const char *name)
{
    for (; list; list = list->next) {
        for (int i = 0; i < list->count; ++i) {
            struct tiger_protocol *p = (struct tiger_protocol *)list->list[i];
            if (!p || !p->protocol_name) continue;
            if (strcmp(p->protocol_name, name) == 0) return YES;
            if (tigerListHasProtocol(p->protocol_list, name)) return YES; /* inherited protocols */
        }
    }
    return NO;
}

BOOL class_conformsToProtocol(Class cls, Protocol *proto)
{
    if (!cls || !proto) return NO;
    return tigerListHasProtocol(cls->protocols, protocol_getName(proto));
}

BOOL class_addProtocol(Class cls, Protocol *proto)
{
    if (!cls || !proto) return NO;
    struct objc_protocol_list *node = calloc(1, sizeof(struct objc_protocol_list));
    node->next = cls->protocols;
    node->count = 1;
    node->list[0] = proto;
    cls->protocols = node;
    return YES;
}

/* --- selectors --- */

BOOL sel_isEqual(SEL a, SEL b) { return a == b ? YES : NO; } /* SELs are uniqued on Tiger */

/* --- class list --- */

Class *objc_copyClassList(unsigned int *outCount)
{
    int n = objc_getClassList(NULL, 0);
    if (n <= 0) { if (outCount) *outCount = 0; return NULL; }
    Class *buf = malloc((n + 1) * sizeof(Class));
    n = objc_getClassList(buf, n);
    buf[n] = Nil;
    if (outCount) *outCount = (unsigned)n;
    return buf;
}

/* --- dynamic class creation --- */

/* Build an old-ABI class/metaclass pair by hand; objc_registerClassPair hands it to objc_addClass. */
Class objc_allocateClassPair(Class superclass, const char *name, size_t extraBytes)
{
    if (!name || objc_lookUpClass(name)) return Nil;
    char *copiedName = strdup(name);
    struct objc_class *cls = calloc(1, sizeof(struct objc_class) + extraBytes);
    struct objc_class *meta = calloc(1, sizeof(struct objc_class));

    /* Root of the metaclass chain is the root class's metaclass (NSObject's, normally). */
    Class root = superclass;
    while (root && root->super_class) root = root->super_class;

    meta->isa = root ? root->isa : meta;
    meta->super_class = superclass ? superclass->isa : cls;
    meta->name = copiedName;
    meta->info = CLS_META;
    meta->instance_size = superclass ? superclass->isa->instance_size : (long)sizeof(struct objc_class);

    cls->isa = meta;
    cls->super_class = superclass;
    cls->name = copiedName;
    cls->info = CLS_CLASS;
    cls->instance_size = superclass ? superclass->instance_size : 0;
    return cls;
}

void objc_registerClassPair(Class cls)
{
    if (cls) objc_addClass(cls);
}

void objc_disposeClassPair(Class cls)
{
    if (!cls) return;
    /* Only safe for a pair that was never registered; Tiger has no way to unregister. */
    free((void *)cls->name);
    free(cls->isa);
    free(cls);
}

BOOL class_addIvar(Class cls, const char *name, size_t size, uint8_t alignment, const char *types)
{
    if (!cls || !name || (cls->info & CLS_META)) return NO;
    if (class_getInstanceVariable(cls, name)) return NO;
    size_t align = (size_t)1 << alignment;
    size_t offset = ((size_t)cls->instance_size + align - 1) & ~(align - 1);
    /* Never realloc() cls->ivars: on a compiled class it points into the image's read-only
       __OBJC,__instance_vars section. objc4-437's class_addIvar allocates a fresh list, memcpy's
       the old one, and only frees the old if malloc_size() says it was heap-allocated. */
    struct objc_ivar_list *old = cls->ivars;
    int n = old ? old->ivar_count : 0;
    size_t oldSize = sizeof(struct objc_ivar_list) + (n ? (n - 1) : 0) * sizeof(struct objc_ivar);
    struct objc_ivar_list *list = calloc(1, oldSize + sizeof(struct objc_ivar));
    if (!list) return NO;
    if (old) memcpy(list, old, oldSize);
    if (old && malloc_size(old)) free(old);
    list->ivar_count = n + 1;
    list->ivar_list[n].ivar_name = strdup(name);
    list->ivar_list[n].ivar_type = strdup(types ? types : "");
    list->ivar_list[n].ivar_offset = (int)offset;
    cls->ivars = list;
    cls->instance_size = (long)(offset + size);
    return YES;
}

/* --- associated objects --- */

typedef uintptr_t objc_AssociationPolicy;
#define TIGER_ASSOC_ASSIGN 0
#define TIGER_ASSOC_COPY_MASK 3

struct tiger_assoc { id value; objc_AssociationPolicy policy; };

extern void _tigerInstallDeallocHook(void); /* arc.m; shared with __weak clearing */
__attribute__((constructor)) static void tigerAssocInit(void) { _tigerInstallDeallocHook(); }

static pthread_mutex_t assocLock = PTHREAD_MUTEX_INITIALIZER;
static CFMutableDictionaryRef assocTable; /* object -> (key -> struct tiger_assoc *) */

static void tigerAssocFree(CFAllocatorRef alloc, const void *value)
{
    (void)alloc;
    struct tiger_assoc *a = (struct tiger_assoc *)value;
    if (a->policy != TIGER_ASSOC_ASSIGN) [a->value release];
    free(a);
}

void objc_setAssociatedObject(id object, const void *key, id value, objc_AssociationPolicy policy)
{
    if (!object) return;
    /* Retain/copy outside the lock; the object may run arbitrary code. */
    id stored = value;
    if (value && policy != TIGER_ASSOC_ASSIGN)
        stored = (policy & TIGER_ASSOC_COPY_MASK) == TIGER_ASSOC_COPY_MASK ? [value copy] : [value retain];

    pthread_mutex_lock(&assocLock);
    if (!assocTable)
        assocTable = CFDictionaryCreateMutable(NULL, 0, NULL, &kCFTypeDictionaryValueCallBacks);
    CFMutableDictionaryRef slots = (CFMutableDictionaryRef)CFDictionaryGetValue(assocTable, object);
    if (!value) {
        if (slots) {
            CFDictionaryRemoveValue(slots, key);
            if (!CFDictionaryGetCount(slots)) CFDictionaryRemoveValue(assocTable, object);
        }
        pthread_mutex_unlock(&assocLock);
        return;
    }
    if (!slots) {
        CFDictionaryValueCallBacks cb = { 0, NULL, tigerAssocFree, NULL, NULL };
        slots = CFDictionaryCreateMutable(NULL, 0, NULL, &cb);
        CFDictionarySetValue(assocTable, object, slots);
        CFRelease(slots);
    }
    struct tiger_assoc *a = malloc(sizeof(*a));
    a->value = stored; a->policy = policy;
    CFDictionarySetValue(slots, key, a);
    pthread_mutex_unlock(&assocLock);
}

id objc_getAssociatedObject(id object, const void *key)
{
    if (!object) return nil;
    pthread_mutex_lock(&assocLock);
    id value = nil;
    if (assocTable) {
        CFMutableDictionaryRef slots = (CFMutableDictionaryRef)CFDictionaryGetValue(assocTable, object);
        struct tiger_assoc *a = slots ? (struct tiger_assoc *)CFDictionaryGetValue(slots, key) : NULL;
        if (a) value = a->value;
    }
    pthread_mutex_unlock(&assocLock);
    return value;
}

void objc_removeAssociatedObjects(id object)
{
    if (!object || !assocTable) return;
    pthread_mutex_lock(&assocLock);
    if (assocTable) CFDictionaryRemoveValue(assocTable, object);
    pthread_mutex_unlock(&assocLock);
}

/* ---------------------------------------------------------------------------
 * Property / protocol introspection.
 *
 * Clang does emit ObjC2 property metadata on the fragile ABI: the class struct it lays down is 12
 * words, not the 10 in <objc/objc-class.h>, with { ivar_layout, ext } appended, and ext points into
 * an __OBJC,__class_ext section. Tiger's runtime never reads any of that (it also never sets a
 * CLS_EXT bit in cls->info, so the flag word cannot be used to detect it), but we can follow the
 * pointers ourselves. Reading word 11 of a class Tiger's own compiler built would run off the end of
 * the struct, so we only do it for a class that lives in an image which actually has a __class_ext
 * section, and we then require the pointer to land inside it.
 * ------------------------------------------------------------------------- */

struct tiger_property_list { uint32_t entsize; uint32_t count; struct objc_property first; };
struct tiger_class_ext { uint32_t size; const void *weak_ivar_layout; struct tiger_property_list *propertyList; };

const char *property_getName(objc_property_t p) { return p ? p->name : NULL; }
const char *property_getAttributes(objc_property_t p) { return p ? p->attributes : NULL; }

/* ponytail: linear walk of the loaded images on every call. Fine for introspection; add a cache if
   something starts calling it per frame. */
static struct tiger_property_list *tigerClassPropertyList(Class cls)
{
    if (!cls) return NULL;
    uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i) {
        const struct mach_header *mh = _dyld_get_image_header(i);
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const char *sectName = (cls->info & CLS_META) ? "__meta_class" : "__class";
        const struct section *classes = getsectbynamefromheader(mh, "__OBJC", sectName);
        if (!classes) continue;
        uintptr_t lo = classes->addr + slide;
        if ((uintptr_t)cls < lo || (uintptr_t)cls >= lo + classes->size) continue;

        const struct section *exts = getsectbynamefromheader(mh, "__OBJC", "__class_ext");
        if (!exts) return NULL; /* compiled by a runtime that never emitted property metadata */
        struct tiger_class_ext *ext = (struct tiger_class_ext *)((void **)cls)[11];
        uintptr_t elo = exts->addr + slide;
        if ((uintptr_t)ext < elo || (uintptr_t)ext >= elo + exts->size) return NULL;
        if (ext->size < sizeof(struct tiger_class_ext) || !ext->propertyList) return NULL;
        if (ext->propertyList->entsize != sizeof(struct objc_property)) return NULL;
        return ext->propertyList;
    }
    return NULL; /* dynamically created class, or not in any loaded image */
}

objc_property_t *class_copyPropertyList(Class cls, unsigned int *outCount)
{
    struct tiger_property_list *list = tigerClassPropertyList(cls);
    unsigned n = list ? list->count : 0;
    if (outCount) *outCount = n;
    if (!n) return NULL;
    objc_property_t *out = malloc((n + 1) * sizeof(*out));
    for (unsigned i = 0; i < n; ++i) out[i] = &list->first + i;
    out[n] = NULL;
    return out;
}

/* A protocol's optional methods and its properties hang off an ext record that the old runtime
   chained through the protocol's isa field. Tiger overwrites isa with the Protocol class at load,
   so that record is unreachable and these come back empty. */
objc_property_t *protocol_copyPropertyList(Protocol *proto, unsigned int *outCount)
{
    (void)proto;
    if (outCount) *outCount = 0;
    return NULL;
}

Protocol **class_copyProtocolList(Class cls, unsigned int *outCount)
{
    unsigned count = 0;
    for (struct objc_protocol_list *l = cls ? cls->protocols : NULL; l; l = l->next) count += l->count;
    if (outCount) *outCount = count;
    if (!count) return NULL;
    Protocol **out = malloc((count + 1) * sizeof(Protocol *));
    unsigned k = 0;
    for (struct objc_protocol_list *l = cls->protocols; l; l = l->next)
        for (int i = 0; i < l->count; ++i) out[k++] = l->list[i];
    out[count] = NULL;
    return out;
}

Protocol **protocol_copyProtocolList(Protocol *proto, unsigned int *outCount)
{
    struct objc_protocol_list *head = proto ? ((struct tiger_protocol *)proto)->protocol_list : NULL;
    unsigned count = 0;
    for (struct objc_protocol_list *l = head; l; l = l->next) count += l->count;
    if (outCount) *outCount = count;
    if (!count) return NULL;
    Protocol **out = malloc((count + 1) * sizeof(Protocol *));
    unsigned k = 0;
    for (struct objc_protocol_list *l = head; l; l = l->next)
        for (int i = 0; i < l->count; ++i) out[k++] = l->list[i];
    out[count] = NULL;
    return out;
}

struct objc_method_description *protocol_copyMethodDescriptionList(Protocol *proto, BOOL isRequiredMethod,
                                                                   BOOL isInstanceMethod, unsigned int *outCount)
{
    struct tiger_protocol *p = (struct tiger_protocol *)proto;
    /* Optional methods live in the unreachable ext record; only the required lists are in the protocol. */
    struct objc_method_description_list *list = NULL;
    if (p && isRequiredMethod)
        list = (struct objc_method_description_list *)(isInstanceMethod ? p->instance_methods : p->class_methods);
    unsigned n = list ? (unsigned)list->count : 0;
    if (outCount) *outCount = n;
    if (!n) return NULL;
    struct objc_method_description *out = malloc((n + 1) * sizeof(*out));
    for (unsigned i = 0; i < n; ++i) out[i] = list->list[i];
    out[n].name = (SEL)0;
    out[n].types = NULL;
    return out;
}

/* "T@\"NSString\",C,N,V_s" -> [{T, @"NSString"}, {C, ""}, {N, ""}, {V, _s}]. One malloc holds the
   array and the strings, so the caller's single free() releases everything, as the real API does. */
objc_property_attribute_t *property_copyAttributeList(objc_property_t prop, unsigned int *outCount)
{
    const char *s = prop ? prop->attributes : NULL;
    if (!s || !*s) { if (outCount) *outCount = 0; return NULL; }

    unsigned count = 1;
    for (const char *c = s; *c; ++c) if (*c == ',') ++count;

    size_t len = strlen(s);
    /* Each attribute contributes at most the field text plus two terminators. */
    char *block = malloc(count * sizeof(objc_property_attribute_t) + len + 2 * count + 2);
    objc_property_attribute_t *out = (objc_property_attribute_t *)block;
    char *text = block + count * sizeof(objc_property_attribute_t);

    unsigned k = 0;
    for (const char *c = s; *c; ) {
        const char *end = strchr(c, ',');
        if (!end) end = c + strlen(c);
        char *name = text;
        *text++ = *c;          /* the one-character attribute code */
        *text++ = '\0';
        char *value = text;
        size_t vlen = (size_t)(end - c) - 1;
        memcpy(text, c + 1, vlen);
        text += vlen;
        *text++ = '\0';
        out[k].name = name;
        out[k].value = value;
        ++k;
        c = (*end == ',') ? end + 1 : end;
    }
    if (outCount) *outCount = k;
    return out;
}
