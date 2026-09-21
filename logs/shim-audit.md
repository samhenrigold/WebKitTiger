# Shim audit against authoritative references (2026-09-20)

Scope: `compat/` shims checked against Apple open source, macports-legacy-support, and
disassembly of Mac OS X 10.5.8 i386 binaries. Fixes applied to the shims this track owns;
`cfcompat.c` / `ctcompat.c` / `cgcompat.c` findings were sent to their owners instead.

## References used

| Reference | Where |
|---|---|
| objc4-267.1 (the runtime Tiger 10.4.11 actually ships) | `refs/objc4-267.1-objc-class.m`, `-objc-runtime.m`, `-objc-private.h`, `-objc-class.h` |
| objc4-437 (last fragile-ABI runtime, 10.6) | `refs/objc4-437-objc-class-old.m`, `-objc-runtime-old.m`, `-objc-references.mm`, `-objc-accessors.m`, `-Protocol.m` |
| libclosure-63 | `refs/libclosure-63-{runtime.c,data.c,Block_private.h,Block.h}` |
| libdispatch-84.5.1 | `refs/libdispatch-84-semaphore.c` |
| macports-legacy-support (git HEAD) | `refs/mlegacy/src/` |
| Mac OS X 10.5.8 i386 frameworks | `refs/leopard/{CoreText,CoreGraphics,Foundation,CoreFoundation,libSystem.B.dylib,libobjc.A.dylib}.i386` |

The 10.5.8 combo `.dmg` is gone from every Apple host. Apple's legacy Software Update catalog
(`swscan.apple.com/content/catalogs/others/index-leopard-snowleopard.merged-1.sucatalog`) still
serves the same update as a flat pkg, kept at `refs/MacOSXUpdCombo10.5.8.pkg` (805 MB). Note for
anyone extracting more: in 10.5, CoreText and CoreGraphics are subframeworks of ApplicationServices.

All spike tests pass on the Tiger box after these changes: `objc2test`, `availtest`, `arctest`,
`exctest`, `nsmaptabletest`, `nscompattest`, `dispatchtest`, `runcxx.sh`, `runfstest.sh`.

---

## compat/libcompat.c

| Function | Reference | Verdict |
|---|---|---|
| `posix_memalign` | macports `posix_memalign_emulation.c` | matches — **comment fixed** |
| `strnlen` | macports `strnlen.c` | matches (ours uses `memchr`, same result) |
| `memmem` | macports `memmem.c` (= Apple Libc / FreeBSD) | **bug, fixed** |
| `getline` | macports `getdelim.c` | differs harmlessly |
| `arc4random_buf`, `arc4random_uniform` | macports `arc4random.c` | matches |
| `clock_gettime`, `clock_getres` | macports `sys_time.c` behaviour | differs harmlessly |
| `pthread_setname_np`, `pthread_getname_np` | macports `pthread_setname_np.c` | matches (both no-op on 10.4) |
| `pthread_threadid_np` | xnu / Libsystem | differs harmlessly |
| `openat`, `unlinkat`, `fdopendir` | macports `atfuncs.c`, `fdopendir.c` | matches in approach |
| `fcopyfile`, `copyfile_state_alloc/free` | macports `copyfile.c` | differs harmlessly |
| `__bzero`, `__eprintf`, `_dyld_find_unwind_sections` | — | no reference needed; covered by passing tests |

**`memmem` — bug, fixed.** The shim returned the haystack pointer for a zero-length needle.
Apple's Libc `memmem` (FreeBSD `string/memmem.c`, which is what macports ships verbatim) returns
`NULL` when either length is zero. `WTF::find(span, span)` in
`WebKit/Source/WTF/wtf/StdLibExtras.h:1118` calls `memmem` directly on Darwin, so the shim now
matches so a Tiger build and a macOS build agree. Changed to `if (!hl || !nl) return NULL;`.

