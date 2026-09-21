# Foundation / AppKit gap survey for Mac OS X 10.4 (i386)

How the data was produced (reproducible):

- Classes: `logs/api/used-NSclasses.txt` (classes WebKit sends messages to, with counts)
  minus `logs/api/tiger-NSclasses.txt` (classes present in Tiger's binaries).
- Selectors: Tiger's Foundation/AppKit dylibs carry **no** `__OBJC,__meth_var_names`
  section. The ObjC1 method lists (`__OBJC,__inst_meth`, `__cls_meth`,
  `__cat_inst_meth`, `__cat_cls_meth`) hold raw pointers into `__TEXT,__cstring`.
  A ~40-line Mach-O parser (kept at `/tmp/ns/sels.py` during the survey, re-derivable)
  walks those sections and resolves each 4-byte word that lands inside `__cstring`.
  Result: 4096 Foundation selectors, 12043 AppKit selectors.
- WebKit side: every `[... sel:...]` message send in
  `Source/{WTF,JavaScriptCore,WebCore,WebKitLegacy/mac}/**/*.{m,mm,h}` reduced to a
  selector, minus selectors WebKit itself declares, minus the Tiger set.

Caveat: the WebKit-side scan only sees single-line message sends, so counts are a
lower bound. The Tiger side is exact.

## Headline

Tiger's Foundation is in much better shape than expected. The gap is not a long tail
of missing methods, it is a handful of **language-level** features (fast enumeration,
subscripting) plus ~15 real API additions. Almost everything else WebKit sends to an
`NS` object already exists in 10.4.

Things the brief listed that turned out to be **unused** by the non-WebKit2 tree, so
they are not implemented: `NSCache` (0 references, the 20 apparent hits were
`NSCachedURLResponse` / `NSURLCache`), `NSHashTable` (0), `NSPointerArray` (0),
`NSRegularExpression` (0), `NSJSONSerialization` (3 files, all
ApplePay / FairPlay, WebCore-late), `NSMutableOrderedSet` (1 file, iOS-only),
`sortedArrayUsingComparator:` (0), `addObserverForName:object:queue:usingBlock:` (0),
`scheduledTimerWithTimeInterval:repeats:block:` (0),
`weakObjectsHashTable` / `strongToStrongObjectsMapTable` (0).

## Missing classes that matter (count = message sends in WebKit)

| Class | Count | Tier | Verdict |
|---|---|---|---|
| NSUUID | 9 files | WTF | implement (over Tiger's `uuid_*` family) |
| NSMapTable | 5 files | JSC ObjC API | implement (over Tiger's `NSCreateMapTable` C API) |
| NSOperationQueue / NSBlockOperation | 8 files | WebCore | implement small, over libtigerdispatch |
| NSJSONSerialization | 3 files | WebCore-late | deferred |
| NSItemProvider, NSFileCoordinator, NSURLSession, NSPersonNameComponents, NSTextCheckingResult, NSPresentationIntent, NSDateComponentsFormatter | 1-3 each | WebCore-late | deferred |
| NSApp*, NSAppearance, NSTouchBar, NSLayoutConstraint, NSViewController, NSPopover, NSVisualEffectView, NSScrollerImp, NSTrackingArea, NSSharingService*, NSAccessibility* | 1-50 each | AppKit, WebKitLegacy UI | out of scope for this track |

`NSFileCoordinator` is referenced from `WTF/wtf/cocoa/FileSystemCocoa.mm`, which is a
WTF file, but only inside two functions; a stub that just calls the accessor block
directly would be the minimum. Not done here.

## Language-level gaps (the important part)

1. **Fast enumeration**: `countByEnumeratingWithState:objects:count:` does not exist in
   Tiger, and neither `NSFastEnumerationState` nor the `NSFastEnumeration` protocol is
   declared in the 10.4u SDK. `for (x in collection)` appears at ~195 sites
   (3 in WTF/JSC, 192 in WebCore/WebKitLegacy). Without this nothing compiles.
2. **Subscripting**: `objectAtIndexedSubscript:`, `objectForKeyedSubscript:` and the two
   setters are absent. Used in JSC's ObjC API.
3. **`NSInteger` / `NSUInteger` are not in the 10.4u SDK at all** (10.5 additions).
   WebKit uses them everywhere. These are a header-only fix and are declared in
   `TigerCompat/NSCompat.h` under the real `NSINTEGER_DEFINED` guard, but they belong in
   the SDK overlay so that translation units which never include `NSCompat.h` still see
   them. Same for `NSFastEnumerationState` / `NSFastEnumeration` / `NSComparator`.
4. Object literals (`@[]`, `@{}`, `@(x)`) need `arrayWithObjects:count:`,
   `dictionaryWithObjects:forKeys:count:`, the whole `numberWith*:` family and
   `valueWithBytes:objCType:`. **All present on Tiger.** Nothing to do.

## Method gaps, by tier

WTF and JavaScriptCore need, in rough order of blocking-ness:

- fast enumeration, subscripting (above)
- `NSThread +isMainThread` / `-isMainThread` / `+mainThread` (96 uses across the tree,
  8 in WTF/JSC). 10.5 additions.
- `NSArray -firstObject` (22 uses). Tiger has `lastObject` only.
- `NSString -containsString:` (6), `-stringByReplacingOccurrencesOfString:withString:` (2),
  `-rangeOfString:options:range:locale:`. Tiger has the `options:range:` variant.
- `NSNumber +numberWithInteger:`, `-integerValue`, `-unsignedIntegerValue` (21 uses).
- `NSUUID`.
- `NSMapTable` (JSC's `JSVirtualMachine` / `JSWrapperMap` / `JSManagedValue` wrapper caches).
  They ask for zeroing-weak keys, which the fragile runtime cannot do; backed by
  non-retained keys instead, so a wrapper cache entry outlives its key. Documented in the code.
- `NSFileManager` modern error-returning API, driven by `WTF/wtf/cocoa/FileSystemCocoa.mm`:
  `contentsOfDirectoryAtPath:error:`, `attributesOfItemAtPath:error:`,
  `setAttributes:ofItemAtPath:error:`, plus the `removeItem`/`moveItem`/`copyItem`/
  `createDirectoryAtPath:withIntermediateDirectories:` family for completeness.
  Tiger's `directoryContentsAtPath:`, `fileAttributesAtPath:traverseLink:`,
  `changeFileAttributes:atPath:`, `removeFileAtPath:handler:`, `movePath:toPath:handler:`,
  `copyPath:toPath:handler:`, `createDirectoryAtPath:attributes:` cover all of it.
- `NSURL +fileURLWithPath:isDirectory:` (16), `-URLByAppendingPathComponent:`,
  `-URLByDeletingLastPathComponent`, `-URLByStandardizingPath`,
  `-setResourceValue:forKey:error:` (backup exclusion; a no-op is the right answer on Tiger).
- `NSLocale +preferredLanguages` (6), `+localeWithLocaleIdentifier:`.
- `NSProcessInfo -operatingSystemVersion`, `-processorCount`, `-activeProcessorCount`,
  `-physicalMemory`.

WebCore / WebKitLegacy additionally want the block-taking enumeration methods
(`enumerateObjectsUsingBlock:`, `enumerateKeysAndObjectsUsingBlock:`,
`enumerateIndexesUsingBlock:`, `indexOfObjectPassingTest:`,
`indexesOfObjectsPassingTest:`), `NSOperationQueue`, and KVO's
`removeObserver:forKeyPath:context:` (10.7). The last one is not implemented: the
context-taking overload cannot be faked correctly on top of Tiger's
`removeObserver:forKeyPath:`, and there are only 6 call sites.

Everything WebCore sends that is *not* Foundation (AVFoundation, CoreAnimation,
`CALayer`, `UTType`, touch bar) dominates the raw candidate list and is another
track's problem.

## What shipped

`compat/nscompat.m`
: fast enumeration on NSArray, NSSet, NSDictionary and NSEnumerator, plus
  `objc_enumerationMutation` and `objc_setEnumerationMutationHandler`, which
  Tiger's libobjc does not export and clang emits calls to; array and dictionary
  subscripting; `NSArray` firstObject and the block enumeration and predicate
  methods; `NSMutableArray` / `NSDictionary` / `NSSet` / `NSIndexSet` block
  enumeration; the NSString, NSNumber, NSData, NSThread, NSProcessInfo, NSLocale,
  NSFileManager and NSURL additions; and NSUUID.

`compat/nscompat-maptable.m`
: NSMapTable over `NSCreateMapTable`.

NSUUID is built on `uuid_generate_random`, `uuid_parse` and `uuid_unparse_upper`.
Tiger's libSystem exports the whole `uuid_*` family, including
`uuid_unparse_lower` and `uuid_unparse_upper`, and the 10.4u SDK declares all of
them. An earlier draft of this survey claimed otherwise; that was a truncated
grep, not a fact about Tiger.

`compat/nscompat-operation.m`
: NSOperation, NSBlockOperation and NSOperationQueue over libtigerdispatch.

`compat/include/TigerCompat/NSCompat.h` declares all of it, plus the 10.5 types
the 10.4u SDK is missing.

Two bugs the Tiger runs caught, both fixed:

- `-[NSUUID initWithUUIDString:]` accepted malformed input. The first version was
  built on `CFUUIDCreateFromString`, which is lenient on Tiger and returns a UUID
  for junk. It now uses `uuid_parse`, which rejects it.
- Operations run on a dispatch worker thread, which has no autorelease pool, so
  anything they autoreleased leaked with a `_NSAutoreleaseNoPool` warning. Both
  the operation path and the block path now push a pool.

## Tests

`spike/nscompattest.mm`, 58 checks, passes in both MRR and ARC on the Tiger box.
`spike/nsmaptabletest.m`, 10 checks, passes; it is a separate translation unit
because it has to rename Tiger's `NSMapTable` C typedef before importing
Foundation.

```
NOARC=1 spike/run.sh spike/nscompattest.mm -ltigerdispatch
        spike/run.sh spike/nscompattest.mm -ltigerdispatch
```

## Not implemented, and why

| Item | Reason |
|---|---|
| NSCache, NSHashTable, NSPointerArray, NSRegularExpression, NSMutableOrderedSet, `sortedArrayUsingComparator:` on NSSet, block-based NSNotificationCenter and NSTimer | zero uses outside WebKit2 |
| NSJSONSerialization | 3 files, all ApplePay and FairPlay, WebCore-late |
| NSFileCoordinator | 2 uses in FileSystemCocoa.mm; a stub that calls the accessor block directly is the minimum, not written yet |
| KVO `removeObserver:forKeyPath:context:` | cannot be faked correctly on Tiger's context-free `removeObserver:forKeyPath:`; 6 call sites |
| NSURL resource values | Tiger has no resource-value store. The getter reports nil and the setter succeeds without doing anything, which is right for the one WebKit writer (the backup-exclusion flag) |
| zeroing-weak NSMapTable | impossible on the fragile runtime; degraded to non-retained, so entries dangle instead of zeroing |
| per-queue NSOperationQueue concurrency width, operation dependencies, KVO on isFinished, cancelling a started operation | not needed by the three WebCore call sites |
