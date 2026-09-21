/* fndbehaviour.m -- Foundation on Tiger versus modern, measured side by side.
 *
 * Same dual-build differential method as spike/cgbehaviour.c: one source, built
 * for Tiger and for this Mac, printing identical KEY=value lines, diffed by
 * spike/run-fndbehaviour.sh. Modern Foundation is the reference, so a divergence
 * is a measurement rather than my opinion about what Tiger should do.
 *
 * Every divergence falls into one of three buckets, and the report says which:
 *   - Tiger Foundation behaviour, which WebCore has to tolerate or gate
 *   - an nscompat shim bug, which goes back to that track
 *   - expected absence, where Tiger simply has no such API
 *
 * Strings are printed as UTF-8 hex, never raw, so the diff is exact and nothing
 * depends on what survives an ssh pipe.
 *
 * Tiger:  see spike/run-fndbehaviour.sh
 * Host:   clang -fobjc-arc=0 -o build/fndbehaviour-host spike/fndbehaviour.m -framework Foundation
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if TIGER
#import <TigerCompat/NSCompat.h>
#endif

/* ------------------------------------------------------------------ output */

static void kvs(const char *key, const char *val) { printf("%s=%s\n", key, val); }
static void kvi(const char *key, long long v) { printf("%s=%lld\n", key, v); }

/* A string as its UTF-8 bytes in hex, plus its length in UTF-16 units. nil and
   empty are distinguished, because several of these APIs answer with one where
   the other would be expected. */
static void kvstr(const char *key, NSString *s)
{
    if (!s) { printf("%s=nil\n", key); return; }
    const char *u = [s UTF8String];
    printf("%s=len%lu:", key, (unsigned long)[s length]);
    if (!u) { printf("nonutf8\n"); return; }
    for (const unsigned char *p = (const unsigned char *)u; *p; ++p)
        printf("%02x", *p);
    printf("\n");
}

static void kvrange(const char *key, NSRange r)
{
    if (r.location == NSNotFound) { printf("%s=notfound\n", key); return; }
    printf("%s=%lu,%lu\n", key, (unsigned long)r.location, (unsigned long)r.length);
}

/* Build a string from explicit UTF-16 units, so no source encoding is involved. */
static NSString *u16(const unichar *units, NSUInteger n)
{
    return [NSString stringWithCharacters:units length:n];
}

/* ----------------------------------------------------------- NSString ----- */

static void probeStringEncodings(void)
{
    /* A non-BMP character as a surrogate pair: U+1D11E MUSICAL SYMBOL G CLEF. */
    const unichar clef[] = { 0xD834, 0xDD1E };
    NSString *s = u16(clef, 2);
    kvstr("str.surrogate.utf8", s);
    kvi("str.surrogate.length", (long long)[s length]);
    kvi("str.surrogate.char0", [s characterAtIndex:0]);
    kvi("str.surrogate.char1", [s characterAtIndex:1]);
    NSData *d = [s dataUsingEncoding:NSUTF8StringEncoding];
    kvi("str.surrogate.utf8bytes", (long long)[d length]);

    /* A lone high surrogate, which is not valid UTF-8 output. */
    const unichar lone[] = { 0xD834 };
    NSString *bad = u16(lone, 1);
    NSData *badData = [bad dataUsingEncoding:NSUTF8StringEncoding];
    kvi("str.lonesurrogate.length", (long long)[bad length]);
    kvs("str.lonesurrogate.utf8data", badData ? "nonnil" : "nil");
    kvi("str.lonesurrogate.utf8len", (long long)[badData length]);
    NSData *lossy = [bad dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:YES];
    kvi("str.lonesurrogate.lossylen", (long long)[lossy length]);

    /* Invalid UTF-8 input: a bare continuation byte, and a truncated sequence. */
    const unsigned char invalid1[] = { 0x41, 0x80, 0x42 };
    NSString *i1 = [[[NSString alloc] initWithBytes:invalid1 length:3
                                           encoding:NSUTF8StringEncoding] autorelease];
    kvstr("str.invalidutf8.continuation", i1);
    const unsigned char invalid2[] = { 0x41, 0xE2, 0x82 };
    NSString *i2 = [[[NSString alloc] initWithBytes:invalid2 length:3
                                           encoding:NSUTF8StringEncoding] autorelease];
    kvstr("str.invalidutf8.truncated", i2);

    /* Latin-1 round trip, and an unmappable character. */
    const unichar euro[] = { 0x20AC };
    NSString *e = u16(euro, 1);
    NSData *l1 = [e dataUsingEncoding:NSISOLatin1StringEncoding];
    kvs("str.euro.latin1", l1 ? "nonnil" : "nil");
    NSData *l1lossy = [e dataUsingEncoding:NSISOLatin1StringEncoding allowLossyConversion:YES];
    kvi("str.euro.latin1lossylen", (long long)[l1lossy length]);
}