**`posix_memalign` — matches, stale comment fixed.** The implementation is line-for-line what
macports does: `malloc` at or below 16 bytes of alignment (Tiger's tiny-region quantum is 16),
`valloc` above. The `ponytail:` comment claimed the code stashed a base pointer for an
over-allocate scheme; it never did. Rewritten to name the real ceiling: an alignment above the
4096-byte page is silently under-satisfied. That is acceptable here — bmalloc routes large
aligned requests through `SystemHeap::memalignLarge` → `tryVMAllocate`, not through
`posix_memalign` (`WebKit/Source/bmalloc/bmalloc/bmalloc.cpp:84`). Free-compatibility is also
confirmed: `SystemHeap::memalign` calls `::aligned_alloc` (an inline in `tigerprelude.h:147` over
`posix_memalign`) and frees with `malloc_zone_free(m_zone, …)` where `m_zone` is forced to the
default zone on Tiger (`SystemHeap.cpp:56-62`), and `valloc`'d memory belongs to the default zone.
Redundant `alignment >= 4096` branch folded away; behaviour unchanged.

**`getline` — differs harmlessly.** On a read error with a partial line already buffered, BSD
`getdelim` returns -1; ours returns the partial line. No WebKit caller distinguishes the two.

**`clock_gettime` — differs harmlessly.** Any clock id other than `CLOCK_REALTIME` is treated as
monotonic and returns 0 rather than `EINVAL`. `clock_getres` always reports 1 µs. Real 10.12+
reports per-clock resolution. Nothing reads it.

**`pthread_threadid_np` — differs harmlessly.** Returns the `pthread_t` value, not the kernel's
64-bit thread id. Unique among live threads, so `WTF::Thread` identity comparisons are correct,
but the value will not match a ktrace or `thread_info` reading, and it repeats when a `pthread_t`
is recycled.

**Gap survey against macports.** Checked what macports implements that Tiger lacks and we do not
provide: `strndup`, `stpncpy`, `fmemopen`, `open_memstream`, `sincos`, `getentropy`. Confirmed
absent from `logs/api/tiger-libSystem.txt`, and confirmed no WebKit source outside the unused
`bmalloc/mimalloc` tree calls any of them. `realpath` and `pthread_get_stacksize_np` do exist on
Tiger. One latent item: Tiger's `realpath` predates POSIX 2008 and does not accept a `NULL` output
buffer; `JavaScriptCore/API/tests/testapi.c:1611` passes `NULL`. Test-only, not shimmed.

## compat/objc2compat.m

Struct layouts verified against `objc4-267.1-objc-class.h` and the 10.4u SDK's `objc-class.h`
(byte-identical) rather than 437's `old_class`/`old_method_list`, since 267.1 is what the target
runs. `old_*` in 437 is a rename of the same layout.

| Function | Reference | Verdict |
|---|---|---|
| `class_addMethod` | objc4-267.1 `fixupSelectorsInMethodList`; objc4-437 `_class_addMethod` | **bug, fixed** |
| `class_addIvar` | objc4-437 `class_addIvar` | **bug, fixed** |
| `method_setImplementation`, `method_exchangeImplementations` | objc4-437 | differs harmlessly |
| `objc_allocateClassPair` / `objc_registerClassPair` | objc4-437 `objc_initializeClassPair`; objc4-267.1 `objc_addClass` | matches |
| `objc_get/setAssociatedObject`, `objc_removeAssociatedObjects` | objc4-437 `objc-references.mm` | matches |
| `objc_get/setProperty`, `objc_copyStruct` | objc4-437 `objc-accessors.m` | matches |
| `class_copyMethodList`, `class_copyIvarList`, `class_copyPropertyList` | objc4-437 | matches |
| protocol introspection | objc4-437 `Protocol.m` | known limit, already documented |

**`class_addMethod` — bug, fixed.** The freshly `calloc`'d `objc_method_list` left `obsolete` at
`NULL`. objc4-267.1 `objc-class.m:831` treats any list whose `obsolete` is not `_OBJC_FIXED_UP`
(`(void *)1771`) as still holding selector *names*: it copies the whole list with
`_malloc_internal` + `memmove` and runs every `method_name` through `sel_registerNameNoLock`.
Our names are already registered `SEL`s, and on the old runtime a `SEL` is the uniqued name
pointer, so the re-registration happened to be a no-op — but the copy was not: the list we built
and the `strdup`'d type string leaked on every call, and the `Method` the runtime installed was
not the one we allocated. Now sets `ml->obsolete = (struct objc_method_list *)1771`, which is
exactly what objc4-437 does under the name `fixed_up_method_list`.

**`class_addIvar` — bug, fixed.** The shim called `realloc(cls->ivars, …)`. On a class compiled
into an image, `cls->ivars` points into the read-only `__OBJC,__instance_vars` section, so that is
a `realloc` of a non-heap pointer. objc4-437 `class_addIvar` always allocates a fresh list,
`memcpy`s the old one, and frees the old only when `malloc_size` says it was heap-allocated. The
shim now does the same. Only reachable when a caller adds an ivar to a compiled class, which the
API forbids, but the crash mode was ugly. The offset arithmetic (`round_up(instance_size, 1 <<
alignment)`, then `instance_size = offset + size`) already matched 437 exactly.

**`method_setImplementation` / `method_exchangeImplementations` — differs harmlessly.** Both call
`_objc_flush_caches(Nil)`; objc4-437 does not flush at all, it just swaps under a spinlock. That
is correct on the old runtime because `struct objc_cache` holds `Method buckets[1]`
(`objc4-267.1-objc-class.h:186`) — the cache stores the `Method` pointer, so a changed
`method_imp` is visible immediately through cached entries. The flush is a process-wide cache
wipe and is pure cost. Left alone: it is not wrong, it is just slow, and both are cold paths.
The same fact confirms `arc.m`'s dealloc hook (which writes `method_imp` with no flush) is
correct rather than lucky.

**`objc_allocateClassPair` / `objc_registerClassPair` — matches, for a non-obvious reason.** The
shim `calloc`s both structs, so `cls->cache` is `NULL` and `cls->info` lacks
`CLS_CONSTRUCTING` / `CLS_EXT` / `CLS_LEAF`, all of which objc4-437's `objc_initializeClassPair`
sets. On Tiger this is fine: `objc_addClass` (objc4-267.1 `objc-runtime.m`) explicitly handles a
hand-rolled class — if `cache` is `NULL` it installs `&emptyCache` and *overwrites* `info` with
`CLS_CLASS` (and `CLS_META` for the metaclass). So setting those bits would be pointless, and the
`NULL` cache is repaired before the first message send. Metaclass wiring (`meta->isa` =
superclass's metaclass's isa) is equivalent to the shim's walk-to-root-and-take-its-metaclass for
any NSObject-rooted hierarchy. Verified end to end by `spike/objc2test.m`.

**Associated objects — matches.** objc4-437's `objc-references.mm` keys an outer table on the
object and an inner table on the key, releases non-assign values on removal, and clears the whole
entry from `objc_destructInstance`. Ours does the same over `CFMutableDictionary`, cleared from
the shared `-dealloc` hook. The policy decode (`policy & 3 == 3` means copy, non-zero means
retain) matches `OBJC_ASSOCIATION_COPY_NONATOMIC = 3` / `RETAIN = 1`. Retain/copy happens outside
the lock, as 437 is careful to do.

## compat/arc.m

| Entry point | Reference | Verdict |
|---|---|---|
| `objc_retain/release/autorelease` and the `*ReturnValue` family | ARC ABI on a runtime without the fast paths | matches |
| `objc_storeStrong` | — | matches |
| `objc_retainBlock` | libclosure `Block_copy` | matches |
| `objc_autoreleasePoolPush/Pop` | — | matches (non-GC `-drain` is `-release`) |
| `__weak` side table | objc4-437 weak tables | differs harmlessly |
| `_tigerInstallDeallocHook` | objc4-267.1 cache layout | matches |
| `_Block_use_RR2` callbacks | libclosure-63 `runtime.c:260` | matches |

**Dealloc hook — matches.** Swapping `m->method_imp` on `NSObject`'s `-dealloc` with no cache
flush is correct on this runtime, for the reason above: cache buckets are `Method` pointers.
Idempotence and the shared constructor between `arc.m` and `objc2compat.m` are correct.

**`blockDestructInstance` — matches, and the comment is right.** `_Block_use_RR2` overwrites
`_Block_destructInstance` wholesale (`libclosure-63-runtime.c:263`), and `_Block_release` calls
it unconditionally at `runtime.c:513`, so passing `NULL` would be a jump to zero. Our
BlocksRuntime carries the same default no-op at `runtime.c:199`, so the explicit no-op is
redundant but not wrong.

**`__weak` — differs harmlessly, documented limit.** `objc_loadWeakRetained` sends `-retain`
under the side-table lock; a weak load racing the final `-release` of the same object can still
resurrect a partly-deallocated object, because `-release` reaching zero is not serialised against
the weak lock the way objc4's `SideTable` does it. Inherent to implementing weak on top of
plain retain/release; unchanged.

## compat/blockclasses.m

| Item | Reference | Verdict |
|---|---|---|
| `_NSConcrete*Block` placeholder arrays | libclosure-63 `data.c` | matches (same six symbols, same `void *[32]`) |
| `-retain` per block kind | Foundation behaviour | matches |
| `-copy` / `-copyWithZone:` → `_Block_copy` | libclosure | matches |
| `-retainCount` shift | libclosure-63 `Block_private.h:32-34` | matches |
| class-struct memcpy | — | differs harmlessly |

`BLOCK_REFCOUNT_MASK` is `0xfffe` with `BLOCK_DEALLOCATING` in bit 0, so `(flags & mask) >> 1` is
the right refcount decode. `_NSConcreteAutoBlock` / `FinalizingBlock` / `WeakBlockVariable` stay
zeroed, which is correct: libclosure only touches them under GC.

**Class-struct memcpy — differs harmlessly, worth knowing.** `memcpy(_NSConcreteStackBlock,
[__NSStackBlock__ class], sizeof(struct objc_class))` copies the `cache` pointer, so the
placeholder and the real class share one cache. If Tiger's `_cache_expand` ever grows that cache
it updates only the class whose message send triggered it, leaving the other with a stale pointer
into cache garbage. Unreachable in practice: no instance ever has the original class as its `isa`,
so only the placeholder copies receive instance messages. Also note `sizeof(struct objc_class)` is
10 words while clang lays down 12 (see NOTES), so `ivar_layout` and `ext` are not copied — which
is fine, and `tigerClassPropertyList`'s range check correctly declines to read word 11 of a
placeholder.

## compat/tlv.c

No Apple reference exists (dyld's TLV support postdates Tiger entirely). Checked against dyld's
documented `_tlv_atexit` contract: LIFO order per thread, run at thread exit. The shim does both,
claims its pthread key in a constructor so its destructor runs before compiler-rt's emutls frees
the storage, and flushes the main thread from `atexit` because pthread key destructors never run
there. All three are the right calls. Verdict: matches. Covered by `spike/cxxtest.cpp`.

## compat/availability.c

Reference: compiler-rt `os_version_check.c`. That file reads the same
`/System/Library/CoreServices/SystemVersion.plist` key, via CoreFoundation, and compares
major/minor/subminor in the same order. The shim's string scan is equivalent for a well-formed
plist. `PLATFORM_MACOS = 1` matches `<mach-o/loader.h>`. Verdict: matches. Covered by
`spike/availtest.m`.

## compat/nscompat.m, nscompat-maptable.m, nscompat-operation.m

Audited against `refs/leopard/Foundation.i386` (notably `-[NSClassicMapTable allKeys]` at 0xb0c60)
and the 10.5 SDK headers.

**`NSMapTable` personality dispatch — bug, fixed.** `tigerOptionsUsePointerIdentity` only
recognised `NSPointerFunctionsObjectPointerPersonality`. Opaque (`1<<8`) and integer (`5<<8`)
personalities fell through to `NSNonRetainedObjectMapKeyCallBacks`, whose hash and equal callbacks
send `-hash` / `-isEqual:` to the key. JSC keys three tables on non-objects under the opaque
personality — `JSVirtualMachine.mm:52` and `:110` (`JSContextGroupRef`, `JSGlobalContextRef`) and
`JSWrapperMap.mm:600` — so this crashes on the first `JSContext` creation. Inverted the test: only
`NSPointerFunctionsObjectPersonality` (0) may be messaged; everything else hashes by address.
`spike/nsmaptabletest.m` gained a case that creates an opaque-personality table over stack
addresses and reads a value back.

**`NSMapTable` strong keys under `ObjectPointerPersonality` — bug, fixed.** The identity branch
was tested before the retaining branch, so a caller asking for strong memory *and* identity got
`NSNonOwnedPointerMapKeyCallBacks` and its keys were never retained. Tiger has no retain+identity
callback struct, so the shim now copies `NSObjectMapKeyCallBacks`'s `retain`/`release` into a copy
of the non-owned struct — and only for the object-pointer personality, since retaining an opaque
or integer key would message a non-object.

**`NSMapTable` fast-enumeration leak — fixed.** `countByEnumeratingWithState:` released the
enumerator only on the call that returned zero, so a `break` out of a `for (k in table)` leaked
the enumerator and the key array behind it. Now releases as soon as a short batch shows the
enumerator is spent, which is also the zero-return path.

**`NSMapTable` enumeration of non-object keys — investigated, deliberately left alone.**
`NSAllMapTableKeys` builds an `NSArray`, which retains every element, so enumerating an
opaque-keyed table is a crash. That is also what real Foundation does: the 10.5 disassembly shows
`allKeys` packing the keys and calling `+arrayWithObjects:count:`. Routing non-objects through a
null-callback `CFArray` instead was tried and is worse — Tiger's `NSCFArray` enumerator messages
the element on the second `-nextObject` and hangs the process. Reverted to the reference
behaviour with a comment recording the experiment. Enumerating a non-object-personality table is
unsupported, exactly as on macOS.

**`NSOperationQueue -setSuspended:` — bug, fixed.** The suspend check sat inside the dispatched
block (`if (!_suspended) [op start];`), so an operation dequeued while the queue was suspended was
*discarded*: it never ran after resume, `isFinished` never became YES, and
`waitUntilAllOperationsAreFinished` returned as though it had completed. Now suspend really holds
work back, via `dispatch_suspend`/`dispatch_resume` on the queue, with a matching resume in
`-dealloc` because releasing a suspended queue is a crash in real GCD. Ceiling recorded in the
source: a queue with `maxConcurrentOperationCount` other than 1 runs on the shared global queue,
and `+mainQueue` runs on the main queue, so suspend is a no-op for those two.

**`-[NSOperation waitUntilFinished]` — missing, added.** `WebCoreNSURLSession.mm:367` adds an
operation to the delegate queue and then blocks on it; the selector was neither declared nor
implemented, which is a hard compile failure for that file. Added as a 1 ms poll on `_finished`
rather than a condition variable, to avoid changing the fragile-ABI ivar layout. Ceiling and
upgrade path are in the source comment. `spike/nscompattest.mm` gained a case covering both this
and the suspend fix.

**Reported, not fixed** (out of proportion to the audit, or owned elsewhere):

- `-[NSOperationQueue operationCount]` always returns 0 and `-cancelAllOperations` is a no-op.
  Zero is an affirmative lie a caller can act on, unlike an unimplemented selector.
- `NSOperationQueueDefaultMaxConcurrentOperationCount` is used at `ResourceHandleCocoa.mm:78` and
  is declared nowhere in `compat/`.
- `TIGER_FAST_ENUM_FROM` in `nscompat.m:71` neither retains nor releases the enumerator, so an
  autorelease pool drained inside a `for-in` body leaves it dangling. The same retain/release
  pattern now used in `nscompat-maptable.m` applies.
- `stringByReplacingOccurrencesOfString:` returns a mutable string where Foundation returns an
  immutable one; the `URLBy*` family only handles file URLs; `setObject:nil forKey:` removes
  instead of raising. All harmless for current callers.

`foundationcompat.m` was flagged for reimplementing `CFStringCreateWithBytesNoCopy`, which Tiger
already exports (only the 10.4u SDK header declaration is missing). The file was deleted by the
nscompat track while this audit was running; no action needed.

## compat/dispatch/

Reference: libdispatch-84.5.1 `src/semaphore.c`, plus the documented contracts.

| Primitive | Verdict |
|---|---|
| `dispatch_once` / `dispatch_once_f` | matches, with a deadlock hazard worth recording |
| `dispatch_group_*` | matches |
| `dispatch_semaphore_wait` | matches |
| `dispatch_semaphore_signal` | differs harmlessly |
| `dispatch_after` / timers | matches |
| main-queue drain over CFRunLoop | matches given Tiger's CF |
| `dispatch_sync` / `dispatch_barrier_sync` | matches |

**`dispatch_semaphore_signal` — differs harmlessly.** libdispatch lets `dsema_value` go negative
while threads wait, and `signal` returns non-zero exactly when it had to wake one
(`semaphore.c:296-324`). The shim never lets `value` go below zero, so it always returns 0. No
caller in the WebKit checkout reads the return value (only two call sites, both in
`JavaScriptCore/API/tests/Regress141275.mm`, both discarding it), so this was left alone rather
than adding a waiter counter.

**`dispatch_once` — matches, with a hazard.** The fast path is a plain load plus a barrier, which
is correct on x86 TSO, and the slow path is guarded. But one recursive mutex serialises every
`dispatch_once` in the process, so a `once` block on thread A that blocks waiting for thread B to
finish a *different* `once` deadlocks, where real libdispatch would not. The existing `ponytail:`
comment calls this contention; it is stronger than that. Recorded here rather than fixed — a
per-predicate scheme is a restructure, and nothing in the current test set hits it.

**Group notify and `dispatch_after` — matches.** `td_item_new` copies the block and `td_item_run`
releases it, so `td_group_notify`'s own copy/release round-trip is balanced (one redundant copy,
no leak). `dispatch_after` retains the target queue; `dispatch_group_notify` does not, a minor
asymmetry with no current consequence.

**Main-queue drain — matches.** Tiger has no `CFRunLoopPerformBlock`, so the shim drives a
version-0 `CFRunLoopSource` added to `kCFRunLoopCommonModes` on `CFRunLoopGetMain()`. That is the
correct construction on this CF. The double-checked initialisation of `g_main_src` is unguarded
but benign on x86. Inherent limit: a process whose main run loop never runs never drains the main
queue. Covered by `spike/dispatchtest.mm`.

---

## Findings sent to other owners (not edited here)

### cfcompat.c → **wkcmake**

- `malloc_zone_memalign` (`cfcompat.c:101`) is the only definition of that symbol in the archive
  and is what `bmalloc`'s `SystemHeap::memalign` would reach on a non-Tiger path. On Tiger
  `SystemHeap.cpp:87` already bypasses it for `::aligned_alloc`, so the shim is currently unused
  from bmalloc; worth confirming nothing else expects zone-correct behaviour from it, since
  `posix_memalign`-backed memory belongs to the default zone regardless of the `zone` argument.

### ctcompat.c → **ctcompat**

- **`CTFontCreateUIFontForLanguage` — bug.** 15 of 27 UI font types get the wrong size and 5 lose
  bold. 10.5's implementation is a 32-entry table at `CoreText.i386 __DATA+0x460`
  (`{int uiType; CFStringRef psName; float size; CFStringRef cssName;}`, `-1` terminated), decoded
  in full and handed over. Notably wrong today: SmallEmphasizedSystem (should be 11 bold),
  MiniEmphasizedSystem (9 bold), AlertHeader (13 bold), SystemDetail (9),
  EmphasizedSystemDetail (9 bold), Views (12), MenuTitle/MenuItemMark/MenuItemCmdKey (14),
  UtilityWindowTitle/Toolbar/Palette/ToolTip (11), SmallToolbar (10), ControlContent (12). The
  reference uses PostScript names (`LucidaGrande`, `LucidaGrande-Bold`), which also removes the
  symbolic-traits round-trip for bold. Types outside `0..26` and `1000..1006` return NULL.
- `CTLineGetBoundsWithOptions` matches the documented shape for the two cases WebCore uses
  (10.5 has no such symbol, so this is judged on docs). Five option bits are ignored, none
  reachable; `kCTLineBoundsUseGlyphPathBounds` would be a one-liner over `CTLineGetImageBounds`.
- `CTFontDescriptorCreateWithTextStyle` differs harmlessly. Headline/ShortHeadline use weight 0.4
  (bold) where macOS documents 0.3 (semibold); `lineSpacing = size * 1.2` is invented. No caller
  reads either.
- Survey correction: 10.5 **does** export `CTLineGetTrailingWhitespaceWidth`, so prior art exists
  for the stub at `ctcompat.c:903` if measured-width error ever matters.

### cgcompat.c → **cgcompat**

- **`kCGGradientInterpolatesPremultiplied` dropped — bug.**
  `CGGradientCreateWithColorComponentsAndOptions` discards the options dictionary and
  `evaluateGradient` interpolates unpremultiplied. `GradientRendererCG.cpp:85-96` passes that
  option whenever `alphaPremultiplication == Premultiplied`, which is the CSS default for legacy
  sRGB gradients, so `linear-gradient(red, transparent)` darkens through the middle. ~6 lines in
  `evaluateGradient`.
- **`CGColorSpaceGetName` identity-cache defect.** Matching by pointer identity against
  `sNamedSpaces[].cs` is unsound because Tiger's `CGColorSpaceCreateWithName` caches and returns
  the same object for `GenericRGB`, `GenericRGBLinear` and `GenericXYZ`; whichever name was asked
  for first wins, and the property-list round-trip can change a space's name.
- **Uneven rounded-rect clamping too aggressive.** The shim clamps each corner to half the width
  and height; `PathCG.cpp:212-216` clamps each radius against `rectWidth − oppositeCornerRadius`,
  so `border-radius: 80px 20px` is legal there and gets squashed to 50/20. Clamp adjacent pairs to
  the side length instead.
- Verified correct: the extend-flag handling in `CGContextDrawLinearGradient` /
  `DrawRadialGradient` is byte-for-byte what 10.5 does, including the radial case (no
  radial-specific treatment exists); the rounded-rect kappa `0.5522847498307933` and all four
  corner curves; the corner ordering against `PathCG.cpp`'s flipped-space enum;
  `CGColorSpaceCreateWithName` covers all 13 names WebCore uses.
- Gradient stop divergences, all unreachable: 10.5 returns NULL for out-of-range locations, for
  `count == 1` with NULL locations, and for a NULL colorspace, and it `qsort`s unsorted locations;
  the shim accepts all of these and does not sort. `GradientColorStops::sorted()` sorts first.
- Survey correction: `CG-SURVEY.md` says Tiger's `CGColorSpaceCreateWithName` knows only
  GenericGray/GenericRGB/GenericCMYK. It actually accepts 17 names (the Generic, User,
  SystemDefault and Uncalibrated families plus `DisplayGray`, `DisplayRGB`, `GenericHDR`,
  `Undo601`). No `SRGB`, no `AdobeRGB1998`, no `Device*`.
- Lead worth three lines: 10.5's `CGColorSpaceGetModel` is `return cs ? *(int *)((char *)cs +
  0x10) : -1`, with `GetNumberOfComponents` at `+0x18`. Tiger reads components at `+0x14`, four
  bytes earlier, so the model field is probably at `+0x0C` — unverified, cheap to confirm on the
  box. That would let `CGColorSpaceGetModel` report `kCGColorSpaceModelIndexed`, which it can
  never do today.
