/* TIGER: Foundation / CoreFoundation entry points that Mac OS X 10.4 lacks.
   Declared in compat/include/TigerCompat/NSCompat.h. Survey: NSCOMPAT-SURVEY.md.

   MRR, fragile runtime. Built into libtigercompat.a by compat/Makefile
   (which globs *.m). Sibling files: nscompat-maptable.m, nscompat-operation.m. */

#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <TigerCompat/NSCompat.h>

#include <sys/sysctl.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <uuid/uuid.h>

/* =========================================================================
 * CoreFoundation
 * ========================================================================= */

/* CFAutorelease, 10.9+. Apple's implementation is this line: CF objects are
   toll-free bridged far enough to take -autorelease, which is already true on
   Tiger. Declared in <TigerCompat/CFCompat.h>. */
CFTypeRef CFAutorelease(CFTypeRef object)
{
    return (CFTypeRef)[(id)object autorelease];
}

/* 10.5+. Tiger's CFNotificationCenter never posts this; the name matches the
   real one so that a Tiger binary run on 10.5+ would still line up. */
const CFStringRef kCFTimeZoneSystemTimeZoneDidChangeNotification =
    (CFStringRef)@"kCFTimeZoneSystemTimeZoneDidChangeNotification";

/* 10.5+ name for Tiger's kCFLocaleCollationIdentifier. */
const CFStringRef kCFLocaleCollatorIdentifier = (CFStringRef)@"collation";

/* =========================================================================
 * Fast enumeration (10.5)
 *
 * The index-based version is used for NSArray, which can be walked without
 * allocating. Everything else goes through -objectEnumerator / -keyEnumerator.
 *
 * ponytail: the enumerator is kept alive by the caller's autorelease pool, not
 * retained in the state struct. Breaking out of a for-in loop therefore leaks
 * nothing, but draining a pool *inside* the loop body would invalidate it.
 * Upgrade path if that ever bites: retain into extra[0] and release on
 * exhaustion, accepting a leak on early break.
 * ========================================================================= */

/* clang emits a call to this whenever a for-in loop notices *state->mutationsPtr
 * changed. Tiger's libobjc has neither this nor the handler setter. Ours always
 * points mutationsPtr at a word we never touch, so this should be unreachable;
 * it exists so that the compiler-emitted call resolves. */
static void (*tigerEnumerationMutationHandler)(id);

void objc_setEnumerationMutationHandler(void (*handler)(id))
{
    tigerEnumerationMutationHandler = handler;
}

void objc_enumerationMutation(id object)
{
    if (tigerEnumerationMutationHandler) {
        tigerEnumerationMutationHandler(object);
        return;
    }
    [NSException raise:NSGenericException
                format:@"*** Collection <%@: %p> was mutated while being enumerated.",
                       NSStringFromClass([object class]), object];
}

#define TIGER_FAST_ENUM_FROM(enumeratorExpr)                                   \
    do {                                                                       \
        if (!state->state) {                                                   \
            state->state = 1;                                                  \
            state->mutationsPtr = &state->extra[4];                            \
            state->extra[0] = (unsigned long)(enumeratorExpr);                 \
        }                                                                      \
        NSEnumerator *e = (NSEnumerator *)state->extra[0];                     \
        NSUInteger n = 0;                                                      \
        id obj;                                                                \
        while (n < len && (obj = [e nextObject]) != nil)                       \
            buffer[n++] = obj;                                                 \
        state->itemsPtr = buffer;                                              \
        return n;                                                              \
    } while (0)

@implementation NSArray (TigerFastEnumeration)
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len
{
    NSUInteger total = [self count];
    NSUInteger i = (NSUInteger)state->state;
    if (!state->mutationsPtr)
        state->mutationsPtr = &state->extra[4];
    NSUInteger n = 0;
    while (n < len && i < total)
        buffer[n++] = [self objectAtIndex:i++];
    state->state = (unsigned long)i;
    state->itemsPtr = buffer;
    return n;
}
@end

@implementation NSSet (TigerFastEnumeration)
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len
{
    TIGER_FAST_ENUM_FROM([self objectEnumerator]);
}
@end

/* for-in over a dictionary walks its keys, matching 10.5+. */
@implementation NSDictionary (TigerFastEnumeration)
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len
{
    TIGER_FAST_ENUM_FROM([self keyEnumerator]);
}
@end

