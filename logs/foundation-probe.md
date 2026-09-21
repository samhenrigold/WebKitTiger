# Foundation on Tiger versus modern, measured

Probe `spike/fndbehaviour.m`, runner `spike/run-fndbehaviour.sh`. One source built for both
10.4.11 and this Mac (macOS 26), printing identical `KEY=value` lines, diffed mechanically. Modern
Foundation is the reference, so each entry below is a measurement rather than a judgement about
what Tiger ought to do. Strings are compared as UTF-8 hex so the diff is exact.

**366 values compared. 45 differ, and they fall into four groups, none of which is an nscompat
logic bug.** Every remaining area matched exactly.

Scope was taken from what WebCore, WebKitLegacy/mac and WTF call by name, not from SPI headers.

## The one finding that affects the build

**`-ObjC` is mandatory when linking `libtigercompat.a`, and its absence fails silently.** The first
Tiger run of this probe died on `-[NSCFString stringByReplacingOccurrencesOfString:withString:]:
selector not recognized`, a method nscompat does shim. Categories in a static archive are only
pulled in if something references a symbol in the same object file, so without `-ObjC` the shimmed
categories are simply absent at runtime, with no link error and no warning.

`-ObjC` then force-loads the whole archive, which pulls in `nscompat-appkit.m.o` and
`nscompat-operation.m.o`, so the link line also needs AppKit, ApplicationServices and
`-ltigerdispatch`. The working line is in `spike/run-fndbehaviour.sh`.

This is worth checking in the real WebKit link, because the failure mode is a selector-not-found
at runtime in whichever code path happens to touch a shimmed category first.

## Group 1: C99 length modifiers in `-stringWithFormat:`

Tiger Foundation behaviour. `%zu`, `%zd`, `%jd` and `%td` are not understood: the specifier is
emitted as literal text, so `%zu` with 123456 yields the two characters `zu`. Silent corruption
rather than an error.

Everything else in the sweep matches: `%@` including nil, `%ld`, `%lu`, `%lld`, `%llu`, `%qi`,
`%qu`, `%hd`, `%hhd`, `%d`, `%x`, `%c`, `%s`, `%C`, `%e`, `%g`, `%f` with precision, `%%`, and
width and `%*d` padding.

**Not reached today.** No ObjC format string in WebKit uses `%z` or `%ll` (grepped: zero hits).
Recorded so nobody introduces one.

## Group 2: NSURL

Three separate things, all Tiger Foundation behaviour.

**`-[NSURL lastPathComponent]` and `-[NSURL pathExtension]` do not exist** (10.6 API), and
nscompat does not shim them. **Not reached:** the Mac port goes through
`[[url path] lastPathComponent]`, which is NSString's and is 10.0. The probe measures that route
too and it matches modern exactly on every URL tested. The direct-on-NSURL call sites are
iOS-only files.

**Tiger's `+URLWithString:` rejects a raw space.** `http://example.com/a b` returns nil where
modern returns a URL. Anything that hands an unescaped string to NSURL gets nil rather than a
lenient parse. WebCore parses URLs with its own WTF::URL and only converts at the boundary, so
this is a boundary hazard rather than a live break.

**File URLs carry an explicit `localhost` host.** `+fileURLWithPath:` gives
`file://localhost/tmp/a%20b/c.txt` with `host` = `localhost`, where modern gives `file:///tmp/...`
with `host` = nil. Worth knowing for any code that compares file URLs by absolute string or
inspects the host, since the two forms are not string-equal.

Everything else about NSURL matched, which is a lot: scheme, host, port, user, path, query and
fragment extraction across userinfo, IPv6 literal and full IPv6 hosts, percent-encoded paths,
empty paths, scheme-less strings, `data:` and `about:` URLs, and all five relative resolutions
(sibling, absolute path, `..`, query-only, fragment-only).

## Group 3: base64 decoding is absent

`-[NSData initWithBase64EncodedString:options:]` is not shimmed. This is deliberate and correct:
`NSCompat.h` says Tiger's Foundation has no base64 of any kind, nscompat provides
`-base64EncodedStringWithOptions:` only, and no WebKit call site decodes. Encoding matches modern
byte for byte. **Expected absence.**

## Group 4: NSError codes and domain from the file-manager shims

nscompat's `tigerFileError` (`nscompat.m:556`) builds every error as `NSPOSIXErrorDomain` with the
raw errno, which its own comment documents as the design. Modern Foundation returns
`NSCocoaErrorDomain` with the Cocoa codes:

