/* TIGER: NSMapTable, the 10.5 Objective-C class, on top of Tiger's
 * NSCreateMapTable C API.
 *
 * Tiger's <Foundation/NSMapTable.h> spells the C hash table
 * `typedef struct _NSMapTable NSMapTable;`, which collides head-on with the
 * class name. compat/sdk-overlay renames that typedef to NSMapTableCStruct, so
 * this file, and every other translation unit, needs the overlay on its
 * framework search path. See compat/sdk-overlay/README.md for the flags.
 *
 * Weak memory does not exist on the fragile runtime, so every "weak" option
 * degrades to non-retained: an entry whose key has been deallocated is not
 * zeroed out, it dangles until something removes it. JavaScriptCore's wrapper
 * caches are the only users and they remove entries explicitly.
 */

#import <Foundation/Foundation.h>
#import <TigerCompat/NSCompat.h>

#if !defined(TIGER_NSMAPTABLE_TYPEDEF_RENAMED)
#error "compat/sdk-overlay is not on the framework search path; see its README"
#endif

/* The memory bits of an NSPointerFunctions option word. Anything that is not
 * plain strong memory is treated as non-owned. */
static BOOL tigerOptionsAreRetaining(NSUInteger options)
{
    NSUInteger memory = options & 0xff;
    return memory == NSPointerFunctionsStrongMemory;
}

/* Only the object personality may be messaged with -hash/-isEqual:. Every other personality
 * (opaque, object-pointer, C string, struct, integer) holds something that is not an object, so
 * it must hash on the address instead -- JSC's JSVirtualMachine/JSWrapperMap tables key on
 * JSContextGroupRef/JSGlobalContextRef under the opaque personality, and sending -hash to one of
 * those crashes. C-string and struct personalities really want memcmp; Tiger has no such map
 * callbacks, and identity is the safe answer for the callers this port has. */
static BOOL tigerOptionsUsePointerIdentity(NSUInteger options)
{
    return (options & 0xff00) != NSPointerFunctionsObjectPersonality;
}

@implementation NSMapTable

+ (id)strongToStrongObjectsMapTable
{
    return [[[self alloc] initWithKeyOptions:NSPointerFunctionsStrongMemory
                                valueOptions:NSPointerFunctionsStrongMemory
                                    capacity:0] autorelease];
}

+ (id)weakToStrongObjectsMapTable
{
    return [[[self alloc] initWithKeyOptions:NSPointerFunctionsWeakMemory
                                valueOptions:NSPointerFunctionsStrongMemory
                                    capacity:0] autorelease];
}

+ (id)strongToWeakObjectsMapTable
{
    return [[[self alloc] initWithKeyOptions:NSPointerFunctionsStrongMemory
                                valueOptions:NSPointerFunctionsWeakMemory
                                    capacity:0] autorelease];
}

+ (id)weakToWeakObjectsMapTable
{
    return [[[self alloc] initWithKeyOptions:NSPointerFunctionsWeakMemory
                                valueOptions:NSPointerFunctionsWeakMemory
                                    capacity:0] autorelease];
}

- (id)init
{
    return [self initWithKeyOptions:NSPointerFunctionsStrongMemory
                       valueOptions:NSPointerFunctionsStrongMemory
                           capacity:0];
}