@implementation NSEnumerator (TigerFastEnumeration)
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(__unsafe_unretained id *)buffer
                                    count:(NSUInteger)len
{
    TIGER_FAST_ENUM_FROM(self);
}
@end

/* =========================================================================
 * Subscripting
 * ========================================================================= */

@implementation NSArray (TigerSubscripting)
- (id)objectAtIndexedSubscript:(NSUInteger)index { return [self objectAtIndex:index]; }
@end

@implementation NSMutableArray (TigerSubscripting)
- (void)setObject:(id)object atIndexedSubscript:(NSUInteger)index
{
    if (index == [self count])
        [self addObject:object];
    else
        [self replaceObjectAtIndex:index withObject:object];
}
@end

@implementation NSDictionary (TigerSubscripting)
- (id)objectForKeyedSubscript:(id)key { return [self objectForKey:key]; }
@end

@implementation NSMutableDictionary (TigerSubscripting)
- (void)setObject:(id)object forKeyedSubscript:(id)key
{
    if (object)
        [self setObject:object forKey:key];
    else
        [self removeObjectForKey:key];
}
@end

/* =========================================================================
 * NSArray / NSDictionary / NSSet / NSIndexSet
 * ========================================================================= */

@implementation NSArray (TigerCompat)

- (id)firstObject
{
    return [self count] ? [self objectAtIndex:0] : nil;
}

#if TIGER_NS_BLOCKS

- (void)enumerateObjectsUsingBlock:(void (^)(id, NSUInteger, BOOL *))block
{
    [self enumerateObjectsWithOptions:0 usingBlock:block];
}

/* NSEnumerationConcurrent is honoured as "run them in order"; correct, just not
 * parallel. NSEnumerationReverse is real. */
- (void)enumerateObjectsWithOptions:(NSEnumerationOptions)opts
                         usingBlock:(void (^)(id, NSUInteger, BOOL *))block
{
    NSUInteger count = [self count];
    BOOL stop = NO;
    if (opts & NSEnumerationReverse) {
        for (NSUInteger i = count; i-- > 0 && !stop;)
            block([self objectAtIndex:i], i, &stop);
    } else {
        for (NSUInteger i = 0; i < count && !stop; ++i)
            block([self objectAtIndex:i], i, &stop);
    }
}

- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(id, NSUInteger, BOOL *))predicate
{
    NSUInteger count = [self count];
    BOOL stop = NO;
    for (NSUInteger i = 0; i < count && !stop; ++i) {
        if (predicate([self objectAtIndex:i], i, &stop))
            return i;
    }
    return NSNotFound;
}

- (NSIndexSet *)indexesOfObjectsPassingTest:(BOOL (^)(id, NSUInteger, BOOL *))predicate
{
    NSMutableIndexSet *result = [NSMutableIndexSet indexSet];
    NSUInteger count = [self count];
    BOOL stop = NO;
    for (NSUInteger i = 0; i < count && !stop; ++i) {
        if (predicate([self objectAtIndex:i], i, &stop))
            [result addIndex:i];
    }
    return result;
}

static int tigerComparatorTrampoline(id a, id b, void *context)
{
    return (int)((NSComparator)context)(a, b);
}

- (NSArray *)sortedArrayUsingComparator:(NSComparator)cmptr
{
    return [self sortedArrayUsingFunction:tigerComparatorTrampoline context:(void *)cmptr];
}

#endif /* TIGER_NS_BLOCKS */
@end

#if TIGER_NS_BLOCKS

@implementation NSMutableArray (TigerCompat)
- (void)sortUsingComparator:(NSComparator)cmptr
{
    [self sortUsingFunction:tigerComparatorTrampoline context:(void *)cmptr];
}
@end

@implementation NSDictionary (TigerCompatBlocks)
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(id, id, BOOL *))block
{
    BOOL stop = NO;
    NSEnumerator *keys = [self keyEnumerator];
    id key;
    while (!stop && (key = [keys nextObject]) != nil)
        block(key, [self objectForKey:key], &stop);
}
@end

@implementation NSSet (TigerCompatBlocks)
- (void)enumerateObjectsUsingBlock:(void (^)(id, BOOL *))block
{
    BOOL stop = NO;
    NSEnumerator *objects = [self objectEnumerator];
    id object;
    while (!stop && (object = [objects nextObject]) != nil)
        block(object, &stop);
}
@end