static void probeStringFormat(void)
{
    kvstr("fmt.obj", ([NSString stringWithFormat:@"[%@]", @"x"]));
    kvstr("fmt.objnil", ([NSString stringWithFormat:@"[%@]", (NSString *)nil]));
    kvstr("fmt.ld", ([NSString stringWithFormat:@"%ld", (long)-1234567]));
    kvstr("fmt.lu", ([NSString stringWithFormat:@"%lu", (unsigned long)4000000000UL]));
    kvstr("fmt.zu", ([NSString stringWithFormat:@"%zu", (size_t)123456]));
    kvstr("fmt.lf", ([NSString stringWithFormat:@"%lf", 1.5]));
    kvstr("fmt.g", ([NSString stringWithFormat:@"%g", 0.1]));
    kvstr("fmt.f6", ([NSString stringWithFormat:@"%.6f", 1.0 / 3.0]));
    kvstr("fmt.d", ([NSString stringWithFormat:@"%d", -42]));
    kvstr("fmt.x", ([NSString stringWithFormat:@"%x", 0xdeadbeefU]));
    kvstr("fmt.percent", ([NSString stringWithFormat:@"100%%"]));
    kvstr("fmt.c", ([NSString stringWithFormat:@"%c", 'Z']));
    kvstr("fmt.s", ([NSString stringWithFormat:@"%s", "cstr"]));
    kvstr("fmt.p_nonnull", ([NSString stringWithFormat:@"%@", [NSNumber numberWithInt:7]]));
    /* Width and padding, which HTTP date and number formatting lean on. */
    kvstr("fmt.pad", ([NSString stringWithFormat:@"%02d:%02d", 3, 7]));
    kvstr("fmt.widthstar", ([NSString stringWithFormat:@"%*d", 5, 42]));
    /* The length modifiers, swept: a specifier Tiger does not know comes back
       as literal text rather than as an error, which is silent corruption. */
    kvstr("fmt.zd", ([NSString stringWithFormat:@"%zd", (ssize_t)-99]));
    kvstr("fmt.lld", ([NSString stringWithFormat:@"%lld", (long long)-5000000000LL]));
    kvstr("fmt.llu", ([NSString stringWithFormat:@"%llu", (unsigned long long)5000000000ULL]));
    kvstr("fmt.qi", ([NSString stringWithFormat:@"%qi", (long long)-7]));
    kvstr("fmt.qu", ([NSString stringWithFormat:@"%qu", (unsigned long long)7]));
    kvstr("fmt.jd", ([NSString stringWithFormat:@"%jd", (intmax_t)-11]));
    kvstr("fmt.td", ([NSString stringWithFormat:@"%td", (ptrdiff_t)-13]));
    kvstr("fmt.hd", ([NSString stringWithFormat:@"%hd", (int)-17]));
    kvstr("fmt.hhd", ([NSString stringWithFormat:@"%hhd", (int)-19]));
    kvstr("fmt.e", ([NSString stringWithFormat:@"%e", 12345.678]));
    kvstr("fmt.C", ([NSString stringWithFormat:@"%C", (unichar)0x00E9]));
}

/* --------------------------------------------------------------- NSURL ---- */

