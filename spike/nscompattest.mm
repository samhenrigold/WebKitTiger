// Exercises compat/nscompat*.m on Tiger. Builds in both MRR and ARC:
//   NOARC=1 spike/run.sh spike/nscompattest.mm -ltigerdispatch
//           spike/run.sh spike/nscompattest.mm -ltigerdispatch
// Prints PASS/FAIL per feature and exits non-zero if anything failed.

#import <Foundation/Foundation.h>
#import <TigerCompat/NSCompat.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static int failures;

static void expect(const char *name, bool ok)
{
    std::printf("%-44s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++failures;
}

int main(int, char **)
{
    @autoreleasepool {

    // ---- object literals (Tiger already has everything these lower to) ----
    NSArray *three = @[@"a", @"b", @"c"];
    NSDictionary *dict = @{@"one": @1, @"two": @2};
    expect("literals", [three count] == 3 && [dict count] == 2);

    // ---- subscripting ----
    expect("array subscript", [three[1] isEqual:@"b"]);
    expect("dictionary subscript", [dict[@"two"] intValue] == 2);

    NSMutableArray *mutableArray = [NSMutableArray arrayWithArray:three];
    mutableArray[0] = @"z";
    mutableArray[3] = @"d";
    expect("mutable array subscript",
          [mutableArray[0] isEqual:@"z"] && [mutableArray count] == 4);

    NSMutableDictionary *mutableDict = [NSMutableDictionary dictionary];
    mutableDict[@"k"] = @7;
    expect("mutable dict subscript", [mutableDict[@"k"] intValue] == 7);
    mutableDict[@"k"] = nil;
    expect("mutable dict subscript removes", [mutableDict count] == 0);

    // ---- fast enumeration ----
    {
        NSMutableString *joined = [NSMutableString string];
        for (NSString *s in three)
            [joined appendString:s];
        expect("for-in NSArray", [joined isEqualToString:@"abc"]);
    }
    {
        // More elements than one buffer refill, to exercise the state struct.
        NSMutableArray *many = [NSMutableArray array];
        for (int i = 0; i < 100; ++i)
            [many addObject:[NSNumber numberWithInt:i]];
        long sum = 0;
        for (NSNumber *n in many)
            sum += [n intValue];
        expect("for-in NSArray, 100 elements", sum == 4950);
    }
    {
        long sum = 0;
        for (NSNumber *n in [NSSet setWithObjects:@1, @2, @3, nil])
            sum += [n intValue];
        expect("for-in NSSet", sum == 6);
    }
    {
        long sum = 0;
        for (NSString *key in dict)
            sum += [dict[key] intValue];
        expect("for-in NSDictionary yields keys", sum == 3);
    }
    {
        NSMutableString *joined = [NSMutableString string];
        for (NSString *s in [three objectEnumerator])
            [joined appendString:s];
        expect("for-in NSEnumerator", [joined isEqualToString:@"abc"]);
    }
    {
        // break must not corrupt anything that follows
        int seen = 0;
        for (NSString *s in three) {
            (void)s;
            if (++seen == 2)
                break;
        }
        int again = 0;
        for (NSString *s in three) { (void)s; ++again; }
        expect("for-in survives break", seen == 2 && again == 3);
    }

    // ---- NSArray / NSDictionary / NSIndexSet additions ----
    expect("firstObject", [[three firstObject] isEqual:@"a"]);
    expect("firstObject on empty", [[NSArray array] firstObject] == nil);

    {
        __block NSUInteger count = 0;
        [three enumerateObjectsUsingBlock:^(id, NSUInteger, BOOL *) { ++count; }];
        expect("enumerateObjectsUsingBlock", count == 3);
    }
    {
        __block NSUInteger count = 0;
        [three enumerateObjectsUsingBlock:^(id, NSUInteger, BOOL *stop) { ++count; *stop = YES; }];
        expect("enumerateObjectsUsingBlock stop", count == 1);
    }
    {
        NSMutableString *order = [NSMutableString string];
        [three enumerateObjectsWithOptions:NSEnumerationReverse
                                usingBlock:^(id o, NSUInteger, BOOL *) { [order appendString:o]; }];
        expect("enumerate reverse", [order isEqualToString:@"cba"]);
    }
    {
        NSUInteger idx = [three indexOfObjectPassingTest:^BOOL(id o, NSUInteger, BOOL *) {
            return [o isEqual:@"c"];
        }];
        expect("indexOfObjectPassingTest", idx == 2);
    }
    {
        NSIndexSet *set = [three indexesOfObjectsPassingTest:^BOOL(id o, NSUInteger, BOOL *) {
            return ![o isEqual:@"b"];
        }];
        __block NSUInteger total = 0;
        [set enumerateIndexesUsingBlock:^(NSUInteger i, BOOL *) { total += i; }];
        expect("indexesOfObjectsPassingTest", [set count] == 2 && total == 2);
    }
    {
        NSArray *sorted = [@[@3, @1, @2] sortedArrayUsingComparator:^NSComparisonResult(id a, id b) {
            return [a compare:b];
        }];
        expect("sortedArrayUsingComparator",
              [sorted[0] intValue] == 1 && [sorted[2] intValue] == 3);
    }
    {
        __block int sum = 0;
        [dict enumerateKeysAndObjectsUsingBlock:^(id, id v, BOOL *) { sum += [v intValue]; }];
        expect("enumerateKeysAndObjectsUsingBlock", sum == 3);
    }

    // ---- NSString ----
    expect("containsString: yes", [@"hello world" containsString:@"lo wo"]);
    expect("containsString: no", ![@"hello world" containsString:@"xyz"]);
    expect("stringByReplacingOccurrences",
          [[@"a-b-c" stringByReplacingOccurrencesOfString:@"-" withString:@"+"]
              isEqualToString:@"a+b+c"]);
    {
        NSRange r = [@"Hello" rangeOfString:@"ELL"
                                    options:NSCaseInsensitiveSearch
                                      range:NSMakeRange(0, 5)
                                     locale:nil];
        expect("rangeOfString:options:range:locale:", r.location == 1 && r.length == 3);
    }

    // ---- NSNumber ----
    expect("numberWithInteger", [[NSNumber numberWithInteger:-5] integerValue] == -5);
    expect("unsignedIntegerValue",
          [[NSNumber numberWithUnsignedInteger:4000000000U] unsignedIntegerValue] == 4000000000U);

    // ---- NSThread ----
    expect("NSThread +isMainThread", [NSThread isMainThread]);
    expect("NSThread +mainThread", [NSThread mainThread] != nil);
    expect("NSThread -isMainThread", [[NSThread currentThread] isMainThread]);

    // ---- NSProcessInfo ----
    {
        NSProcessInfo *info = [NSProcessInfo processInfo];
        NSOperatingSystemVersion v = [info operatingSystemVersion];
        std::printf("   os %d.%d.%d, %u cpu, %llu MB\n",
                    (int)v.majorVersion, (int)v.minorVersion, (int)v.patchVersion,
                    (unsigned)[info processorCount],
                    [info physicalMemory] >> 20);
        expect("operatingSystemVersion", v.majorVersion == 10 && v.minorVersion >= 4);
        expect("processorCount", [info processorCount] >= 1);
        expect("physicalMemory", [info physicalMemory] > (1ULL << 20));
        NSOperatingSystemVersion tiger = { 10, 4, 0 };
        NSOperatingSystemVersion future = { 10, 9, 0 };
        expect("isOperatingSystemAtLeastVersion",
              [info isOperatingSystemAtLeastVersion:tiger]
              && ![info isOperatingSystemAtLeastVersion:future]);
    }

    // ---- NSLocale ----
    {
        NSArray *languages = [NSLocale preferredLanguages];
        expect("preferredLanguages", [languages count] >= 1);
        NSLocale *us = [NSLocale localeWithLocaleIdentifier:@"en_US"];
        expect("localeWithLocaleIdentifier", us != nil);
        expect("languageCode", [[us languageCode] isEqualToString:@"en"]);
    }

    // ---- NSUUID ----
    {
        NSUUID *a = [NSUUID UUID];
        NSString *s = [a UUIDString];
        expect("UUIDString shape", [s length] == 36 && [s characterAtIndex:8] == '-');
        NSUUID *b = [[NSUUID alloc] initWithUUIDString:s];
        expect("initWithUUIDString round trip", [b isEqual:a] && [a hash] == [b hash]);
        expect("initWithUUIDString rejects junk",
              [[NSUUID alloc] initWithUUIDString:@"not a uuid"] == nil);
        expect("UUIDs differ", ![[[NSUUID UUID] UUIDString] isEqualToString:s]);
        unsigned char bytes[16];
        [a getUUIDBytes:bytes];
        NSUUID *c = [[NSUUID alloc] initWithUUIDBytes:bytes];
        expect("initWithUUIDBytes", [c isEqual:a]);
#if !__has_feature(objc_arc)
        [b release];
        [c release];
#endif
    }

    // ---- NSFileManager ----
    {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *root = @"/tmp/nscompattest.dir";
        NSError *error = nil;
        [fm removeItemAtPath:root error:nil];

        NSString *nested = [root stringByAppendingPathComponent:@"a/b/c"];
        expect("createDirectory withIntermediateDirectories",
              [fm createDirectoryAtPath:nested withIntermediateDirectories:YES
                             attributes:nil error:&error]);

        NSString *file = [root stringByAppendingPathComponent:@"f.txt"];
        [@"hello" writeToFile:file atomically:YES];

        NSArray *contents = [fm contentsOfDirectoryAtPath:root error:&error];
        expect("contentsOfDirectoryAtPath", [contents count] == 2);

        NSDictionary *attributes = [fm attributesOfItemAtPath:file error:&error];
        expect("attributesOfItemAtPath",
              [[attributes objectForKey:NSFileSize] intValue] == 5);

        error = nil;
        expect("contentsOfDirectoryAtPath reports error",
              [fm contentsOfDirectoryAtPath:@"/tmp/definitely-not-here" error:&error] == nil
              && error != nil);

        NSString *moved = [root stringByAppendingPathComponent:@"g.txt"];
        expect("moveItemAtPath", [fm moveItemAtPath:file toPath:moved error:&error]);
        expect("copyItemAtPath", [fm copyItemAtPath:moved toPath:file error:&error]);
        expect("removeItemAtURL",
              [fm removeItemAtURL:[NSURL fileURLWithPath:file] error:&error]);
        expect("removeItemAtPath", [fm removeItemAtPath:root error:&error]);
    }

    // ---- NSURL ----
    {
        NSURL *dir = [NSURL fileURLWithPath:@"/tmp/nscompat" isDirectory:YES];
        expect("fileURLWithPath:isDirectory:", [[dir absoluteString] hasSuffix:@"/"]);
        NSURL *child = [dir URLByAppendingPathComponent:@"file.txt"];
        expect("URLByAppendingPathComponent",
              [[child path] isEqualToString:@"/tmp/nscompat/file.txt"]);
        expect("URLByDeletingLastPathComponent",
              [[[child URLByDeletingLastPathComponent] path] isEqualToString:@"/tmp/nscompat"]);
        expect("URLByAppendingPathExtension",
              [[[child URLByAppendingPathExtension:@"bak"] path] hasSuffix:@".txt.bak"]);
        expect("setResourceValue no-ops",
              [child setResourceValue:[NSNumber numberWithBool:YES]
                               forKey:NSURLIsExcludedFromBackupKey
                                error:nil]);
    }

    // ---- NSData ----
    {
        char *buffer = (char *)malloc(4);
        memcpy(buffer, "abcd", 4);
        __block bool freed = false;
        NSData *data = [[NSData alloc] initWithBytesNoCopy:buffer length:4
                                               deallocator:^(void *p, NSUInteger) {
            free(p);
            freed = true;
        }];
        expect("initWithBytesNoCopy:deallocator:", [data length] == 4 && freed);
#if !__has_feature(objc_arc)
        [data release];
#endif
    }

#if TIGER_NSMAPTABLE_TYPEDEF_RENAMED
    // ---- NSMapTable (only once the SDK overlay has renamed the C typedef) ----
    {
        NSMapTable *table = [[NSMapTable alloc]
            initWithKeyOptions:NSPointerFunctionsObjectPointerPersonality
                  valueOptions:NSPointerFunctionsStrongMemory
                      capacity:0];
        NSObject *key = [[NSObject alloc] init];
        [table setObject:@"value" forKey:key];
        expect("NSMapTable round trip",
              [[table objectForKey:key] isEqual:@"value"] && [table count] == 1);
        [table removeObjectForKey:key];
        expect("NSMapTable remove", [table count] == 0);
    }
#else
    std::printf("%-44s SKIP (SDK overlay has not renamed the C typedef)\n", "NSMapTable");
#endif

    // ---- NSOperationQueue ----
    {
        NSOperationQueue *queue = [[NSOperationQueue alloc] init];
        [queue setMaxConcurrentOperationCount:1];
        __block int counter = 0;
        for (int i = 0; i < 10; ++i)
            [queue addOperationWithBlock:^{ ++counter; }];
        [queue waitUntilAllOperationsAreFinished];
        expect("NSOperationQueue addOperationWithBlock", counter == 10);

        __block bool ran = false;
        [queue addOperation:[NSBlockOperation blockOperationWithBlock:^{ ran = true; }]];
        [queue waitUntilAllOperationsAreFinished];
        expect("NSBlockOperation", ran);

        // A suspended queue must hold work back, not drop it: the operation has to run once the
        // queue resumes, and -waitUntilFinished has to return rather than hang.
        __block bool deferredRan = false;
        NSOperation *deferred = [NSBlockOperation blockOperationWithBlock:^{ deferredRan = true; }];
        [queue setSuspended:YES];
        [queue addOperation:deferred];
        usleep(50000);
        expect("suspended queue defers instead of dropping", !deferredRan && ![deferred isFinished]);
        [queue setSuspended:NO];
        [deferred waitUntilFinished];
        expect("resumed queue runs the deferred operation", deferredRan && [deferred isFinished]);

        // operationCount must reflect queued work rather than answering a flat zero.
        [queue setSuspended:YES];
        NSOperation *pending = [NSBlockOperation blockOperationWithBlock:^{ }];
        [queue addOperation:pending];
        expect("operationCount counts a queued operation", [queue operationCount] == 1);
        [queue setSuspended:NO];
        [queue waitUntilAllOperationsAreFinished];
        usleep(50000);
        expect("operationCount drops back to zero", [queue operationCount] == 0);

#if !__has_feature(objc_arc)
        [queue release];
#endif
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    }
    return failures ? 1 : 0;
}