@implementation NSIndexSet (TigerCompat)
- (void)enumerateIndexesUsingBlock:(void (^)(NSUInteger, BOOL *))block
{
    BOOL stop = NO;
    for (NSUInteger i = [self firstIndex]; i != NSNotFound && !stop; i = [self indexGreaterThanIndex:i])
        block(i, &stop);
}
@end

#endif /* TIGER_NS_BLOCKS */

/* =========================================================================
 * NSString
 * ========================================================================= */

@implementation NSString (TigerCompat)

- (BOOL)containsString:(NSString *)str
{
    return [self rangeOfString:str].location != NSNotFound;
}

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement
{
    return [self stringByReplacingOccurrencesOfString:target
                                           withString:replacement
                                              options:0
                                                range:NSMakeRange(0, [self length])];
}

- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)target
                                        withString:(NSString *)replacement
                                           options:(NSStringCompareOptions)options
                                             range:(NSRange)searchRange
{
    NSMutableString *result = [[self mutableCopy] autorelease];
    [result replaceOccurrencesOfString:target
                            withString:replacement
                               options:(unsigned)options
                                 range:searchRange];
    return result;
}

/* Tiger has -rangeOfString:options:range:. The locale argument only matters for
 * NSCaseInsensitiveSearch in Turkish and the like; ignoring it is the documented
 * behaviour when locale is nil, and every WebKit caller passes nil or the
 * current locale. */
- (NSRange)rangeOfString:(NSString *)str
                 options:(NSStringCompareOptions)options
                   range:(NSRange)range
                  locale:(NSLocale *)locale
{
    (void)locale;
    return [self rangeOfString:str options:(unsigned)options range:range];
}

@end

/* =========================================================================
 * NSData
 * ========================================================================= */

@implementation NSData (TigerCompat)

/* Base64 arrived in Foundation in 10.9; Tiger has nothing to build on. The line
 * wrapping options are honoured because the one non-test caller feeds the
 * result into a data: URL, where a stray newline would corrupt it, and getting
 * that wrong would be silent. */
- (NSString *)base64EncodedStringWithOptions:(NSDataBase64EncodingOptions)options
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const unsigned char *bytes = (const unsigned char *)[self bytes];
    NSUInteger length = [self length];

    NSUInteger lineLength = 0;
    if (options & NSDataBase64Encoding64CharacterLineLength)
        lineLength = 64;
    else if (options & NSDataBase64Encoding76CharacterLineLength)
        lineLength = 76;

    const char *lineEnding = "\r\n";
    if (lineLength) {
        BOOL cr = (options & NSDataBase64EncodingEndLineWithCarriageReturn) != 0;
        BOOL lf = (options & NSDataBase64EncodingEndLineWithLineFeed) != 0;
        if (cr && !lf)
            lineEnding = "\r";
        else if (lf && !cr)
            lineEnding = "\n";
    }

    NSMutableString *result = [NSMutableString stringWithCapacity:((length + 2) / 3) * 4];
    char quad[4];
    NSUInteger sinceBreak = 0;

    for (NSUInteger i = 0; i < length; i += 3) {
        NSUInteger remaining = length - i;
        unsigned long triple = (unsigned long)bytes[i] << 16;
        if (remaining > 1)
            triple |= (unsigned long)bytes[i + 1] << 8;
        if (remaining > 2)
            triple |= (unsigned long)bytes[i + 2];

        quad[0] = alphabet[(triple >> 18) & 0x3f];
        quad[1] = alphabet[(triple >> 12) & 0x3f];
        quad[2] = remaining > 1 ? alphabet[(triple >> 6) & 0x3f] : '=';
        quad[3] = remaining > 2 ? alphabet[triple & 0x3f] : '=';

        if (lineLength && sinceBreak == lineLength) {
            [result appendFormat:@"%s", lineEnding];
            sinceBreak = 0;
        }
        [result appendString:[NSString stringWithCString:quad length:4]];
        sinceBreak += 4;
    }

    return result;
}

@end

#if TIGER_NS_BLOCKS
@implementation NSData (TigerCompatBlocks)