static void kvurl(const char *prefix, NSURL *u)
{
    char k[128];
    if (!u) { printf("%s.url=nil\n", prefix); return; }
    snprintf(k, sizeof k, "%s.abs", prefix);    kvstr(k, [u absoluteString]);
    snprintf(k, sizeof k, "%s.scheme", prefix); kvstr(k, [u scheme]);
    snprintf(k, sizeof k, "%s.host", prefix);   kvstr(k, [u host]);
    snprintf(k, sizeof k, "%s.port", prefix);   kvstr(k, [[u port] stringValue]);
    snprintf(k, sizeof k, "%s.user", prefix);   kvstr(k, [u user]);
    snprintf(k, sizeof k, "%s.path", prefix);   kvstr(k, [u path]);
    snprintf(k, sizeof k, "%s.query", prefix);  kvstr(k, [u query]);
    snprintf(k, sizeof k, "%s.frag", prefix);   kvstr(k, [u fragment]);
    /* -[NSURL lastPathComponent] and -pathExtension are 10.6. Report the absence
       rather than crashing on it: the Mac port's own call sites go through
       [[url path] lastPathComponent], which is NSString's and is fine on Tiger. */
    snprintf(k, sizeof k, "%s.last", prefix);
    if ([u respondsToSelector:@selector(lastPathComponent)]) kvstr(k, [u lastPathComponent]);
    else kvs(k, "absent");
    snprintf(k, sizeof k, "%s.ext", prefix);
    if ([u respondsToSelector:@selector(pathExtension)]) kvstr(k, [u pathExtension]);
    else kvs(k, "absent");
    /* The route the Mac port actually uses. */
    snprintf(k, sizeof k, "%s.pathlast", prefix);
    kvstr(k, [u path] ? [[u path] lastPathComponent] : nil);
}

static void probeURL(void)
{
    kvurl("url.simple", [NSURL URLWithString:@"http://example.com/a/b.html?q=1#frag"]);
    kvurl("url.userinfo", [NSURL URLWithString:@"http://user:pw@example.com:8080/p"]);
    kvurl("url.ipv6", [NSURL URLWithString:@"http://[::1]:8080/p"]);
    kvurl("url.ipv6full", [NSURL URLWithString:@"http://[2001:db8::1]/"]);
    kvurl("url.pctpath", [NSURL URLWithString:@"http://example.com/a%20b/c%2Fd"]);
    kvurl("url.emptypath", [NSURL URLWithString:@"http://example.com"]);
    kvurl("url.noscheme", [NSURL URLWithString:@"example.com/x"]);
    kvurl("url.spaces_raw", [NSURL URLWithString:@"http://example.com/a b"]);
    kvurl("url.data", [NSURL URLWithString:@"data:text/plain,hi"]);
    kvurl("url.about", [NSURL URLWithString:@"about:blank"]);

    NSURL *base = [NSURL URLWithString:@"http://example.com/dir/page.html"];
    kvurl("url.rel.sibling", [NSURL URLWithString:@"other.html" relativeToURL:base]);
    kvurl("url.rel.absPath", [NSURL URLWithString:@"/root.html" relativeToURL:base]);
    kvurl("url.rel.dotdot", [NSURL URLWithString:@"../up.html" relativeToURL:base]);
    kvurl("url.rel.query", [NSURL URLWithString:@"?x=1" relativeToURL:base]);
    kvurl("url.rel.frag", [NSURL URLWithString:@"#f" relativeToURL:base]);

    kvurl("url.file.space", [NSURL fileURLWithPath:@"/tmp/a b/c.txt"]);
    kvurl("url.file.dir", [NSURL fileURLWithPath:@"/tmp/dir" isDirectory:YES]);
    kvstr("url.file.path", [[NSURL fileURLWithPath:@"/tmp/a b/c.txt"] path]);
}

/* ---------------------------------------------- dates, numbers, locales ---- */

static void probeDatesAndNumbers(void)
{
    NSDate *d = [NSDate dateWithTimeIntervalSince1970:1234567890.0];
    kvi("date.epoch.ti", (long long)[d timeIntervalSince1970]);
    kvi("date.epoch.ref", (long long)[d timeIntervalSinceReferenceDate]);

    /* Fixed locale and zone, or the comparison measures the machine's settings. */
    NSTimeZone *utc = [NSTimeZone timeZoneWithAbbreviation:@"GMT"];
    kvs("tz.gmt", utc ? "nonnil" : "nil");
    kvi("tz.gmt.offset", utc ? (long long)[utc secondsFromGMT] : -1);
    NSTimeZone *pst = [NSTimeZone timeZoneWithName:@"America/Los_Angeles"];
    kvs("tz.named", pst ? "nonnil" : "nil");

    NSCalendarDate *cd = [NSCalendarDate dateWithTimeIntervalSince1970:1234567890.0];
    if (cd) {
        [cd setTimeZone:utc];
        kvstr("date.httpformat",
              [cd descriptionWithCalendarFormat:@"%a, %d %b %Y %H:%M:%S GMT"]);
    }

    NSNumber *n = [NSNumber numberWithInt:42];
    kvstr("num.int.str", [n stringValue]);
    kvstr("num.int.desc", [n description]);
    kvi("num.int.int", [n intValue]);
    NSNumber *dbl = [NSNumber numberWithDouble:1.5];
    kvstr("num.dbl.str", [dbl stringValue]);
    NSNumber *big = [NSNumber numberWithLongLong:9007199254740993LL];
    kvstr("num.ll.str", [big stringValue]);
    kvi("num.cmp", (long long)[[NSNumber numberWithInt:1] compare:[NSNumber numberWithInt:2]]);
    kvi("num.bool.isequal",
        [[NSNumber numberWithBool:YES] isEqual:[NSNumber numberWithInt:1]] ? 1 : 0);
    kvi("str.intValue", (long long)[@"  -42abc" intValue]);
    kvi("str.doubleValue_x1000", (long long)([@"3.5xyz" doubleValue] * 1000));
    kvi("str.intValue.empty", (long long)[@"" intValue]);
}