- (id)initWithKeyOptions:(NSUInteger)keyOptions
            valueOptions:(NSUInteger)valueOptions
                capacity:(NSUInteger)initialCapacity
{
    if (!(self = [super init]))
        return nil;

    _keyOptions = keyOptions;
    _valueOptions = valueOptions;

    /* Pointer identity wants the non-owned *pointer* callbacks, which hash on
     * the address. The object callbacks hash with -hash/-isEqual: and differ
     * only in whether they retain. */
    NSMapTableKeyCallBacks keyCallBacks;
    if (tigerOptionsUsePointerIdentity(keyOptions)) {
        keyCallBacks = NSNonOwnedPointerMapKeyCallBacks;
        /* Tiger has no retain+identity key callbacks, so build them: address hashing from the
         * non-owned set, ownership from the object set. Without this, a strong-memory
         * object-pointer-personality table silently does not retain its keys. Only do it for the
         * object-pointer personality; retaining an opaque or integer key would message a
         * non-object. */
        if ((keyOptions & 0xff00) == NSPointerFunctionsObjectPointerPersonality
            && tigerOptionsAreRetaining(keyOptions)) {
            keyCallBacks.retain = NSObjectMapKeyCallBacks.retain;
            keyCallBacks.release = NSObjectMapKeyCallBacks.release;
        }
    } else if (tigerOptionsAreRetaining(keyOptions))
        keyCallBacks = NSObjectMapKeyCallBacks;
    else
        keyCallBacks = NSNonRetainedObjectMapKeyCallBacks;

    NSMapTableValueCallBacks valueCallBacks = tigerOptionsAreRetaining(valueOptions)
        ? NSObjectMapValueCallBacks
        : NSNonOwnedPointerMapValueCallBacks;

    _table = NSCreateMapTable(keyCallBacks, valueCallBacks, (unsigned)initialCapacity);
    if (!_table) {
        [self release];
        return nil;
    }
    return self;
}

- (void)dealloc
{
    if (_table)
        NSFreeMapTable((NSMapTableCStruct *)_table);
    [super dealloc];
}

- (id)objectForKey:(id)key
{
    if (!key)
        return nil;
    return (id)NSMapGet((NSMapTableCStruct *)_table, key);
}

- (void)setObject:(id)object forKey:(id)key
{
    if (!key)
        return;
    if (!object) {
        [self removeObjectForKey:key];
        return;
    }
    NSMapInsert((NSMapTableCStruct *)_table, key, object);
}

- (void)removeObjectForKey:(id)key
{
    if (key)
        NSMapRemove((NSMapTableCStruct *)_table, key);
}

- (void)removeAllObjects
{
    NSResetMapTable((NSMapTableCStruct *)_table);
}

- (NSUInteger)count
{
    return NSCountMapTable((NSMapTableCStruct *)_table);
}

/* Both of these retain every element, so they are only meaningful on a table whose keys (resp.
 * values) really are objects. Real Foundation is the same: -[NSClassicMapTable allKeys] packs the
 * keys and calls +arrayWithObjects:count:, which retains them, and Apple documents enumeration of
 * a non-object personality as unsupported. Routing non-objects through an NSEnumerator instead is
 * worse, not better -- tried it, and Tiger's NSCFArray enumerator messages the element on the
 * second -nextObject and hangs. So: do not enumerate an opaque/integer-keyed table. */
- (NSEnumerator *)keyEnumerator
{
    return [NSAllMapTableKeys((NSMapTableCStruct *)_table) objectEnumerator];
}

- (NSEnumerator *)objectEnumerator
{
    return [NSAllMapTableValues((NSMapTableCStruct *)_table) objectEnumerator];
}


/* for-in over an NSMapTable yields its keys, matching 10.5+. */
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len
{
    if (!state->state) {
        state->state = 1;
        state->mutationsPtr = &state->extra[4];
        state->extra[0] = (unsigned long)[[self keyEnumerator] retain];
    }
    NSEnumerator *e = (NSEnumerator *)state->extra[0];
    NSUInteger n = 0;
    id obj;
    while (n < len && (obj = [e nextObject]) != nil)
        buffer[n++] = obj;
    /* n < len means nextObject returned nil, i.e. the enumerator is spent. Releasing here rather
     * than only on the n == 0 call also stops a `break` out of the loop from leaking it (and the
     * key array it holds); a later call finds extra[0] == 0 and [nil nextObject] ends the loop. */
    if (n < len) {
        [e release];
        state->extra[0] = 0;
    }
    state->itemsPtr = buffer;
    return n;
}

@end