/* Tiger has only -initWithBytesNoCopy:length:freeWhenDone:, which always frees
 * with free(), while a deallocator block may free some other way. NSData is a
 * class cluster, so there is nowhere to hang the block: copy the bytes and run
 * the deallocator now. The contract is "called when the NSData no longer needs
 * the bytes", and after a copy that is immediately.
 *
 * ponytail: one extra copy per call. If it shows up in a profile, hang the
 * block off the instance with objc2compat's objc_setAssociatedObject. */
- (id)initWithBytesNoCopy:(void *)bytes
                   length:(NSUInteger)length
              deallocator:(void (^)(void *, NSUInteger))deallocator
{
    id result = [self initWithBytes:bytes length:length];
    if (deallocator)
        deallocator(bytes, length);
    return result;
}

@end
#endif

/* =========================================================================
 * NSNumber
 * ========================================================================= */

@implementation NSNumber (TigerCompat)
+ (NSNumber *)numberWithInteger:(NSInteger)value { return [self numberWithInt:(int)value]; }
+ (NSNumber *)numberWithUnsignedInteger:(NSUInteger)value { return [self numberWithUnsignedInt:(unsigned)value]; }
- (NSInteger)integerValue { return (NSInteger)[self intValue]; }
- (NSUInteger)unsignedIntegerValue { return (NSUInteger)[self unsignedIntValue]; }
@end

/* =========================================================================
 * NSThread
 * ========================================================================= */

static NSThread *tigerMainThread;

/* Recorded from whichever thread first touches Foundation. In a WebKit process
 * that is the thread that runs main(), because +load and static initialisers
 * run there. */
static void tigerCaptureMainThread(void) __attribute__((constructor));
static void tigerCaptureMainThread(void)
{
    tigerMainThread = [[NSThread currentThread] retain];
}

@implementation NSThread (TigerCompat)

+ (NSThread *)mainThread
{
    return tigerMainThread;
}

+ (BOOL)isMainThread
{
    return pthread_main_np() != 0;
}

- (BOOL)isMainThread
{
    return self == tigerMainThread;
}

@end

/* =========================================================================
 * NSProcessInfo
 * ========================================================================= */

static long tigerSysctlLong(const char *name, long fallback)
{
    long value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, NULL, 0) != 0)
        return fallback;
    return value;
}

@implementation NSProcessInfo (TigerCompat)

/* Read from SystemVersion.plist rather than assuming 10.4.11: the same binary
 * has to answer correctly if it is ever run on a later system. */
- (NSOperatingSystemVersion)operatingSystemVersion
{
    NSOperatingSystemVersion version = { 10, 4, 0 };
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:
        @"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *string = [plist objectForKey:@"ProductVersion"];
    if ([string isKindOfClass:[NSString class]]) {
        NSArray *parts = [string componentsSeparatedByString:@"."];
        NSUInteger count = [parts count];
        if (count > 0) version.majorVersion = [[parts objectAtIndex:0] intValue];
        if (count > 1) version.minorVersion = [[parts objectAtIndex:1] intValue];
        if (count > 2) version.patchVersion = [[parts objectAtIndex:2] intValue];
    }
    return version;
}

- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version
{
    NSOperatingSystemVersion mine = [self operatingSystemVersion];
    if (mine.majorVersion != version.majorVersion)
        return mine.majorVersion > version.majorVersion;
    if (mine.minorVersion != version.minorVersion)
        return mine.minorVersion > version.minorVersion;
    return mine.patchVersion >= version.patchVersion;
}

- (NSUInteger)processorCount
{
    return (NSUInteger)tigerSysctlLong("hw.ncpu", 1);
}

- (NSUInteger)activeProcessorCount
{
    return (NSUInteger)tigerSysctlLong("hw.activecpu", tigerSysctlLong("hw.ncpu", 1));
}

- (unsigned long long)physicalMemory
{
    uint64_t bytes = 0;
    size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, NULL, 0) == 0)
        return bytes;
    /* Tiger/i386 has hw.memsize, but fall back to the 32-bit name anyway. */
    return (unsigned long long)(unsigned long)tigerSysctlLong("hw.physmem", 0);
}

@end

/* =========================================================================
 * NSLocale
 * ========================================================================= */

@implementation NSLocale (TigerCompat)

+ (NSArray *)preferredLanguages
{
    NSArray *languages = [[NSUserDefaults standardUserDefaults] objectForKey:@"AppleLanguages"];
    if ([languages isKindOfClass:[NSArray class]] && [languages count])
        return languages;
    return [NSArray arrayWithObject:@"en"];
}

