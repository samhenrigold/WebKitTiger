# i386 ABI screen: CoreFoundation, ATS/ApplicationServices, LaunchServices, Security

Run 2026-09-20. Tool: `tools/abi-screen.py` (reproducible; `--control` re-runs the
positive control). Raw export lists: `logs/api/tiger-{CF-defined,ATS,LaunchServices,HIServices,Security}.txt`.
Behavioural backstop: `spike/cfabitest.c`.

**Result: no ABI mismatch. 206 functions screened across 1,583 call sites; 200 matched
mechanically, 6 hand-checked and also matched; 0 candidates, 0 float/double shape
mismatches.** Independently, 199 of the 206 are declared by *both* the 10.4u SDK and
the Xcode 27 SDK, and those declared prototypes are byte-identical for i386 in every
case — an exact comparison rather than a heuristic. Nothing needs a Tiger-ABI adapter,
so no `compat/sdk-overlay/CoreFoundation.framework/Headers` hook was added and
wkcmake's ownership of that subdir is untouched.

## What was screened

Call sites were taken by grepping `WebKit/Source/{WTF,JavaScriptCore,WebCore,WebKitLegacy/mac}`
for `CF*`/`ATS*`/`LS*`/`Sec*` followed by `(`, excluding `*/spi/*`, `*SPI.h`, `SoftLink*`
and test directories so that PAL/WTF SPI *declarations* do not count as calls. That
yields 322 candidate names; intersecting with the symbols Tiger actually defines
leaves 206.

| framework | Tiger exports (i386, defined) | called by WebKit and exported |
|---|---|---|
| CoreFoundation | 1393 | 202 |
| Security | 1099 | 3 |
| LaunchServices | 248 | 1 |
| ATS | 419 | 0 |
| HIServices | 464 | 0 |

**ATS is dead surface.** Modern WebKit calls no `ATS*` function at all — the ATSUI
text path was removed years ago, so the 419 exports are unreachable. The only
non-CoreFoundation hits are `LSCopyApplicationForMIMEType`, `SecAccessCreate`,
`SecTrustCreateWithCertificates` and `SecTrustedApplicationCreateFromPath`. Everything
else WebKit calls with a `Sec` prefix (`SecTrustEvaluateWithError`, `SecItemAdd`,
`SecTaskCreateFromSelf`, …) Tiger does not export at all, which is a coverage
question for another track, not an ABI question.

Note the export lists in `logs/api/tiger-CF.txt` include **undefined** symbols
(`nm -g` prints imports too): 2243 lines against 1393 genuinely defined. The screen
uses `logs/api/tiger-CF-defined.txt`, regenerated here with the type column filtered.

## Method

For each screened function, two numbers are compared.

*Tiger side.* `tiger-otool -arch i386 -tV` on the framework binary, split per symbol.
Arguments live at `8(%ebp)` upward, so the argument footprint is
`max(displacement + access width) - 8`, rounded up to a 4-byte cdecl slot.

**The access width is load-bearing.** A trailing `CFAbsoluteTime` arrives as a single
`movsd 0xc(%ebp), %xmm0`, which touches bytes 0xc..0x13. Counting the displacement
alone scores that as 8 bytes instead of 12 and invents a mismatch. The first pass of
this screen did exactly that and flagged `CFRunLoopTimerSetNextFireDate`; it is a
false positive, and the width table in the tool is what removes it.

*Modern side.* Rather than parse headers, the tool asks clang to lower each prototype
for i386 (`clang++ -target i386-apple-macosx10.13 -isysroot <Xcode 27 SDK> -S -emit-llvm`)
and reads the resulting `declare`. That makes the front end responsible for struct
flattening, `sret` and `byval`, so the numbers the lead asked about come out exactly:
`CFRange` lowers to two `i32` (8 bytes), `CGRect` to 16, `CGFloat` to `float` (4),
`CFIndex` to `i32` (4), `CFAbsoluteTime` to `double` (8).

Two ABI details this settled, both confirmed against the SDK's own lowering:

- An 8-byte struct return on Darwin i386 goes in `EAX:EDX`, not through a hidden
  pointer: `declare i64 @CFStringGetRangeOfComposedCharactersAtIndex(ptr, i32)`.
  So a `CFRange`-returning function adds nothing to its stack footprint.
- A 16-byte struct return does use `sret`, and a 16-byte struct parameter uses
  `byval`: `declare void @CGRectStandardize(ptr sret(%struct.CGRect), ptr byval(%struct.CGRect))`.
  **No screened function uses either**, so no hidden-pointer accounting was needed
  for this set.

A second check looks for a float/double swap that equal totals would hide: for each
modern `double` parameter, the tool flags a Tiger 4-byte FP load at that exact offset
(and the converse for `float`). None found.

## Positive control

A zero result is only worth as much as the screen's ability to find a real mismatch,
so the same tool was pointed at Tiger's CoreText, where `logs/shim-audit.md` already
hand-verified four verdicts. It reproduces all four:

| function | modern | Tiger | screen | prior hand verdict |
|---|---|---|---|---|
| `CTLineDraw` | 8 | 16 | **flagged** | mismatch, trailing `CFRange` (adapter written) |
| `CTLineGetTypographicBounds` | 16 | 24 | **flagged** | mismatch (adapter written) |
| `CTFramesetterCreateFrame` | 20 | 20 | clean | "reads exactly the modern five slots" |
| `CTFontGetDescent` | 4 | 4 | clean | "reads exactly one" |

`tools/abi-screen.py --control` reruns this.