/* -------------------------------------------------------- collections ----- */

static void probeCollections(void)
{
    NSArray *a = [NSArray arrayWithObjects:@"a", @"b", @"c", nil];
    kvi("arr.count", (long long)[a count]);
    kvstr("arr.first", [a objectAtIndex:0]);
    kvi("arr.indexof", (long long)[a indexOfObject:@"b"]);
    kvi("arr.indexof.missing", (long long)([a indexOfObject:@"z"] == NSNotFound ? -1 : 0));

    /* Fast enumeration order must match insertion order for an array. */
    NSMutableString *order = [NSMutableString string];
    for (NSString *x in a) [order appendString:x];
    kvstr("arr.fastenum.order", order);

    /* Enumeration of a dictionary is unordered, so only the count is comparable. */
    NSDictionary *dict = [NSDictionary dictionaryWithObjectsAndKeys:
                          @"1", @"one", @"2", @"two", @"3", @"three", nil];
    NSUInteger seen = 0;
    for (NSString *k in dict) { (void)k; ++seen; }
    kvi("dict.fastenum.count", (long long)seen);
    kvstr("dict.lookup", [dict objectForKey:@"two"]);
    kvstr("dict.missing", [dict objectForKey:@"nope"]);

    NSMutableArray *m = [NSMutableArray arrayWithObjects:@"x", @"y", nil];
    [m addObject:@"z"];
    [m insertObject:@"w" atIndex:0];
    [m removeObjectAtIndex:2];
    kvstr("arr.mutated", [m componentsJoinedByString:@""]);

    /* Sorting stability and comparator use. */
    NSArray *nums = [NSArray arrayWithObjects:@"10", @"9", @"2", @"1", nil];
    NSArray *sorted = [nums sortedArrayUsingSelector:@selector(compare:)];
    kvstr("arr.sorted.lexical", [sorted componentsJoinedByString:@","]);

    NSSet *set = [NSSet setWithObjects:@"a", @"b", @"a", nil];
    kvi("set.count", (long long)[set count]);
    kvi("set.contains", [set containsObject:@"b"] ? 1 : 0);

    /* Subscripting and the block enumerations, which nscompat adds on Tiger. */
    kvstr("arr.subscript", a[1]);
    kvstr("dict.subscript", dict[@"one"]);
    __block NSUInteger blockSeen = 0;
    [a enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
        (void)obj; (void)idx; (void)stop; ++blockSeen;
    }];
    kvi("arr.enumblock.count", (long long)blockSeen);
    __block NSUInteger stopAt = 0;
    [a enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
        (void)obj; ++stopAt; if (idx == 1) *stop = YES;
    }];
    kvi("arr.enumblock.stop", (long long)stopAt);
    NSArray *byCmp = [nums sortedArrayUsingComparator:^NSComparisonResult(id x, id y) {
        return [x compare:y options:NSNumericSearch];
    }];
    kvstr("arr.sorted.numeric", [byCmp componentsJoinedByString:@","]);
}

/* ------------------------------------------------------- data and uuid ----- */