+ (NSLocale *)localeWithLocaleIdentifier:(NSString *)ident
{
    return [[[NSLocale alloc] initWithLocaleIdentifier:ident] autorelease];
}

- (NSString *)languageCode { return [self objectForKey:NSLocaleLanguageCode]; }
- (NSString *)scriptCode   { return [self objectForKey:NSLocaleScriptCode]; }
- (NSString *)countryCode  { return [self objectForKey:NSLocaleCountryCode]; }

@end

/* =========================================================================
 * NSFileManager
 *
 * Tiger's handler-based API never reports why it failed, so the NSError out
 * parameters get a generic NSPOSIXErrorDomain error built from errno.
 * ========================================================================= */

static BOOL tigerFileError(NSError **error, NSString *path)
{
    if (error) {
        int code = errno ? errno : EIO;
        NSDictionary *info = path ? [NSDictionary dictionaryWithObject:path forKey:NSFilePathErrorKey] : nil;
        *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:info];
    }
    return NO;
}

@implementation NSFileManager (TigerCompat)

- (NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError **)error
{
    NSArray *contents = [self directoryContentsAtPath:path];
    if (!contents) {
        tigerFileError(error, path);
        return nil;
    }
    return contents;
}

- (NSDictionary *)attributesOfItemAtPath:(NSString *)path error:(NSError **)error
{
    NSDictionary *attributes = [self fileAttributesAtPath:path traverseLink:NO];
    if (!attributes) {
        tigerFileError(error, path);
        return nil;
    }
    return attributes;
}

- (BOOL)setAttributes:(NSDictionary *)attributes ofItemAtPath:(NSString *)path error:(NSError **)error
{
    if ([self changeFileAttributes:attributes atPath:path])
        return YES;
    return tigerFileError(error, path);
}

- (BOOL)createDirectoryAtPath:(NSString *)path
  withIntermediateDirectories:(BOOL)createIntermediates
                   attributes:(NSDictionary *)attributes
                        error:(NSError **)error
{
    if (!createIntermediates) {
        if ([self createDirectoryAtPath:path attributes:attributes])
            return YES;
        return tigerFileError(error, path);
    }

    NSString *built = nil;
    NSEnumerator *components = [[path pathComponents] objectEnumerator];
    NSString *component;
    while ((component = [components nextObject]) != nil) {
        built = built ? [built stringByAppendingPathComponent:component] : component;
        BOOL isDirectory = NO;
        if ([self fileExistsAtPath:built isDirectory:&isDirectory]) {
            if (isDirectory)
                continue;
            errno = ENOTDIR;
            return tigerFileError(error, built);
        }
        if (![self createDirectoryAtPath:built attributes:attributes])
            return tigerFileError(error, built);
    }
    return YES;
}

- (BOOL)createDirectoryAtURL:(NSURL *)url
 withIntermediateDirectories:(BOOL)createIntermediates
                  attributes:(NSDictionary *)attributes
                       error:(NSError **)error
{
    return [self createDirectoryAtPath:[url path]
           withIntermediateDirectories:createIntermediates
                            attributes:attributes
                                 error:error];
}

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError **)error
{
    if ([self removeFileAtPath:path handler:nil])
        return YES;
    return tigerFileError(error, path);
}

- (BOOL)removeItemAtURL:(NSURL *)url error:(NSError **)error
{
    return [self removeItemAtPath:[url path] error:error];
}

- (BOOL)moveItemAtPath:(NSString *)src toPath:(NSString *)dst error:(NSError **)error
{
    if ([self movePath:src toPath:dst handler:nil])
        return YES;
    return tigerFileError(error, src);
}

- (BOOL)copyItemAtPath:(NSString *)src toPath:(NSString *)dst error:(NSError **)error
{
    if ([self copyPath:src toPath:dst handler:nil])
        return YES;
    return tigerFileError(error, src);
}

@end

/* =========================================================================
 * NSURL
 * ========================================================================= */

NSString *const NSURLIsExcludedFromBackupKey = @"NSURLIsExcludedFromBackupKey";

@implementation NSURL (TigerCompat)

/* Tiger's +fileURLWithPath: stats the path to decide whether to add a trailing
 * slash; the isDirectory: form is told instead, which also works for paths that
 * do not exist yet. */