| Case | Tiger | Modern |
|---|---|---|
| `createDirectoryAtPath:` without intermediates, already exists | POSIX 17 (`EEXIST`) | Cocoa 516 (`NSFileWriteFileExistsError`) |
| `attributesOfItemAtPath:` on a missing file | POSIX 2 (`ENOENT`) | Cocoa 260 (`NSFileReadNoSuchFileError`) |

**Not reached, but internally inconsistent.** `NSCompat.h` defines `NSFileWriteFileExistsError` as
516, and nscompat's own errors can never carry that code, so any caller comparing against the
constant the header provides would always fail. The only comparison in WebKit is
`FileSystemCocoa.mm:83`, inside an `NSFileManagerDelegate` callback, and that protocol is 10.5, so
Tiger's NSFileManager never invokes it. Sent to nscompat as a consistency note rather than a bug.

## Everything that matched

Listed because a null result is the useful half, and because several of these are shims rather
than Tiger's own code.

**NSString.** Surrogate pairs (length, `characterAtIndex:` on both halves, 4-byte UTF-8);
lone-surrogate encoding returning nil, and lossy conversion; invalid UTF-8 input (bare
continuation byte, truncated sequence) both returning nil; Latin-1 round trip and lossy fallback
for an unmappable character; `rangeOfString:` plain, case-insensitive, backwards, both combined,
literal, empty needle, missing needle, sub-range and anchored; `compare:` plain, case-insensitive
and `NSNumericSearch`; `componentsSeparatedByString:` for simple, leading and trailing separators,
no separator, empty string and doubled separators, plus `componentsJoinedByString:`;
`precomposedStringWithCanonicalMapping` composing e plus combining acute to one unit;
case mapping for sharp s, e-acute, dotless i and ASCII; `hasPrefix:`/`hasSuffix:` including the
empty prefix; trimming; `substringWithRange:`; and nscompat's
`stringByReplacingOccurrencesOfString:withString:`.

**NSCharacterSet.** Whitespace and whitespace-and-newline membership for space, tab, LF, CR, FF,
VT, NBSP, U+2028, U+2029, ideographic space and a letter — all eleven agree on both sets, which
matters because the two sets differ from each other in exactly the same places. Alphanumeric
membership for ASCII, punctuation and e-acute.

**NSNumber and numeric parsing.** `stringValue`, `description`, `intValue`, double and long-long
values, `compare:`, the `boolValue`/`intValue` equality quirk, and `-[NSString intValue]` and
`doubleValue` on leading whitespace, trailing garbage and the empty string.

**Dates.** Epoch and reference-date conversion, GMT and named time zones, and an HTTP-format date
rendered through `NSCalendarDate`.

**Collections.** Count, indexing, `indexOfObject:` including the `NSNotFound` case, fast
enumeration order for arrays and count for dictionaries, dictionary lookup and miss, mutation
(add, insert, remove), `sortedArrayUsingSelector:`, sets, and the nscompat additions: array and
dictionary subscripting, `enumerateObjectsUsingBlock:` including honouring `*stop`, and
`sortedArrayUsingComparator:` with `NSNumericSearch`.

**NSData and NSUUID.** Length, equality, sub-ranges, base64 encoding; UUID string round trip,
rejection of a malformed string, distinctness of two generated UUIDs, string length and
self-equality.

**NSFileManager.** `createDirectoryAtPath:withIntermediateDirectories:` creating a nested path,
succeeding when repeated with intermediates and failing without them; existence checks including
the directory flag; `attributesOfItemAtPath:` returning size, type and modification date;
`contentsOfDirectoryAtPath:` contents; nil plus a set error for a missing path; removal.

**Run loop and timers.** Current run loop, idle mode, one-shot timer firing exactly once and
invalidating itself, repeating timer firing at least three times and invalidating on request, and
a timer added only to a private mode correctly not firing in the default mode. That last one
matters for WTF's RunLoop.

**NSNotificationCenter.** Synchronous delivery, name filtering, removal by name, object filtering
both ways, and userInfo delivery.

**NSOperationQueue.** A serial queue preserved submission order exactly (`01234567`), a concurrent
queue ran all sixteen blocks, and `operationCount` returned to zero after
`waitUntilAllOperationsAreFinished` on both — which also exercises the counter added during the
shim audit.

**NSLocale.** `en_US_POSIX` construction and identifier round trip, non-empty preferred languages,
and a non-nil current locale.

## Not covered

`NSAttributedString` (WebKitLegacy pasteboard) and `NSDateFormatter`/`NSNumberFormatter`. The
formatters are locale-driven and would mostly measure ICU version differences between 2005 and
2025 rather than anything actionable; attributed strings need an AppKit context to be meaningful.
Both are worth a separate pass if the pasteboard or `<input type=date>` paths become live.