static void probeDataAndUUID(void)
{
    const unsigned char raw[] = { 0x00, 0x01, 0xfe, 0xff, 0x41 };
    NSData *d = [NSData dataWithBytes:raw length:sizeof raw];
    kvi("data.len", (long long)[d length]);
    kvi("data.isequal", [d isEqualToData:[NSData dataWithBytes:raw length:sizeof raw]] ? 1 : 0);
    NSData *sub = [d subdataWithRange:NSMakeRange(1, 3)];
    kvi("data.sub.len", (long long)[sub length]);
    kvstr("data.base64", [d base64EncodedStringWithOptions:0]);
    /* nscompat shims encoding only, by design, and no WebKit call site decodes.
       Report the absence rather than assuming it. */
    if ([[NSData alloc] respondsToSelector:@selector(initWithBase64EncodedString:options:)]) {
        NSData *back = [[[NSData alloc] initWithBase64EncodedString:@"AAH+/0E=" options:0] autorelease];
        kvi("data.base64.roundtrip", (back && [back isEqualToData:d]) ? 1 : 0);
        NSData *badb64 = [[[NSData alloc] initWithBase64EncodedString:@"!!!!" options:0] autorelease];
        kvs("data.base64.invalid", badb64 ? "nonnil" : "nil");
    } else {
        kvs("data.base64.roundtrip", "absent");
        kvs("data.base64.invalid", "absent");
    }

    NSUUID *u = [[[NSUUID alloc] initWithUUIDString:@"E621E1F8-C36C-495A-93FC-0C247A3E6E5F"] autorelease];
    kvstr("uuid.roundtrip", [u UUIDString]);
    NSUUID *bad = [[[NSUUID alloc] initWithUUIDString:@"not-a-uuid"] autorelease];
    kvs("uuid.invalid", bad ? "nonnil" : "nil");
    NSUUID *r1 = [NSUUID UUID], *r2 = [NSUUID UUID];
    kvi("uuid.random.distinct", [r1 isEqual:r2] ? 0 : 1);
    kvi("uuid.random.len", (long long)[[r1 UUIDString] length]);
    kvi("uuid.equalself", [r1 isEqual:r1] ? 1 : 0);
}

/* ------------------------------------------------------- file manager ----- */

static void probeFileManager(void)
{
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *base = [NSString stringWithFormat:@"/tmp/fndprobe_%d", (int)getpid()];
    NSString *nested = [base stringByAppendingString:@"/a/b/c"];
    NSError *err = nil;
    BOOL ok = [fm createDirectoryAtPath:nested withIntermediateDirectories:YES
                             attributes:nil error:&err];
    kvi("fm.mkdir.intermediates", ok ? 1 : 0);
    kvi("fm.mkdir.err", err ? 1 : 0);

    /* Creating it again must succeed with intermediates, and fail without. */
    err = nil;
    kvi("fm.mkdir.again", [fm createDirectoryAtPath:nested withIntermediateDirectories:YES
                                         attributes:nil error:&err] ? 1 : 0);
    err = nil;
    BOOL noInt = [fm createDirectoryAtPath:nested withIntermediateDirectories:NO
                                attributes:nil error:&err];
    kvi("fm.mkdir.noint.again", noInt ? 1 : 0);
    kvi("fm.mkdir.noint.errcode", err ? (long long)[err code] : -1);

    NSString *f = [base stringByAppendingPathComponent:@"file.txt"];
    [[NSData dataWithBytes:"hello" length:5] writeToFile:f atomically:YES];
    kvi("fm.exists", [fm fileExistsAtPath:f] ? 1 : 0);
    BOOL isDir = NO;
    [fm fileExistsAtPath:base isDirectory:&isDir];
    kvi("fm.exists.isdir", isDir ? 1 : 0);

    err = nil;
    NSDictionary *attrs = [fm attributesOfItemAtPath:f error:&err];
    kvi("fm.attrs.nonnil", attrs ? 1 : 0);
    kvi("fm.attrs.size", attrs ? [[attrs objectForKey:NSFileSize] longLongValue] : -1);
    kvi("fm.attrs.hastype", (attrs && [attrs objectForKey:NSFileType]) ? 1 : 0);
    kvi("fm.attrs.hasmdate", (attrs && [attrs objectForKey:NSFileModificationDate]) ? 1 : 0);

    err = nil;
    NSArray *contents = [fm contentsOfDirectoryAtPath:base error:&err];
    kvi("fm.dir.count", (long long)[contents count]);
    NSArray *sortedContents = [contents sortedArrayUsingSelector:@selector(compare:)];
    kvstr("fm.dir.sorted", [sortedContents componentsJoinedByString:@","]);

    err = nil;
    NSDictionary *missing = [fm attributesOfItemAtPath:@"/tmp/definitely_not_here_xyz" error:&err];
    kvi("fm.attrs.missing.nil", missing ? 0 : 1);
    kvi("fm.attrs.missing.errset", err ? 1 : 0);
    kvi("fm.attrs.missing.errcode", err ? (long long)[err code] : -999);
    kvstr("fm.attrs.missing.domain", err ? [err domain] : nil);

    [fm removeItemAtPath:base error:NULL];
    kvi("fm.removed", [fm fileExistsAtPath:base] ? 0 : 1);
}