## Hand-checked cases

Six functions the mechanical comparison could not decide. All six match.

- **`CFPreferencesGetAppIntegerValue`** is a forwarding thunk
  (`push ebp; mov esp,ebp; pop ebp; jmp _CFPreferencesAppIntegerValue`) and reads no
  arguments itself. The target reads `0x8/0xc/0x10` = 12 bytes, matching the modern
  three-pointer prototype.
- **Five functions have no modern SDK prototype** because WebKit declares them itself
  in its own SPI headers, which the call-site grep deliberately excludes. Compared
  against WebKit's declarations instead, all five match:

  | function | WebKit's declaration | bytes | Tiger reads |
  |---|---|---|---|
  | `CFBundleCopyLocalizationForLocalizationInfo` | `(SInt32, SInt32, SInt32, CFStringEncoding)` | 16 | 8,12,16,20 = 16 |
  | `CFBundleGetLocalizationInfoForLocalization` | `(CFStringRef, SInt32*, SInt32*, SInt32*, CFStringEncoding*)` | 20 | 8,12,16,20,24 = 20 |
  | `CFReadStreamCreate` | `(CFAllocatorRef, const void*, void*)` | 12 | 8,12,16 = 12 |
  | `CFReadStreamSignalEvent` | `(CFReadStreamRef, CFStreamEventType, const void*)` | 12 | 8,12,16 = 12 |
  | `CFStringGetRangeOfCharacterClusterAtIndex` | `(CFStringRef, CFIndex, CFStringCharacterClusterType)` | 12 | 8,12,16 = 12 |

**Survey correction.** `WTF/wtf/spi/cf/CFStringSPI.h:57` carries a `// TIGER:` comment
saying `CFStringGetRangeOfCharacterClusterAtIndex` is 10.5+ and that nothing calls it.
Both halves are wrong. Tiger's CoreFoundation exports it with a real 938-instruction
implementation, and `WTF/wtf/text/cf/TextBreakIteratorCFCharacterCluster.h` calls it at
three sites (`:77`, `:85`, `:93`). It returns a sane `CFRange` on the box
(`{3,1}` for index 3 of `"abcdef"`). Whoever owns that header should drop the comment.

