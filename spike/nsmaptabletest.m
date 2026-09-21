/* NSMapTable (compat/nscompat-maptable.m) on Tiger.

   compat/sdk-overlay renames Tiger's colliding NSMapTable C typedef to
   NSMapTableCStruct, so both the class and the C API are usable here with no
   tricks. Build with the overlay on the framework search path:

     spike/run-nsmaptabletest.sh */

#import <TigerCompat/FoundationCompat.h>

#include <stdio.h>

static int failures;
static void expect(const char *name, BOOL ok)
{
    printf("%-44s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

int main(void)
{
    setbuf(stdout, NULL);
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    /* What JSC's JSVirtualMachine asks for: identity keys, retained values. */
    NSMapTable *table = [[NSMapTable alloc]
        initWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
              valueOptions:NSPointerFunctionsStrongMemory
                  capacity:0];

    NSObject *k1 = [[NSObject alloc] init];
    NSObject *k2 = [[NSObject alloc] init];
    [table setObject:@"one" forKey:k1];
    [table setObject:@"two" forKey:k2];

    expect("count", [table count] == 2);
    expect("objectForKey", [[table objectForKey:k1] isEqualToString:@"one"]);
    expect("missing key is nil", [table objectForKey:[NSObject class]] == nil);

    /* Identity, not -isEqual:: two equal strings must be distinct keys. */
    NSMapTable *identity = [NSMapTable strongToStrongObjectsMapTable];
    NSString *a = [NSMutableString stringWithString:@"same"];
    NSString *b = [NSMutableString stringWithString:@"same"];
    [identity setObject:@1 forKey:a];
    [identity setObject:@2 forKey:b];
    expect("object personality hashes by -hash", [identity count] == 1);

    NSMapTable *pointers = [[NSMapTable alloc]
        initWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
              valueOptions:NSPointerFunctionsStrongMemory
                  capacity:0];
    [pointers setObject:@1 forKey:a];
    [pointers setObject:@2 forKey:b];
    expect("pointer personality hashes by address", [pointers count] == 2);

    int seen = 0;
    NSEnumerator *keys = [table keyEnumerator];
    while ([keys nextObject])
        ++seen;
    expect("keyEnumerator", seen == 2);

    seen = 0;
    for (id key in table) {
        (void)key;
        ++seen;
    }
    expect("for-in yields keys", seen == 2);

    [table setObject:nil forKey:k1];
    expect("setObject:nil removes", [table count] == 1);
    [table removeObjectForKey:k2];
    expect("removeObjectForKey", [table count] == 0);

    [table setObject:@"x" forKey:k1];
    [table removeAllObjects];
    expect("removeAllObjects", [table count] == 0);

    /* What JSC's JSVirtualMachine/JSWrapperMap really ask for: an opaque personality whose keys
       are JSContextGroupRef/JSGlobalContextRef, not objects. Sending -hash to one of those is a
       crash, so this table must hash on the address. */
    NSMapTable *opaque = [[NSMapTable alloc]
        initWithKeyOptions:NSPointerFunctionsOpaquePersonality | NSPointerFunctionsOpaqueMemory
              valueOptions:NSPointerFunctionsStrongMemory
                  capacity:0];
    struct { char pad[8]; } notAnObject1, notAnObject2;
    [opaque setObject:@"a" forKey:(id)&notAnObject1];
    [opaque setObject:@"b" forKey:(id)&notAnObject2];
    expect("opaque personality holds non-objects", [opaque count] == 2);
    expect("opaque personality hashes by address",
           [[opaque objectForKey:(id)&notAnObject2] isEqualToString:@"b"]);
    /* No enumeration test on purpose: -keyEnumerator retains its elements, so enumerating a
       non-object-keyed table is unsupported here exactly as it is in real Foundation. */
    [opaque release];

    [table release];
    [pointers release];
    [k1 release];
    [k2 release];

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    [pool drain];
    return failures ? 1 : 0;
}