static void probeStringSearch(void)
{
    NSString *hay = @"Hello World hello world";
    kvrange("find.plain", [hay rangeOfString:@"world"]);
    kvrange("find.ci", [hay rangeOfString:@"WORLD" options:NSCaseInsensitiveSearch]);
    kvrange("find.backwards", [hay rangeOfString:@"hello" options:NSBackwardsSearch]);
    kvrange("find.ci.backwards", [hay rangeOfString:@"HELLO"
                                             options:NSCaseInsensitiveSearch | NSBackwardsSearch]);
    kvrange("find.literal", [hay rangeOfString:@"World" options:NSLiteralSearch]);
    kvrange("find.empty", [hay rangeOfString:@""]);
    kvrange("find.missing", [hay rangeOfString:@"zzz"]);
    kvrange("find.inrange", [hay rangeOfString:@"hello" options:0 range:NSMakeRange(12, 11)]);
    kvrange("find.anchored", [hay rangeOfString:@"Hello" options:NSAnchoredSearch]);

    /* Numeric comparison, which WebCore uses for version-ish sorting. */
    kvi("cmp.numeric.2v10", (long long)[@"item2" compare:@"item10" options:NSNumericSearch]);
    kvi("cmp.plain.2v10", (long long)[@"item2" compare:@"item10"]);
    kvi("cmp.ci", (long long)[@"ABC" compare:@"abc" options:NSCaseInsensitiveSearch]);
    kvi("cmp.empty", (long long)[@"" compare:@""]);
}

static void probeStringComponents(void)
{
    NSArray *a;
    a = [@"a,b,c" componentsSeparatedByString:@","];
    kvi("comp.simple.count", (long long)[a count]);
    kvstr("comp.simple.0", [a objectAtIndex:0]);
    a = [@",a," componentsSeparatedByString:@","];
    kvi("comp.edges.count", (long long)[a count]);
    kvstr("comp.edges.0", [a objectAtIndex:0]);
    kvstr("comp.edges.2", [a objectAtIndex:2]);
    a = [@"abc" componentsSeparatedByString:@","];
    kvi("comp.nosep.count", (long long)[a count]);
    a = [@"" componentsSeparatedByString:@","];
    kvi("comp.emptystr.count", (long long)[a count]);
    a = [@"a,,b" componentsSeparatedByString:@","];
    kvi("comp.doublesep.count", (long long)[a count]);
    kvi("comp.doublesep.1len", (long long)[[a objectAtIndex:1] length]);
    kvstr("comp.join", [[@"x,y" componentsSeparatedByString:@","] componentsJoinedByString:@"-"]);
}

static void probeStringCaseAndNormalisation(void)
{
    /* e + combining acute, which NFC composes to U+00E9. */
    const unichar decomposed[] = { 0x0065, 0x0301 };
    NSString *d = u16(decomposed, 2);
    kvi("norm.decomposed.length", (long long)[d length]);
    NSString *pre = [d precomposedStringWithCanonicalMapping];
    kvi("norm.precomposed.length", (long long)[pre length]);
    kvstr("norm.precomposed.utf8", pre);

    /* Case mapping outside ASCII: sharp s, accented e, Turkish dotted I. */
    const unichar sharps[] = { 0x00DF };
    kvstr("case.sharps.upper", [u16(sharps, 1) uppercaseString]);
    const unichar eacute[] = { 0x00E9 };
    kvstr("case.eacute.upper", [u16(eacute, 1) uppercaseString]);
    const unichar dotlessI[] = { 0x0131 };
    kvstr("case.dotlessi.upper", [u16(dotlessI, 1) uppercaseString]);
    kvstr("case.ascii.lower", [@"MiXeD" lowercaseString]);
    kvstr("case.ascii.upper", [@"MiXeD" uppercaseString]);

    /* hasPrefix/hasSuffix and trimming, used all over WebCore's URL handling. */
    kvi("str.hasprefix", [@"foobar" hasPrefix:@"foo"] ? 1 : 0);
    kvi("str.hasprefix.empty", [@"foobar" hasPrefix:@""] ? 1 : 0);
    kvi("str.hassuffix", [@"foobar" hasSuffix:@"bar"] ? 1 : 0);
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    kvstr("str.trim", [@"  \t x \n " stringByTrimmingCharactersInSet:ws]);
    kvstr("str.trim.allws", [@" \t\n " stringByTrimmingCharactersInSet:ws]);
    kvstr("str.substring", [@"abcdef" substringWithRange:NSMakeRange(2, 3)]);
    kvstr("str.replace", [@"a.b.c" stringByReplacingOccurrencesOfString:@"." withString:@"-"]);
}