`CFRangeMake` is `CF_INLINE` in both the 10.4u SDK and the Xcode 27 SDK, with the same
`__CFRangeMake` out-of-line fallback, so it never reaches the dynamic linker and is not
screenable. (An earlier version of the tool stripped *all* leading underscores from
Mach-O symbols and so mistook the private `___CFRangeMake` for the public name. Fixed:
exactly one underscore is the C prefix. Left as a warning — that bug class can pair a
public name against a private function's disassembly and produce a false "clean".)

## Behavioural check on the box

A matching stack footprint does not prove the arguments are *interpreted* the same way
(NOTES: "a Tiger export whose name AND argument list match can still behave
differently"). `spike/cfabitest.c` exercises the cases where a difference would bite —
by-value doubles, `CFRange` by value in first, middle and last position, and the
8-byte struct return. Built with `tiger-clang`, run on the 10.4.11 box: **13 checks,
all pass, exit 0.**

```
PASS: CFAbsoluteTimeGetCurrent returns a plausible double
PASS: CFStringGetCharacters reads the by-value CFRange {3,4}
PASS: CFStringFindAndReplace honours the by-value CFRange
PASS: CFStringGetBytes converts exactly the by-value CFRange {2,3}
PASS: CFArrayAppendArray appends exactly the by-value CFRange {1,2}
PASS: CFStringGetRangeOfComposedCharactersAtIndex returns CFRange in registers
PASS: CFStringGetRangeOfCharacterClusterAtIndex returns a sane CFRange  (cluster at 3 = {3, 1})
PASS: CFRunLoopRunInMode times out rather than finishing early
PASS: CFRunLoopRunInMode honours a 0.25 s by-value double timeout  (elapsed 0.250 s, result 3)
PASS: CFRunLoopTimerCreate with two by-value doubles returns a timer
PASS: CFRunLoopTimerGetNextFireDate returns the date that was passed in
PASS: repeating CFRunLoopTimer fires on its by-value interval
PASS: CFRunLoopTimerSetNextFireDate stores the by-value double it was given
```

The three functions taking by-value doubles all place them where the modern prototype
does: `CFRunLoopRunInMode` reads `8,12,20` for `(ptr, double, Boolean)`,
`CFRunLoopTimerSetNextFireDate` reads `8,12` for `(ptr, double)`, and
`CFRunLoopTimerCreate` reaches `0x28` for its seven-parameter list, all exact.
The 0.250 s measured timeout is the strongest single piece of evidence that a
`double` argument lands correctly.

## Cross-check: Tiger's own declared prototypes

The disassembly screen measures what the callee *reads*, which cannot see a parameter
Tiger declares but ignores. The cgcompat track's CoreGraphics screen (commit `fd4afe3`)
points at the better oracle for that: where the **10.4u SDK declares the function, that
header is Tiger's own prototype**, so the two prototypes can be compared directly.

Lowering the same 206 names twice — once with `tiger-clang` against the 10.4u SDK, once
with `clang++ -target i386-apple-macosx10.13` against the Xcode 27 SDK — and diffing the
i386 argument totals gives:

| | count |
|---|---|
| declared by both SDKs, compared head-to-head | 199 |
| **prototype differences** | **0** |
| declared only by WebKit's own SPI headers | 7 |

So for 199 of 206 the result does not rest on disassembly at all, and the
declared-but-unread blind spot does not apply to them. The seven with no 10.4u
declaration, where disassembly remains the only oracle, are the five hand-checked in
the previous section plus:

- `CFRunLoopGetMain` — no arguments on either side. (Tiger exports it although the
  10.4u headers omit it; `compat/dispatch` already relies on this.)
- `CFStringCreateWithBytesNoCopy` — modern
  `(CFAllocatorRef, const UInt8*, CFIndex, CFStringEncoding, Boolean, CFAllocatorRef)`
  = 24 bytes; Tiger reads all six slots, `8,12,16,20,24,28` = 24 bytes.

## Limits of this screen

- The disassembly half measures what the callee **reads**, so a parameter Tiger declares
  but ignores is invisible to it. `CTLineDraw` was catchable only because Tiger's
  implementation genuinely uses its extra `CFRange`. The prototype cross-check above
  closes this for the 199 functions the 10.4u SDK declares; it remains a real limit for
  the other seven.
- It says nothing about **semantics** — return-value conventions, error behaviour, or
  a stub that returns a fixed value. That is what the spike and the shim audit are for.
- Seventeen screened functions take no arguments, so they carry no ABI information
  beyond their return type.
- Functions with no frame pointer would read arguments off `%esp` and score nothing;
  none of the 206 did.

## Stub detection (added later, across CoreText, CoreGraphics and CoreFoundation)

`tools/abi-screen.py` now also carries the two stub detectors from the audit track's
screen, because a signature that matches perfectly says nothing about whether the
function does anything. Credit for both modes and for the `CGLayerGetSize` false
positive: the `audit` agent.

- **Mode 1, empty body.** First `ret` within five instructions, nothing but frame
  bookkeeping, no argument read at all. Tiger's `CTRunGetGlyphs` is
  `push ebp; mov esp,ebp; pop ebp; ret`.
- **Mode 3, constant return.** Returns within seven instructions without reading an
  argument or calling anything, so the result cannot depend on the inputs. Tiger's
  `CTFontCreateUIFontForLocale` is `xorl %eax,%eax; ret`.
- **Mode 2, fixed-global-return stub.** No real call (a `get_pc_thunk` does not
  count, a tail `jmp` into a real implementation disqualifies), at least one
  PIC-relative global load, 25 instructions or fewer, and the only argument slot
  touched is the hidden `sret` pointer. Tiger's `CTRunGetImageBounds` copies 16
  bytes from a global into the caller's `CGRect` and never looks at the run.

  Two details cost real false positives before this worked. A PIC-relative global
  load only exists if a `get_pc_thunk` established a base register first; without
  that check, an ordinary struct dereference like `0x10(%eax)` matches, and every
  genuine accessor (`CTRunGetGlyphCount`, `CTRunGetStatus`, `CTLineGetGlyphRuns`,
  eight more) was reported as a stub. And the tail-call check has to match
  C++-mangled targets: `CTRunGetGlyphsPtr` jumps to `__ZNK13TStorageRange9GetGlyphsEv`,
  which a `_[A-Za-z]` pattern misses because the name begins with two underscores.

**Mode 2 is verified live here.** The audit track found afterwards (commit `f2c7588`)
that the version committed on their side was dead — it compared a set against a list,
which is never equal, so it reported nothing, and the original `CTRunGetImageBounds`
find had come from an earlier working draft. The re-implementation in
`tools/abi-screen.py` compares `sorted(acc)` against `[8]` and was checked against the
real binary before use: it returns `('global', 'fills the sret struct from a fixed
global, ignores every argument')` for `CTRunGetImageBounds`, `('empty', ...)` for
`CTRunGetGlyphs`, and `None` for the genuine accessors `CTRunGetGlyphCount`,
`CTRunGetStatus` and `CTLineGetGlyphRuns`.

Requiring slot 0 to be a real `sret` is a refinement on the mode as described: it
separates `CTRunGetImageBounds` (true positive) from `CGLayerGetSize` (the known
false positive, which reads its layer and only falls back to a global when the layer
is NULL). Functions matching the shape *without* `sret` are reported in a demoted
`global?` bucket rather than called stubs.

### Result over the WebKit-called sets: no new hits

| framework | screened (WebKit-called) | empty | constant | global-return |
|---|---|---|---|---|
| CoreFoundation + ATS + LaunchServices + HIServices + Security | 206 | 0 | 0 | 0 |
| CoreGraphics | 250 | 0 | 0 | 0 |
| CoreText | 54 | 3 | 0 | 0 |

The three CoreText hits are `CTRunGetGlyphs`, `CTRunGetAdvances` and
`CTRunGetStringIndices`, all already documented in `logs/shim-audit.md` and already
carrying ctcompat adapters. Nothing new.

Over **all 155 public-shaped CoreText exports** rather than only the called ones, the
three modes agree exactly with the audit track's independently derived expectations:

| mode | functions |
|---|---|
| empty | `CTRunDraw`, `CTRunGetAdvances`, `CTRunGetGlyphs`, `CTRunGetStringIndices` |
| constant | `CTFontCreateUIFontForLocale`, `CTFontCreateWithQuickdrawNameAndStyle`, `CTRunGetEmbeddedObject` |
| global-return | `CTLineGetImageBounds`, `CTRunGetImageBounds` |

Sweeping CoreFoundation and CoreGraphics the same way turns up only private
window-server surface (`CGS*`, `CGX*`, `VFB*`) and internal statics (`dummy`, `no_op`,
`rgn_size`, `CFMachPortInvalidateAll`), so an all-exports sweep needs a relevance
filter before it is worth running routinely.

### The prototype gate had the wrong polarity

Mode 3 is gated so that a zero-argument function returning a constant is not called a
stub — `CFArrayGetTypeID` is a constant by definition. Gating that on *having* a
prototype turned out to discard real findings: Tiger's CoreText is private, and
`CTFontCreateUIFontForLocale`, `CTFontCreateWithQuickdrawNameAndStyle` and
`CTRunGetEmbeddedObject` are declared by **no** header available here — not the Xcode 27
SDK, not the 10.4u SDK (which has no CoreText headers at all), not the 10.5 SDK
reference copy. All three are `xorl %eax,%eax; ret`, and all three were being dropped
for want of a prototype.

The gate now suppresses only when a prototype *positively says* the function takes no
arguments, and reports an unprototyped match as `const?` with the uncertainty named.
Absence of evidence annotates a finding rather than discarding it. `CFArrayGetTypeID`
is still suppressed, because it does have a prototype and that prototype says zero.

A 10.4u fallback prototype source was added for the same gate (`tiger_lowering()`),
which helps elsewhere but recovered nothing for CoreText. It is deliberately not used
for the size comparison, where checking Tiger's code against Tiger's own header would
be circular.

### Three tool bugs found and fixed while re-running

Extending to CoreGraphics exposed defects that the CoreFoundation set never triggered.
The CoreFoundation numbers above are unchanged by all three.

1. **`byval` parameters were sized as zero.** `CGContextFillRect(CGContextRef, CGRect)`
   scored 4 bytes instead of 20, and **47 of CoreGraphics' 250 functions looked
   mismatched**. The tool now sizes every `%struct.X` from the IR type table. No CF
   function takes a by-value struct, which is why this hid until now.
2. **`leal 0xc(%ebp)` was counted as a 4-byte read.** Taking the address of a by-value
   argument and passing it on — what `CGContextFillRect` does with its `CGRect` —
   makes the extent invisible, so these are now `undetermined` rather than scored.
3. **Over-reads and under-reads were conflated.** Only a callee reading *past* the
   argument list is an ABI break; reading less is harmless under cdecl, since the
   caller pops. `CGRectIsNull` reads 8 of the 16 bytes it is passed because a null
   rect is detectable from the origin alone. The two directions are now reported
   separately, and after the fixes CoreGraphics has **0 over-reads**, which agrees
   with the cgcompat track's independent conclusion.

Also guarded: `otool` labels only exported symbols, so a scan can run past a function
into unlabelled code and decode garbage. `CGPDFPageGetBoxRect` picked up a bogus
`addb %cl, 0x489d845(%ebp)` and scored 76 MB of arguments. Offsets beyond 0x400 are
now discarded and the function flagged instead.

## Third prototype source: WebKit's own SPI headers

The tool used to give up whenever the modern SDK had no declaration, which was 43 of
the CoreGraphics set, 5 of the CoreFoundation set and 1 of CoreText. For that API,
WebKit's own `*SPI.h` headers are the prototype it actually compiles against, so they
are the right record to screen against. `spi_lowering()` now lifts those declarations
out textually and lowers them for i386 through the same clang path.

Lifting them textually rather than including the headers is deliberate:
`CoreGraphicsSPI.h` pulls in `wtf/Platform.h` and `wtf/text/WTFString.h`, so it will
not parse standalone. Two details made the difference between working and not:

- **Compile the extracted declarations as C++ inside `extern "C"`.** In C, an unknown
  parameter type reads as a K&R parameter name and clang says "a parameter list
  without types", which gives nothing to heal from. C++ says
  `unknown type name 'CGGStateRef'`, which the tool heals by synthesising the type.
- **Synthesised types are opaque handles or enums**, both 4 bytes on i386, so totals
  stay exact. Fourteen were synthesised for CoreGraphics (`CGGStateRef`, `CGStyleRef`,
  `CGContextType`, `CGSWindowID`, …), and some declarations are also found in a `.mm`
  next to the call rather than in a header, which needed a second pass:
  `FormDataStreamCFNet.mm` declares `CFReadStreamCreate` with an `EXTERN` prefix.

### It flags CGGStateGetCTM

The check the lead asked for. `CGGStateGetCTM` is now screened rather than skipped and
comes out as the **single over-read in CoreGraphics**: Tiger reads 8 bytes where
WebCore's declaration `const CGAffineTransform *CGGStateGetCTM(CGGStateRef)` passes 4.
Those extra 4 bytes are the hidden `sret` of a by-value return. That is exactly the one
genuine CoreGraphics mismatch the cgcompat track found by hand in commit `fd4afe3` and
has already adapted, so the tool now reproduces it from a cold start.

### Final results, all four frameworks

| framework | screened | over-reads | under-reads | undetermined before | undetermined after |
|---|---|---|---|---|---|
| CoreFoundation + ATS + LaunchServices + HIServices + Security | 206 | 0 | 0 | 6 | 1 |
| CoreGraphics | 250 | 1 | 4 | 52 | 9 |
| CoreText | 54 | 6 | 3 | 4 | 3 |

**No new hits.** Every over-read the extension exposes is already known and already
adapted:

- `CGGStateGetCTM` — cgcompat, `fd4afe3`.
- The six CoreText over-reads (`CTFontCreateCopyWithAttributes`,
  `CTFontCreateWithFontDescriptor`, `CTFontCreateWithGraphicsFont`,
  `CTFontCreateWithName`, `CTFontDescriptorCreateWithNameAndSize`, `CTLineDraw`) are
  the `double`-to-`CGFloat` migration. ctcompat covers all six, four through
  `TigerCT*` adapters and two by redeclaring them with `double size` in
  `CTCompat.h`, whose comment describes the same misalignment this screen measures.

CoreFoundation's five recovered SPI prototypes all confirm the hand-checks recorded
above, and its undetermined count falls to one: `CFPreferencesGetAppIntegerValue`, the
forwarding thunk that reads no arguments itself.

The under-reads are benign in cdecl and all have the same cause — the callee takes the
address of a by-value struct and passes it on instead of loading the fields.
`CGContextSetCTM`, `CGContextSetBaseCTM` and `CGContextConcatCTM` each read 8 of the 28
bytes they are passed; `CGRectIsNull` reads 8 of 16, because a null rect is detectable
from the origin alone.

The nine remaining CoreGraphics undetermined are `CF_INLINE`-style functions with no
out-of-line declaration anywhere (`CGPointEqualToPoint`, `CGSizeApplyAffineTransform`
and friends), the same class as `CFRangeMake`: they are inlined at every call site and
never reach the dynamic linker, so there is nothing to screen.

## Appendix: all 206 screened functions

| function | framework | call sites | modern i386 arg bytes | Tiger arg bytes | verdict |
|---|---|---|---|---|---|
| `CFAbsoluteTimeGetCurrent` | CoreFoundation | 24 | 0 | 0 | match (0 args) |
| `CFAllocatorAllocate` | CoreFoundation | 1 | 12 | 12 | match |
| `CFAllocatorCreate` | CoreFoundation | 1 | 8 | 8 | match |
| `CFAllocatorDeallocate` | CoreFoundation | 1 | 8 | 8 | match |
| `CFArrayAppendArray` | CoreFoundation | 1 | 16 | 16 | match |
| `CFArrayAppendValue` | CoreFoundation | 24 | 8 | 8 | match |
| `CFArrayApplyFunction` | CoreFoundation | 3 | 20 | 20 | match |
| `CFArrayContainsValue` | CoreFoundation | 2 | 16 | 16 | match |
| `CFArrayCreate` | CoreFoundation | 6 | 16 | 16 | match |
| `CFArrayCreateMutable` | CoreFoundation | 22 | 12 | 12 | match |
| `CFArrayCreateMutableCopy` | CoreFoundation | 2 | 12 | 12 | match |
| `CFArrayGetCount` | CoreFoundation | 99 | 4 | 4 | match |
| `CFArrayGetTypeID` | CoreFoundation | 4 | 0 | 0 | match (0 args) |
| `CFArrayGetValueAtIndex` | CoreFoundation | 82 | 8 | 8 | match |
| `CFArrayRemoveAllValues` | CoreFoundation | 1 | 4 | 4 | match |
| `CFArrayRemoveValueAtIndex` | CoreFoundation | 2 | 8 | 8 | match |
| `CFArraySortValues` | CoreFoundation | 1 | 20 | 20 | match |
| `CFAttributedStringCreate` | CoreFoundation | 4 | 12 | 12 | match |
| `CFAttributedStringGetAttributes` | CoreFoundation | 1 | 12 | 12 | match |
| `CFAttributedStringGetLength` | CoreFoundation | 1 | 4 | 4 | match |
| `CFAttributedStringGetString` | CoreFoundation | 1 | 4 | 4 | match |
| `CFBitVectorGetBitAtIndex` | CoreFoundation | 1 | 8 | 8 | match |
| `CFBitVectorGetCount` | CoreFoundation | 3 | 4 | 4 | match |
| `CFBitVectorGetFirstIndexOfBit` | CoreFoundation | 1 | 16 | 16 | match |
| `CFBooleanGetValue` | CoreFoundation | 10 | 4 | 4 | match |
| `CFBundleCopyBundleURL` | CoreFoundation | 1 | 4 | 4 | match |
| `CFBundleCopyLocalizationForLocalizationInfo` | CoreFoundation | 1 | - | 16 | hand-checked (no modern prototype) |
| `CFBundleCopyLocalizedString` | CoreFoundation | 1 | 16 | 16 | match |
| `CFBundleCopyResourceURL` | CoreFoundation | 1 | 16 | 16 | match |
| `CFBundleCreate` | CoreFoundation | 1 | 8 | 8 | match |
| `CFBundleGetBundleWithIdentifier` | CoreFoundation | 3 | 4 | 4 | match |
| `CFBundleGetFunctionPointerForName` | CoreFoundation | 1 | 8 | 8 | match |
| `CFBundleGetIdentifier` | CoreFoundation | 2 | 4 | 4 | match |
| `CFBundleGetInfoDictionary` | CoreFoundation | 2 | 4 | 4 | match |
| `CFBundleGetLocalizationInfoForLocalization` | CoreFoundation | 1 | - | 20 | hand-checked (no modern prototype) |
| `CFBundleGetMainBundle` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFBundleGetPackageInfo` | CoreFoundation | 1 | 12 | 12 | match |
| `CFBundleGetValueForInfoDictionaryKey` | CoreFoundation | 1 | 8 | 8 | match |
| `CFBundleGetVersionNumber` | CoreFoundation | 1 | 4 | 4 | match |
| `CFCharacterSetAddCharactersInRange` | CoreFoundation | 10 | 12 | 12 | match |
| `CFCharacterSetAddCharactersInString` | CoreFoundation | 1 | 8 | 8 | match |
| `CFCharacterSetCreateMutable` | CoreFoundation | 1 | 4 | 4 | match |
| `CFCharacterSetGetPredefined` | CoreFoundation | 6 | 4 | 4 | match |
| `CFCharacterSetHasMemberInPlane` | CoreFoundation | 2 | 8 | 8 | match |
| `CFCharacterSetIsCharacterMember` | CoreFoundation | 2 | 8 | 8 | match |
| `CFCharacterSetIsLongCharacterMember` | CoreFoundation | 3 | 8 | 8 | match |
| `CFCharacterSetUnion` | CoreFoundation | 2 | 8 | 8 | match |
| `CFCopyDescription` | CoreFoundation | 4 | 4 | 4 | match |
| `CFDataCreate` | CoreFoundation | 9 | 12 | 12 | match |
| `CFDataCreateMutable` | CoreFoundation | 6 | 8 | 8 | match |
| `CFDataCreateWithBytesNoCopy` | CoreFoundation | 1 | 16 | 16 | match |
| `CFDataGetBytePtr` | CoreFoundation | 4 | 4 | 4 | match |
| `CFDataGetBytes` | CoreFoundation | 1 | 16 | 16 | match |
| `CFDataGetLength` | CoreFoundation | 9 | 4 | 4 | match |
| `CFDataGetMutableBytePtr` | CoreFoundation | 2 | 4 | 4 | match |
| `CFDataGetTypeID` | CoreFoundation | 2 | 0 | 0 | match (0 args) |
| `CFDataIncreaseLength` | CoreFoundation | 1 | 8 | 8 | match |
| `CFDictionaryAddValue` | CoreFoundation | 60 | 12 | 12 | match |
| `CFDictionaryApplyFunction` | CoreFoundation | 5 | 12 | 12 | match |
| `CFDictionaryContainsKey` | CoreFoundation | 13 | 8 | 8 | match |
| `CFDictionaryCreate` | CoreFoundation | 28 | 24 | 24 | match |
| `CFDictionaryCreateMutable` | CoreFoundation | 41 | 16 | 16 | match |
| `CFDictionaryCreateMutableCopy` | CoreFoundation | 7 | 12 | 12 | match |
| `CFDictionaryGetCount` | CoreFoundation | 14 | 4 | 4 | match |
| `CFDictionaryGetKeysAndValues` | CoreFoundation | 3 | 12 | 12 | match |
| `CFDictionaryGetTypeID` | CoreFoundation | 8 | 0 | 0 | match (0 args) |
| `CFDictionaryGetValue` | CoreFoundation | 183 | 8 | 8 | match |
| `CFDictionaryGetValueIfPresent` | CoreFoundation | 2 | 12 | 12 | match |
| `CFDictionaryRemoveValue` | CoreFoundation | 2 | 8 | 8 | match |
| `CFDictionarySetValue` | CoreFoundation | 122 | 12 | 12 | match |
| `CFEqual` | CoreFoundation | 33 | 8 | 8 | match |
| `CFGetRetainCount` | CoreFoundation | 2 | 4 | 4 | match |
| `CFGetTypeID` | CoreFoundation | 50 | 4 | 4 | match |
| `CFHash` | CoreFoundation | 4 | 4 | 4 | match |
| `CFLocaleCopyCurrent` | CoreFoundation | 3 | 0 | 0 | match (0 args) |
| `CFLocaleCopyDisplayNameForPropertyValue` | CoreFoundation | 3 | 12 | 12 | match |
| `CFLocaleCreate` | CoreFoundation | 4 | 8 | 8 | match |
| `CFLocaleCreateCanonicalLanguageIdentifierFromString` | CoreFoundation | 2 | 8 | 8 | match |
| `CFLocaleCreateCanonicalLocaleIdentifierFromString` | CoreFoundation | 1 | 8 | 8 | match |
| `CFLocaleGetSystem` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFLocaleGetValue` | CoreFoundation | 1 | 8 | 8 | match |
| `CFNotificationCenterAddObserver` | CoreFoundation | 17 | 24 | 24 | match |
| `CFNotificationCenterGetDarwinNotifyCenter` | CoreFoundation | 8 | 0 | 0 | match (0 args) |
| `CFNotificationCenterGetDistributedCenter` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFNotificationCenterGetLocalCenter` | CoreFoundation | 2 | 0 | 0 | match (0 args) |
| `CFNotificationCenterPostNotification` | CoreFoundation | 4 | 20 | 20 | match |
| `CFNotificationCenterRemoveObserver` | CoreFoundation | 6 | 16 | 16 | match |
| `CFNumberCreate` | CoreFoundation | 72 | 12 | 12 | match |
| `CFNumberFormatterCreate` | CoreFoundation | 1 | 12 | 12 | match |
| `CFNumberFormatterCreateStringWithNumber` | CoreFoundation | 2 | 12 | 12 | match |
| `CFNumberGetTypeID` | CoreFoundation | 5 | 0 | 0 | match (0 args) |
| `CFNumberGetValue` | CoreFoundation | 86 | 12 | 12 | match |
| `CFPreferencesAppSynchronize` | CoreFoundation | 1 | 4 | 4 | match |
| `CFPreferencesCopyAppValue` | CoreFoundation | 1 | 8 | 8 | match |
| `CFPreferencesCopyValue` | CoreFoundation | 4 | 16 | 16 | match |
| `CFPreferencesGetAppBooleanValue` | CoreFoundation | 1 | 12 | 12 | match |
| `CFPreferencesGetAppIntegerValue` | CoreFoundation | 2 | 12 | - | hand-checked |
| `CFReadStreamClose` | CoreFoundation | 1 | 4 | 4 | match |
| `CFReadStreamCopyProperty` | CoreFoundation | 1 | 8 | 8 | match |
| `CFReadStreamCreate` | CoreFoundation | 3 | - | 12 | hand-checked (no modern prototype) |
| `CFReadStreamCreateWithBytesNoCopy` | CoreFoundation | 1 | 16 | 16 | match |
| `CFReadStreamCreateWithFile` | CoreFoundation | 1 | 8 | 8 | match |
| `CFReadStreamGetError` | CoreFoundation | 2 | 4 | 4 | match |
| `CFReadStreamGetStatus` | CoreFoundation | 1 | 4 | 4 | match |
| `CFReadStreamHasBytesAvailable` | CoreFoundation | 1 | 4 | 4 | match |
| `CFReadStreamOpen` | CoreFoundation | 2 | 4 | 4 | match |
| `CFReadStreamRead` | CoreFoundation | 1 | 12 | 12 | match |
| `CFReadStreamScheduleWithRunLoop` | CoreFoundation | 2 | 12 | 12 | match |
| `CFReadStreamSetClient` | CoreFoundation | 2 | 16 | 16 | match |
| `CFReadStreamSetProperty` | CoreFoundation | 1 | 12 | 12 | match |
| `CFReadStreamSignalEvent` | CoreFoundation | 6 | - | 12 | hand-checked (no modern prototype) |
| `CFReadStreamUnscheduleFromRunLoop` | CoreFoundation | 1 | 12 | 12 | match |
| `CFRelease` | CoreFoundation | 17 | 4 | 4 | match |
| `CFRetain` | CoreFoundation | 7 | 4 | 4 | match |
| `CFRunLoopAddObserver` | CoreFoundation | 6 | 12 | 12 | match |
| `CFRunLoopAddSource` | CoreFoundation | 10 | 12 | 12 | match |
| `CFRunLoopAddTimer` | CoreFoundation | 4 | 12 | 12 | match |
| `CFRunLoopCopyCurrentMode` | CoreFoundation | 1 | 4 | 4 | match |
| `CFRunLoopGetCurrent` | CoreFoundation | 18 | 0 | 0 | match (0 args) |
| `CFRunLoopGetMain` | CoreFoundation | 13 | 0 | 0 | match (0 args) |
| `CFRunLoopGetNextTimerFireDate` | CoreFoundation | 1 | 8 | 8 | match |
| `CFRunLoopObserverCreate` | CoreFoundation | 5 | 24 | 24 | match |
| `CFRunLoopObserverInvalidate` | CoreFoundation | 1 | 4 | 4 | match |
| `CFRunLoopRemoveObserver` | CoreFoundation | 2 | 12 | 12 | match |
| `CFRunLoopRemoveSource` | CoreFoundation | 2 | 12 | 12 | match |
| `CFRunLoopRun` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFRunLoopRunInMode` | CoreFoundation | 4 | 16 | 16 | match |
| `CFRunLoopSourceCreate` | CoreFoundation | 6 | 12 | 12 | match |
| `CFRunLoopSourceInvalidate` | CoreFoundation | 1 | 4 | 4 | match |
| `CFRunLoopSourceSignal` | CoreFoundation | 6 | 4 | 4 | match |
| `CFRunLoopStop` | CoreFoundation | 1 | 4 | 4 | match |
| `CFRunLoopTimerCreate` | CoreFoundation | 2 | 36 | 36 | match |
| `CFRunLoopTimerDoesRepeat` | CoreFoundation | 2 | 4 | 4 | match |
| `CFRunLoopTimerGetNextFireDate` | CoreFoundation | 1 | 4 | 4 | match |
| `CFRunLoopTimerInvalidate` | CoreFoundation | 8 | 4 | 4 | match |
| `CFRunLoopTimerIsValid` | CoreFoundation | 2 | 4 | 4 | match |
| `CFRunLoopTimerSetNextFireDate` | CoreFoundation | 3 | 12 | 12 | match |
| `CFRunLoopWakeUp` | CoreFoundation | 9 | 4 | 4 | match |
| `CFSetAddValue` | CoreFoundation | 2 | 8 | 8 | match |
| `CFSetApplyFunction` | CoreFoundation | 1 | 12 | 12 | match |
| `CFSetCreate` | CoreFoundation | 1 | 16 | 16 | match |
| `CFSetCreateMutable` | CoreFoundation | 3 | 12 | 12 | match |
| `CFSetGetValue` | CoreFoundation | 1 | 8 | 8 | match |
| `CFSetRemoveValue` | CoreFoundation | 1 | 8 | 8 | match |
| `CFSetSetValue` | CoreFoundation | 1 | 8 | 8 | match |
| `CFStringCompare` | CoreFoundation | 49 | 12 | 12 | match |
| `CFStringConvertEncodingToIANACharSetName` | CoreFoundation | 4 | 4 | 4 | match |
| `CFStringConvertEncodingToNSStringEncoding` | CoreFoundation | 1 | 4 | 4 | match |
| `CFStringConvertIANACharSetNameToEncoding` | CoreFoundation | 2 | 4 | 4 | match |
| `CFStringConvertNSStringEncodingToEncoding` | CoreFoundation | 1 | 4 | 4 | match |
| `CFStringCreateCopy` | CoreFoundation | 4 | 8 | 8 | match |
| `CFStringCreateExternalRepresentation` | CoreFoundation | 1 | 16 | 16 | match |
| `CFStringCreateFromExternalRepresentation` | CoreFoundation | 3 | 12 | 12 | match |
| `CFStringCreateMutableCopy` | CoreFoundation | 5 | 12 | 12 | match |
| `CFStringCreateWithBytes` | CoreFoundation | 5 | 20 | 20 | match |
| `CFStringCreateWithBytesNoCopy` | CoreFoundation | 4 | 24 | 24 | match |
| `CFStringCreateWithCString` | CoreFoundation | 2 | 12 | 12 | match |
| `CFStringCreateWithCStringNoCopy` | CoreFoundation | 6 | 16 | 16 | match |
| `CFStringCreateWithCharacters` | CoreFoundation | 4 | 12 | 12 | match |
| `CFStringCreateWithCharactersNoCopy` | CoreFoundation | 2 | 16 | 16 | match |
| `CFStringCreateWithFileSystemRepresentation` | CoreFoundation | 1 | 8 | 8 | match |
| `CFStringCreateWithFormat` | CoreFoundation | 2 | 12+ | 16 | match (variadic) |
| `CFStringCreateWithFormatAndArguments` | CoreFoundation | 4 | 16 | 16 | match |
| `CFStringFind` | CoreFoundation | 4 | 12 | 12 | match |
| `CFStringFindAndReplace` | CoreFoundation | 2 | 24 | 24 | match |
| `CFStringGetBytes` | CoreFoundation | 2 | 36 | 36 | match |
| `CFStringGetCString` | CoreFoundation | 6 | 16 | 16 | match |
| `CFStringGetCStringPtr` | CoreFoundation | 4 | 8 | 8 | match |
| `CFStringGetCharacterAtIndex` | CoreFoundation | 6 | 8 | 8 | match |
| `CFStringGetCharacters` | CoreFoundation | 1 | 16 | 16 | match |
| `CFStringGetCharactersPtr` | CoreFoundation | 2 | 4 | 4 | match |
| `CFStringGetFileSystemRepresentation` | CoreFoundation | 2 | 12 | 12 | match |
| `CFStringGetLength` | CoreFoundation | 28 | 4 | 4 | match |
| `CFStringGetMaximumSizeForEncoding` | CoreFoundation | 1 | 8 | 8 | match |
| `CFStringGetMaximumSizeOfFileSystemRepresentation` | CoreFoundation | 1 | 4 | 4 | match |
| `CFStringGetRangeOfCharacterClusterAtIndex` | CoreFoundation | 3 | - | 12 | hand-checked (no modern prototype) |
| `CFStringGetTypeID` | CoreFoundation | 6 | 0 | 0 | match (0 args) |
| `CFStringHasPrefix` | CoreFoundation | 7 | 8 | 8 | match |
| `CFStringLowercase` | CoreFoundation | 1 | 8 | 8 | match |
| `CFStringReplace` | CoreFoundation | 3 | 16 | 16 | match |
| `CFStringReplaceAll` | CoreFoundation | 1 | 8 | 8 | match |
| `CFStringTransform` | CoreFoundation | 1 | 16 | 16 | match |
| `CFStringTrimWhitespace` | CoreFoundation | 2 | 4 | 4 | match |
| `CFTimeZoneCopyDefault` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFURLCopyFileSystemPath` | CoreFoundation | 1 | 8 | 8 | match |
| `CFURLCopyPathExtension` | CoreFoundation | 1 | 4 | 4 | match |
| `CFURLCreateAbsoluteURLWithBytes` | CoreFoundation | 5 | 24 | 24 | match |
| `CFURLCreateStringByReplacingPercentEscapes` | CoreFoundation | 1 | 12 | 12 | match |
| `CFURLCreateWithBytes` | CoreFoundation | 4 | 20 | 20 | match |
| `CFURLCreateWithFileSystemPath` | CoreFoundation | 2 | 16 | 16 | match |
| `CFURLCreateWithString` | CoreFoundation | 1 | 12 | 12 | match |
| `CFURLGetBaseURL` | CoreFoundation | 2 | 4 | 4 | match |
| `CFURLGetByteRangeForComponent` | CoreFoundation | 4 | 12 | 12 | match |
| `CFURLGetBytes` | CoreFoundation | 8 | 12 | 12 | match |
| `CFURLGetString` | CoreFoundation | 2 | 4 | 4 | match |
| `CFURLGetTypeID` | CoreFoundation | 1 | 0 | 0 | match (0 args) |
| `CFUUIDCreate` | CoreFoundation | 1 | 4 | 4 | match |
| `CFUUIDCreateString` | CoreFoundation | 1 | 8 | 8 | match |
| `CFWriteStreamClose` | CoreFoundation | 1 | 4 | 4 | match |
| `CFWriteStreamCopyProperty` | CoreFoundation | 1 | 8 | 8 | match |
| `CFWriteStreamCreateWithAllocatedBuffers` | CoreFoundation | 1 | 8 | 8 | match |
| `CFWriteStreamOpen` | CoreFoundation | 1 | 4 | 4 | match |
| `LSCopyApplicationForMIMEType` | LaunchServices | 1 | 12 | 12 | match |
| `SecAccessCreate` | Security | 1 | 12 | 12 | match |
| `SecTrustCreateWithCertificates` | Security | 1 | 12 | 12 | match |
| `SecTrustedApplicationCreateFromPath` | Security | 2 | 8 | 8 | match |