+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDir
{
    return [self fileURLWithPath:path isDirectory:isDir relativeToURL:nil];
}

+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)isDir relativeToURL:(NSURL *)base
{
    NSString *adjusted = path;
    if (isDir && [path length] && ![path hasSuffix:@"/"])
        adjusted = [path stringByAppendingString:@"/"];
    if (!base)
        return [self fileURLWithPath:adjusted];
    return [[[NSURL alloc] initWithString:adjusted relativeToURL:base] autorelease];
}

- (NSURL *)URLByAppendingPathComponent:(NSString *)component
{
    return [NSURL fileURLWithPath:[[self path] stringByAppendingPathComponent:component]];
}

- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)isDir
{
    return [NSURL fileURLWithPath:[[self path] stringByAppendingPathComponent:component]
                      isDirectory:isDir];
}

- (NSURL *)URLByAppendingPathExtension:(NSString *)ext
{
    return [NSURL fileURLWithPath:[[self path] stringByAppendingPathExtension:ext]];
}

- (NSURL *)URLByDeletingLastPathComponent
{
    return [NSURL fileURLWithPath:[[self path] stringByDeletingLastPathComponent]];
}

- (NSURL *)URLByDeletingPathExtension
{
    return [NSURL fileURLWithPath:[[self path] stringByDeletingPathExtension]];
}

- (NSURL *)URLByStandardizingPath
{
    return [NSURL fileURLWithPath:[[self path] stringByStandardizingPath]];
}

- (NSURL *)URLByResolvingSymlinksInPath
{
    return [NSURL fileURLWithPath:[[self path] stringByResolvingSymlinksInPath]];
}

/* Tiger has no resource-value store. Reporting nil with success matches what
 * callers do with an unknown key, and the only WebKit writer is the
 * NSURLIsExcludedFromBackupKey flag, which has no meaning on this system. */
- (BOOL)getResourceValue:(id *)value forKey:(NSString *)key error:(NSError **)error
{
    (void)key; (void)error;
    if (value)
        *value = nil;
    return YES;
}

- (BOOL)setResourceValue:(id)value forKey:(NSString *)key error:(NSError **)error
{
    (void)value; (void)key; (void)error;
    return YES;
}

@end

/* =========================================================================
 * NSUUID (10.8)
 *
 * Tiger's libSystem exports the whole uuid_* family, including
 * uuid_generate_random, uuid_unparse_upper and a uuid_parse that rejects
 * malformed input, so there is nothing to reimplement here.
 * ========================================================================= */

@implementation NSUUID

+ (id)UUID
{
    return [[[self alloc] init] autorelease];
}

- (id)init
{
    if ((self = [super init]))
        uuid_generate_random(_uuid);
    return self;
}

- (id)initWithUUIDString:(NSString *)string
{
    if (!(self = [super init]))
        return nil;
    if (!string || uuid_parse((char *)[string UTF8String], _uuid) != 0) {
        [self release];
        return nil;
    }
    return self;
}

- (id)initWithUUIDBytes:(const unsigned char *)bytes
{
    if (!(self = [super init]))
        return nil;
    if (!bytes) {
        [self release];
        return nil;
    }
    memcpy(_uuid, bytes, sizeof(_uuid));
    return self;
}

- (void)getUUIDBytes:(unsigned char *)uuid
{
    memcpy(uuid, _uuid, sizeof(_uuid));
}

- (NSString *)UUIDString
{
    /* Real NSUUID renders uppercase. */
    char buffer[37];
    uuid_unparse_upper(_uuid, buffer);
    return [NSString stringWithUTF8String:buffer];
}

- (id)copyWithZone:(NSZone *)zone
{
    (void)zone;
    return [self retain];   /* immutable */
}

- (BOOL)isEqual:(id)other
{
    if (self == other)
        return YES;
    if (![other isKindOfClass:[NSUUID class]])
        return NO;
    unsigned char theirs[16];
    [(NSUUID *)other getUUIDBytes:theirs];
    return memcmp(_uuid, theirs, sizeof(_uuid)) == 0;
}

- (NSUInteger)hash
{
    NSUInteger h = 0;
    for (unsigned i = 0; i < sizeof(_uuid); ++i)
        h = h * 31 + _uuid[i];
    return h;
}

- (NSString *)description
{
    return [self UUIDString];
}

@end