static void probeCharacterSets(void)
{
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    NSCharacterSet *wsonly = [NSCharacterSet whitespaceCharacterSet];
    const unichar probe[] = { 0x20, 0x09, 0x0A, 0x0D, 0x0C, 0x0B, 0xA0, 0x2028, 0x2029, 0x3000, 0x41 };
    char buf[64];
    for (unsigned i = 0; i < sizeof probe / sizeof *probe; ++i) {
        snprintf(buf, sizeof buf, "cset.wsnl.u%04x", probe[i]);
        kvi(buf, [ws characterIsMember:probe[i]] ? 1 : 0);
        snprintf(buf, sizeof buf, "cset.ws.u%04x", probe[i]);
        kvi(buf, [wsonly characterIsMember:probe[i]] ? 1 : 0);
    }
    NSCharacterSet *alnum = [NSCharacterSet alphanumericCharacterSet];
    kvi("cset.alnum.A", [alnum characterIsMember:'A'] ? 1 : 0);
    kvi("cset.alnum.dot", [alnum characterIsMember:'.'] ? 1 : 0);
    kvi("cset.alnum.u00e9", [alnum characterIsMember:0x00E9] ? 1 : 0);
}

/* ------------------------------------------ runloop, timers, notifications - */

static int gTimerFired, gNoteCount;

@interface FndProbe : NSObject
- (void)timerFired:(NSTimer *)t;
- (void)noteFired:(NSNotification *)n;
@end
@implementation FndProbe
- (void)timerFired:(NSTimer *)t { (void)t; ++gTimerFired; }
- (void)noteFired:(NSNotification *)n { (void)n; ++gNoteCount; }
@end

static void probeRunLoopAndNotifications(void)
{
    FndProbe *p = [[[FndProbe alloc] init] autorelease];

    /* WTF's RunLoop leans on the default mode and on common modes being distinct
       but both driven by a plain -runUntilDate:. */
    NSRunLoop *rl = [NSRunLoop currentRunLoop];
    kvs("runloop.current", rl ? "nonnil" : "nil");
    kvstr("runloop.mode.idle", [rl currentMode]);

    gTimerFired = 0;
    NSTimer *t = [NSTimer scheduledTimerWithTimeInterval:0.01 target:p
                                                selector:@selector(timerFired:)
                                                userInfo:nil repeats:NO];
    kvs("timer.scheduled", t ? "nonnil" : "nil");
    kvi("timer.valid.before", [t isValid] ? 1 : 0);
    [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
    kvi("timer.fired.oneshot", gTimerFired);
    kvi("timer.valid.after", [t isValid] ? 1 : 0);

    /* A repeating timer must fire more than once in the same window. */
    gTimerFired = 0;
    NSTimer *rep = [NSTimer scheduledTimerWithTimeInterval:0.02 target:p
                                                  selector:@selector(timerFired:)
                                                  userInfo:nil repeats:YES];
    [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
    [rep invalidate];
    kvi("timer.repeats.atleast3", gTimerFired >= 3 ? 1 : 0);
    kvi("timer.repeats.invalidated", [rep isValid] ? 0 : 1);

    /* A timer added only to a private mode must not fire in the default mode. */
    gTimerFired = 0;
    NSTimer *modal = [NSTimer timerWithTimeInterval:0.01 target:p
                                           selector:@selector(timerFired:)
                                           userInfo:nil repeats:NO];
    [rl addTimer:modal forMode:@"FndProbePrivateMode"];
    [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.15]];
    kvi("timer.privatemode.notfired", gTimerFired == 0 ? 1 : 0);
    [modal invalidate];

    /* Notifications: synchronous delivery, observer removal, object filtering. */
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    gNoteCount = 0;
    [nc addObserver:p selector:@selector(noteFired:) name:@"FndProbeNote" object:nil];
    [nc postNotificationName:@"FndProbeNote" object:nil];
    kvi("note.delivered.sync", gNoteCount);
    [nc postNotificationName:@"FndProbeOther" object:nil];
    kvi("note.othername.ignored", gNoteCount == 1 ? 1 : 0);
    [nc removeObserver:p name:@"FndProbeNote" object:nil];
    [nc postNotificationName:@"FndProbeNote" object:nil];
    kvi("note.after.removal", gNoteCount);

    /* Object filtering: an observer registered for one object ignores another. */
    NSString *objA = @"A", *objB = @"B";
    gNoteCount = 0;
    [nc addObserver:p selector:@selector(noteFired:) name:@"FndProbeObj" object:objA];
    [nc postNotificationName:@"FndProbeObj" object:objB];
    kvi("note.objfilter.other", gNoteCount);
    [nc postNotificationName:@"FndProbeObj" object:objA];
    kvi("note.objfilter.match", gNoteCount);
    [nc removeObserver:p];

    /* userInfo round trip. */
    gNoteCount = 0;
    [nc addObserver:p selector:@selector(noteFired:) name:@"FndProbeInfo" object:nil];
    [nc postNotificationName:@"FndProbeInfo" object:nil
                    userInfo:[NSDictionary dictionaryWithObject:@"v" forKey:@"k"]];
    kvi("note.userinfo.delivered", gNoteCount);
    [nc removeObserver:p];
}

static void probeOperationQueue(void)
{
    NSOperationQueue *q = [[NSOperationQueue alloc] init];
    [q setMaxConcurrentOperationCount:1];
    NSMutableString *order = [NSMutableString string];
    NSLock *lock = [[NSLock alloc] init];
    for (int i = 0; i < 8; ++i) {
        [q addOperationWithBlock:^{
            [lock lock];
            [order appendFormat:@"%d", i];
            [lock unlock];
        }];
    }
    [q waitUntilAllOperationsAreFinished];
    kvstr("opq.serial.order", order);
    kvi("opq.serial.count.after", (long long)[q operationCount]);

    /* A serial queue must preserve submission order; a concurrent one need not,
       so only completeness is comparable there. */
    NSOperationQueue *cq = [[NSOperationQueue alloc] init];
    [cq setMaxConcurrentOperationCount:4];
    __block int ran = 0;
    NSLock *l2 = [[NSLock alloc] init];
    for (int i = 0; i < 16; ++i)
        [cq addOperationWithBlock:^{ [l2 lock]; ++ran; [l2 unlock]; }];
    [cq waitUntilAllOperationsAreFinished];
    kvi("opq.concurrent.allran", ran == 16 ? 1 : 0);
    kvi("opq.concurrent.count.after", (long long)[cq operationCount]);
    [lock release]; [l2 release]; [q release]; [cq release];
}

static void probeLocale(void)
{
    NSLocale *posix = [[[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"] autorelease];
    kvs("locale.posix", posix ? "nonnil" : "nil");
    kvstr("locale.posix.id", [posix localeIdentifier]);
    NSArray *langs = [NSLocale preferredLanguages];
    kvi("locale.preferred.nonempty", [langs count] > 0 ? 1 : 0);
    NSLocale *cur = [NSLocale currentLocale];
    kvs("locale.current", cur ? "nonnil" : "nil");
}

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    setbuf(stdout, NULL);
#if TIGER
    kvs("platform", "tiger");
#else
    kvs("platform", "host");
#endif
    probeStringEncodings();
    probeStringFormat();
    probeStringSearch();
    probeStringComponents();
    probeStringCaseAndNormalisation();
    probeCharacterSets();
    probeURL();
    probeDatesAndNumbers();
    probeCollections();
    probeDataAndUUID();
    probeFileManager();
    probeRunLoopAndNotifications();
    probeOperationQueue();
    probeLocale();
    [pool release];
    return 0;
}
