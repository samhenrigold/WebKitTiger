# Shim audit against authoritative references (2026-09-20)

Scope: `compat/` shims checked against Apple open source, macports-legacy-support, and
disassembly of Mac OS X 10.5.8 i386 binaries. Fixes applied to the shims this track owns;
`cfcompat.c` / `ctcompat.c` / `cgcompat.c` findings were sent to their owners instead.

## Criteria (the user's rule)

For every gap, in order:

1. Use what Tiger already exports, including private or older-named symbols. The lists in
   `logs/api/` come from `nm -g` and include private symbols.
2. Failing that, match Apple's implementation, from open source or from disassembly.
3. Failing that, write the best and most performant version we can.

So this audit flags three things beyond ordinary bugs: any shim that reimplements something
Tiger actually has, any stub where a real implementation is reachable under rule 1 or 2, and
any place our behaviour diverges from Apple's in a way a caller could observe.

### Rule 1 sweep: does Tiger already have it?

Mechanical check of all 388 symbols `libtigercompat.a` defines against the 13,817 symbols in
`logs/api/tiger-{libSystem,libobjc,CF,Foundation,CT,CG,AppKit,ImageIO}.txt`, comparing modulo
leading underscores. **No shim reimplements a symbol Tiger exports.** The single near-match is
`__bzero`, which the compiler emits for some zeroing memsets and which Tiger genuinely lacks;
Tiger's own `bzero` exists but is a different symbol.

Names checked individually for a private or older-named Tiger equivalent, all confirmed absent:
thread naming (nothing before 10.6; macports no-ops it too), a 64-bit thread id
(`THREAD_IDENTIFIER_INFO` is 10.6+, so `pthread_mach_thread_np` is the only relative and its
port name is recycled exactly like a `pthread_t`), `posix_memalign` and `malloc_zone_memalign`
(10.6), the `*at()` family (10.10 kernel), `fcopyfile` (Tiger has path-based `copyfile` only),
`_dyld_find_unwind_sections` (10.6), `_tlv_atexit` (no dyld TLV support at all), and every
`dispatch_*` and `os_*` entry point. `realpath` and `pthread_get_stacksize_np` do exist on Tiger
and are correctly not shimmed.

## References used

| Reference | Where |
|---|---|
| objc4-267.1 (the runtime Tiger 10.4.11 actually ships) | `refs/objc4-267.1-objc-class.m`, `-objc-runtime.m`, `-objc-private.h`, `-objc-class.h` |
| objc4-437 (last fragile-ABI runtime, 10.6) | `refs/objc4-437-objc-class-old.m`, `-objc-runtime-old.m`, `-objc-references.mm`, `-objc-accessors.m`, `-Protocol.m` |
| libclosure-63 | `refs/libclosure-63-{runtime.c,data.c,Block_private.h,Block.h}` |
| libdispatch-84.5.1 | `refs/libdispatch-84-semaphore.c` |
| macports-legacy-support (git HEAD) | `refs/mlegacy/src/` |
| Libc-583 (Snow Leopard, the first `posix_memalign`) | `refs/Libc-583-malloc.c`, `-magazine_malloc.c` |
| Mac OS X 10.5.8 i386 frameworks | `refs/leopard/{CoreText,CoreGraphics,Foundation,CoreFoundation,libSystem.B.dylib,libobjc.A.dylib}.i386` |
| Full install trees (10.5.0 GM, WWDC 2006 preview, 10.6.3 build 10D575) | `refs/leopard-9a581/root`, `refs/leopard-9a241/root`, `refs/snowleopard-10.6.3/root` |
| libdispatch-84.5.1 `once.c`, objc4-267.1 `Protocol.m` | `refs/libdispatch-84-once.c`, `refs/objc4-267.1-Protocol.m` |
| Tiger's own CoreText, for the adapter audit | `sysroot/.../CoreText.framework/Versions/A/CoreText` |

The 10.5.8 combo `.dmg` is gone from every Apple host. Apple's legacy Software Update catalog
(`swscan.apple.com/content/catalogs/others/index-leopard-snowleopard.merged-1.sucatalog`) still
serves the same update as a flat pkg, kept at `refs/MacOSXUpdCombo10.5.8.pkg` (805 MB). Note for
anyone extracting more: in 10.5, CoreText and CoreGraphics are subframeworks of ApplicationServices.

All spike tests pass on the Tiger box after these changes: `memaligntest` (new), `objc2test`,
`availtest`, `arctest`, `exctest`, `nsmaptabletest`, `nscompattest`, `dispatchtest`,
`runcxx.sh`, `runfstest.sh`.

`libdispatch-84-semaphore.c`, `Libc-391.5.22-{malloc,scalable_malloc}.c` and
`Libc-583-{malloc,magazine_malloc}.c` were added to `refs/` for this round. Libc-391.5.22 is the
exact Libc 10.4.11 shipped; Libc-583 is Snow Leopard's, the first with `posix_memalign`. The
full install trees the extraction track is unpacking supersede the combo-updater copies in
`refs/leopard/` once their READMEs appear.

---

## compat/libcompat.c

| Function | Reference | Verdict |
|---|---|---|
| `posix_memalign` | macports `posix_memalign_emulation.c`; Libc-391.5.22 `gen/malloc.c` | **bug, rewritten** |
| `strnlen` | macports `strnlen.c` | matches (ours uses `memchr`, same result) |
| `memmem` | macports `memmem.c` (= Apple Libc / FreeBSD) | **bug, fixed** |
| `getline` | macports `getdelim.c` (= BSD/Apple) | **divergence, fixed** |
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

**`posix_memalign` — bug, rewritten.** The old version called `valloc` for every alignment
above 16, so a 16 KB request came back merely 4096-aligned. JavaScriptCore's `MarkedBlock` masks
the block base out of an object pointer, so this corrupted the GC: the jsc shell was crashing on
an uninitialized block footer. macports has the same limitation, so prior art was no help here
and rule 3 applied.

Now: `malloc` at or below 16 bytes of alignment (Tiger's tiny-region quantum), `valloc` at or
below a page, and above that `mmap` of `size + alignment` with the head and tail `munmap`ed so
exactly the aligned region stays mapped. Plain `free()` still works on the result because the
shim registers a `malloc_zone_t` named `TigerAlignedZone`. Libc-391.5.22 `gen/malloc.c` `free()`
calls `find_registered_zone()`, which walks `malloc_zones` in registration order calling
`zone->size(zone, ptr)` and hands the pointer to the first zone that claims it;
`malloc_zone_register()` appends, so the default scalable zone stays `malloc_zones[0]` and our
`size()` is consulted only for pointers it has already rejected. `malloc_size()` and `realloc()`
use the same lookup, so both work on these blocks with no extra code.

Details that matter:

- `size()` must be fast for negative answers. A pointer outside the min/max window of everything
  ever handed out returns 0 with no lock; only a pointer inside the window takes the mutex and
  binary-searches the sorted block table.
- The block table is grown with `malloc_zone_realloc(malloc_default_zone(), …)` rather than plain
  `realloc()`. `realloc()` would call `find_registered_zone()`, which calls every zone's `size()`
  including ours, which wants the lock we are already holding. Naming the zone skips the lookup
  and makes that reentrancy impossible rather than merely unlikely.
- The zone carries a full `malloc_introspection_t` (Tiger's own `szone_introspect` is the model),
  so `leaks` and `malloc_zone_statistics` do not fault on it, and `version = 3` to match
  `create_scalable_zone()`.
- Libc's `realloc()` short-circuits a shrink itself (`if (zone && old_size >= new_size) return
  old_ptr`), so the zone's `realloc` is only reached to grow, where it keeps the caller's
  alignment and copies.
- Apple's libmalloc semantics where observable: `EINVAL` for alignment 0, a non-power-of-two, or
  below `sizeof(void *)`; `ENOMEM` on failure; `memptr` untouched on error; size 0 returns a
  unique freeable pointer.

New test `spike/memaligntest.c`, runner `spike/run-memaligntest.sh`, 84 checks passing on the
Tiger box: alignments 16, 64, 4096, 16 KB, 64 KB and 1 MB crossed with sizes 1 byte to 3 MB with
every block filled and read back; the three `EINVAL` cases; size 0; 64 live 16 KB blocks
interleaved with ordinary `malloc` and freed out of order; 200 alloc/free cycles at 64 KB;
`realloc` shrink and grow; `malloc_size` on our pointers, on `malloc`'d and `valloc`'d pointers
and on a stack address; `aligned_alloc`; and 8 threads doing 200 16 KB alloc/free rounds each.

**Rule 2 check against Apple's own implementation.** Libc-583 is the first Libc with
`posix_memalign`, and its `szone_memalign` (`magazine_malloc.c:5748`) handles alignments above a
page in its last branch by calling `large_malloc(…, MAX(vm_page_shift, __builtin_ctz(alignment)),
…)` → `allocate_pages` (`magazine_malloc.c:940`). That function is what this shim now does, step
for step: round the request to a page, substitute one page when it is zero, add `1 << align` to
the allocation, bail if the sum wrapped, `mmap(0, …, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE)`,
round up to the aligned address, `munmap` the head delta, `munmap` the tail. The only structural
difference is bookkeeping: Apple records the block in the scalable zone's own large-entry table so
that zone's `size()` finds it, which is only possible from inside libc, so we register a separate
zone instead.

Apple's smaller-alignment branches split blocks inside the tiny and small regions using szone
metadata, which needs the same inside-libc access; `malloc`/`valloc` reach the same result here.
Also worth recording: `malloc_zone_memalign` (`Libc-583-malloc.c:660`) refuses any zone with
`version < 5` or a null `memalign` field, and Tiger's `malloc_zone_t` (version 3) has no such
field at all. So offering memalign through Tiger's zone ABI is structurally impossible, not just
unimplemented.

Downstream note sent to wkcmake: `SystemHeap::free` uses `malloc_zone_free(m_zone, …)` with
`m_zone` forced to the default zone on Tiger, which would not free one of these over-aligned
blocks. It does not have to today, because bmalloc sends large aligned requests to
`memalignLarge`/`tryVMAllocate`, but if that routing changes its free path must go through plain
`free()` or `malloc_zone_from_ptr()`.

**`getline` — divergence, fixed.** On a read error with a partial line already buffered, BSD
`getdelim` (what macports ships and what Apple's Libc has) returns -1; ours returned the partial
line, which would let a caller treat truncated input as a complete last line. Now checks
`ferror()` and only reports a clean EOF with nothing read as the end. Initial buffer also changed
from 128 to `BUFSIZ` to match.

**`clock_gettime` — differs harmlessly.** Any clock id other than `CLOCK_REALTIME` is treated as
monotonic and returns 0 rather than `EINVAL`. `clock_getres` always reports 1 µs. Real 10.12+
reports per-clock resolution. Nothing reads it.

**`pthread_threadid_np` — differs harmlessly.** Returns the `pthread_t` value, not the kernel's
64-bit thread id. Unique among live threads, so `WTF::Thread` identity comparisons are correct,
but the value will not match a ktrace or `thread_info` reading, and it repeats when a `pthread_t`
is recycled.

**Rule 1, gap survey against macports.** Checked what macports implements that Tiger lacks and we do not
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

**`-[NSOperationQueue operationCount]` — divergence, fixed.** It returned a flat 0, which is an
affirmative wrong answer a caller can act on, unlike an unimplemented selector. Now an
`int32_t` ivar bumped with `__sync_fetch_and_add`/`_sub` around the dispatched body, covering
both `-addOperation:` and `-addOperationWithBlock:`. Two new checks in `spike/nscompattest.mm`.

**`-[NSOperation waitUntilFinished]` — missing, added.** `WebCoreNSURLSession.mm:367` adds an
operation to the delegate queue and then blocks on it; the selector was neither declared nor
implemented, which is a hard compile failure for that file. Added as a 1 ms poll on `_finished`
rather than a condition variable, to avoid changing the fragile-ABI ivar layout. Ceiling and
upgrade path are in the source comment. `spike/nscompattest.mm` gained a case covering both this
and the suspend fix.

**Reported, not fixed** (out of proportion to the audit, or owned elsewhere):

- `-[NSOperationQueue cancelAllOperations]` is still a no-op. No caller in the checkout uses it,
  and doing it properly needs a list of live operations rather than just a count, so it is
  flagged rather than built out.
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
| `dispatch_semaphore_signal` | **divergence, fixed** |
| `dispatch_once` locking | **hazard, fixed** — see the second pass below |
| `dispatch_after` / timers | matches |
| main-queue drain over CFRunLoop | matches given Tiger's CF |
| `dispatch_sync` / `dispatch_barrier_sync` | matches |

**`dispatch_semaphore_signal` — differs harmlessly.** libdispatch lets `dsema_value` go negative
while threads wait, and `signal` returns non-zero exactly when it had to wake one
(`semaphore.c:296-324`). The shim never lets `value` go below zero, so it always returns 0. No
caller in the WebKit checkout reads the return value (only two call sites, both in
`JavaScriptCore/API/tests/Regress141275.mm`, both discarding it), so this was left alone rather
than adding a waiter counter.

**`dispatch_once` — hazard, fixed in the second pass.** The fast path was a plain load plus a
barrier, correct on x86 TSO, but one recursive mutex serialised every `dispatch_once` in the
process, so a `once` block on thread A that blocked waiting for thread B to finish a *different*
`once` deadlocked. Replaced with libdispatch's per-predicate compare-and-swap; details below.

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

## Divergences from Apple that remain, by design

Flagged under criterion 3, each with the reason it was not closed.

| Shim | Divergence | Why it stands |
|---|---|---|
| `libcompat.c` `pthread_threadid_np` | returns the `pthread_t`, not a kernel thread id | no better source exists before 10.6; a mach port name is recycled the same way |
| `libcompat.c` `clock_getres` | always reports 1 µs | Tiger has no per-clock resolution to report; nothing reads it |
| `libcompat.c` `fdopendir` | leaks one fd per nesting level of a `remove_all()` | real `fdopendir` keeps the caller's fd, which is impossible without kernel `*at()` support |
| `objc2compat.m` `method_setImplementation`, `method_exchangeImplementations` | flushes all method caches; objc4-437 does not flush at all | the flush is unnecessary, not wrong — old-runtime cache buckets are `Method` pointers — and both are cold paths, so a working path was left alone |
| `blockclasses.m` | the placeholder class structs share a `cache` pointer with the real classes | unreachable: no instance ever has the original class as its `isa` |
| `dispatch.c` `dispatch_apply` | runs iterations serially on the calling thread | farming them out to the fixed pool risks self-deadlock when the caller is itself a pool thread |
| `dispatch.c` `dispatch_group_notify` | does not retain the target queue, where `dispatch_after` does | asymmetry with no current consequence |
| `os.c` `os_unfair_lock` | hand-rolled CAS spin with `pause`/`sched_yield` rather than Tiger's `OSSpinLock` | `OSSpinLock` is a bare word and cannot carry the owner id that `os_unfair_lock_assert_owner` needs |
| `nscompat-operation.m` `-setSuspended:` | a no-op on `+mainQueue` and on queues with `maxConcurrentOperationCount != 1` | those run on queues we do not own; real suspend there needs a holding array |
| `nscompat.m` `TIGER_FAST_ENUM_FROM` | enumerator not retained | reported to the nscompat track with the fix pattern |
| `nscompat-operation.m` `-waitUntilFinished` | 1 ms poll, not a condition variable | real Foundation uses mutex + condvar; upgrade path now recorded in the source with the reference address |
| `nscompat-operation.m` `-cancelAllOperations` | no-op | real Foundation sends `-cancel` to every operation; needs a live-operation list, and nothing in WebKit calls it |
| `objc2compat.m` protocol optional methods and properties | return empty | the ext record is destroyed at image load; recoverable only through the local symbol table, see below |

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
- Survey correction: 10.5 **does** export `CTLineGetTrailingWhitespaceWidth`. (My first note said
  the shim returns 0 there; that was read from a stale copy. It already sums the advances of
  trailing space glyphs, and the ctcompat track has corrected the survey.)

**Second round, after the UI font table was fixed.** Audited the ten Tiger-ABI adapters at the
bottom of `ctcompat.c` on request, against Tiger's own CoreText binary.

- **`CTLineDraw` needs an eleventh adapter — the one confirmed signature mismatch left.** Tiger's
  is `CTLineDraw(CTLineRef, CGContextRef, CFRange)`. `_CTLineDraw` at `901cfa84` reads four stack
  words and at `901cfabe` does `leal (%esi,%ecx),%eax` then compares against a count, bailing out
  and drawing nothing when `location + length` exceeds it; `{0, 0}` and `{0, count}` both route to
  `TLine::DrawGlyphs(ctx)` for the whole line. WebCore calls the two-argument form at five sites
  (`ResourceUsageOverlayCocoa.mm:286`, `LegacyTileCache.mm:596`, `PlatformCALayer.mm:169`,
  `DrawGlyphsRecorder.cpp:525`, `WebViewVisualIdentificationOverlay.mm:159` and `:163`), so the
  range is stack junk and the line usually does not draw at all. One-line adapter.
- **The three `CTRunGet*` copying adapters leave the caller's buffer untouched when the `…Ptr`
  variant returns NULL**, which is the only case WebCore calls them
  (`ComplexTextControllerCoreText.mm:74-107` calls the copying variant exactly in the `!ptr`
  branch, after a `Vector::grow()` that does not zero POD). The caller then reads uninitialized
  memory; for `m_coreTextIndices` those values index into the character buffer. Disassembling the
  three accessors: `TStorageRange::GetGlyphs` (`901f8fb8`) and `GetAdvances` (`901f984c`) are
  unconditional pointer arithmetic and effectively never return NULL, but `GetStringIndices`
  (`901f9782`) dispatches through a virtual and genuinely can. Zero-fill at minimum;
  `CTRunGetStringRange` is real on Tiger (`901d1fc6`) and can reconstruct indices for a monotonic
  run.
- `TigerCTFontGetBoundingRectsForGlyphs` returns `CGRectZero` on failure where real CT returns
  `CGRectNull`, and callers tell them apart with `CGRectIsNull`.
- `TigerCTRunDraw` uses a different placement model from real CTRunDraw, which fetches
  `TRun::GetPositions()` and never reads the context's text position (10.5 `CTRunDraw` at
  `0x30474`). Rule 1 offers nothing better: Tiger exports neither `CTRunGetPositions` nor
  `…Ptr`, and `TRun::GetPositions` is a local symbol, so accumulating advances is the right call.
  It does mean the adapter requires a per-run text position, and it ignores `CTRunGetTextMatrix`.
  No WebCore caller today. `TigerCTLineGetImageBounds` shares the approximation and is likewise
  uncalled outside `ctcompat.c` itself.
- Verified correct: `TigerCTFontCopyTable`, `TigerCTLineGetTypographicBounds`, `runRangeCount`,
  `TigerCTFontGetAdvancesForGlyphs`, `TigerCTFontCreateUIFontForLocale`. Also re-verified the
  premises the adapters rest on: Tiger's `CTRunGetGlyphs` (`901e2212`), `CTRunGetAdvances`
  (`901e2218`), `CTRunGetStringIndices` (`901e221e`) and `CTRunDraw` (`901e21ae`) are each
  `push ebp; mov ebp,esp; pop ebp; ret`, and `CTLineGetImageBounds` (`901e0c24`) copies 16 bytes
  from a fixed global into the struct return without touching the line.
- Useful side fact: Tiger's `CTRunGetStatus` (`901d6076`) sets bit 0 for right-to-left and bit 1
  for non-monotonic, and **never** bit 2, `kCTRunStatusHasOrigins`. So WebCore's `HasOrigins`
  branch, which calls the absent `CTRunGetBaseAdvancesAndOrigins`, can never be taken.

**Method note.** The way `CTLineDraw` surfaced generalises: for each of the 54 CoreText functions
WebCore calls that Tiger exports, count the highest positive `%ebp` offset the prologue reads and
compare with the modern prototype's argument count. It produces false positives on short
functions, so the candidates need hand-checking — `CTFramesetterCreateFrame` reads exactly the
modern five slots and `CTFontGetDescent` exactly one (and negates the result, so it returns a
positive descent like modern CT). Beyond the ten adapters and `CTLineDraw`, nothing else in that
set looked mismatched. The same screen is worth running against CoreGraphics.

**Fixed by the ctcompat track**, including one bug this audit did not catch: `kCTFontUIFontMenuItem`
was 10 and `kCTFontUIFontLabel` was 20, where the correct values are 12 and 10. Even a correct
table would have returned the label font wherever WebCore asked for the menu item font.

### cgcompat.c → **cgcompat**

- **`kCGGradientInterpolatesPremultiplied` dropped — bug.**
  `CGGradientCreateWithColorComponentsAndOptions` discards the options dictionary and
  `evaluateGradient` interpolates unpremultiplied. `GradientRendererCG.cpp:85-96` passes that
  option whenever `alphaPremultiplication == Premultiplied`, which is the CSS default for legacy
  sRGB gradients, so `linear-gradient(red, transparent)` darkens through the middle. (My first
  description of the repro was wrong: red to transparent *red* shows no difference, because the
  colour is constant. The artifact needs CSS `transparent`, which is transparent *black*, and it
  is black that the midpoint is dragged toward. The cgcompat track caught this.)
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

---

## Systematic ABI screen over CoreText, CoreGraphics and CoreFoundation

`CTLineDraw` was found by hand-counting stack offsets, and ctcompat's point that "three of this
class now, and a systematic screen beats finding them one at a time" is right.

**A screen already existed.** `tools/abi-screen.py` was committed in `1aa43fc` by another track,
and it is better than the one I wrote: it takes the modern side from clang's own i386 lowering
(`-target i386-... -emit-llvm`) rather than parsing headers, so sret, byval and struct flattening
are exact rather than modelled, and it accounts for access width, which mine did not (a trailing
`double` arrives as one `movsd 0xc(%ebp)`, and counting the displacement alone undercounts it by
four bytes). I deleted my duplicate. What follows is therefore an independent re-derivation that
happens to cross-check theirs, plus two detectors their tool does not have.

The three ways a Tiger function can link cleanly and still not work, none of which produce a
warning, an error or a crash:

1. **Exported but empty.** `CTRunGetGlyphs` is `push ebp; mov ebp,esp; pop ebp; ret`, so it leaves
   the caller's buffer untouched.
2. **Exported but ignores its arguments.** `CTLineGetImageBounds` copies a fixed global into its
   struct return and never looks at the line.
3. **Exported with a different signature.** `CTLineDraw` takes `(line, context, CFRange)`.

Mode 3 is what `tools/abi-screen.py` already does. My version compared the highest incoming-argument
slot each function *reads* against the i386 stack slots the modern prototype implies, parsed from
the host SDK headers. Two details were load-bearing, and both were bugs in my first version, which
is the main reason this is worth writing down: the other track avoided both by not parsing headers
at all.

- `-0x20(%ebp)` is a local, `0x20(%ebp)` is an argument. Without the sign check every function with
  a stack frame looks mismatched; the first run returned 12 CoreText candidates, 9 of them noise.
- Attribute macros trail the return type (`CG_EXTERN CGAffineTransform CG_PURE`), so the return
  type is the last word that is not such a macro. Otherwise the hidden struct-return slot is never
  counted and every `CGAffineTransform`-returning function looks like it reads one slot too many.
  That alone accounted for all 9 CoreGraphics candidates in the first run.

A zero-argument function that returns a constant is just a constant (`CFArrayGetTypeID`), so mode 1
only reports a constant return when the prototype declares at least one argument.

### Results

With both corrections the screen had **no false positives** across the 436 functions WebCore calls
that Tiger exports. The CoreFoundation and CoreGraphics results agree with `logs/abi-screen-cf.md`,
reached by a different method, which is the useful part of having duplicated the work.

| Framework | Compared | Empty / constant | Ignores arguments | Signature mismatch |
|---|---|---|---|---|
| CoreText | 48 | 3 | 0 | 3 |
| CoreGraphics | 200 | 0 | 0 | 0 |
| CoreFoundation | 188 | 0 | 0 | 0 |

Every CoreText hit is already known and handled: the three empty run getters and `CTLineDraw` are
fixed in `ctcompat.c`, and `CTFontCreateWithName` / `CTFontCreateWithGraphicsFont` take `double
size` where modern CoreText takes `CGFloat`, which the SDK overlay already declares correctly
(`compat/sdk-overlay/CoreText.framework/Headers/CTFont.h:91,93`). Confirmed in the binary: Tiger's
`CTFontCreateWithName` does `movsd 0xc(%ebp), %xmm0` and calls a constructor whose mangled name is
literally `CTFont::CTFont(__CFString const*, double, CGAffineTransform const*)`.

**CoreGraphics and CoreFoundation are clean for everything WebCore calls**, agreeing with the
other track's independent run over CoreFoundation, ATS, LaunchServices and Security. That is a
useful negative result: the CoreGraphics problems found this round were not signature mismatches
but behavioural ones, which no static screen can catch.

### Modes 1 and 2, which the existing tool does not cover

The empty-body and argument-ignoring detectors are additive and have been handed to the dispatch
track, which owns `tools/abi-screen.py`, rather than kept as a second tool.

**Correction to the record.** The copy of the argument-ignoring detector I committed in `476eb43`
was dead: it built the set of argument slots read and then compared that set against a list, which
is never equal, so the mode silently reported nothing. The `CTRunGetImageBounds` finding came from
an earlier working draft. It went unnoticed because the expected answer over WebCore's call set was
also zero, so the output looked right. Re-verified over all 243 Tiger CoreText exports: comparing
`sorted(reads)` finds both `CTLineGetImageBounds` and `CTRunGetImageBounds`, and the committed
comparison finds neither. The hand-off carries the fix and the flag; the file itself is deleted. Over every export rather than WebCore's
callers, CoreText has four more empty or constant stubs (`CTRunDraw`,
`CTFontCreateUIFontForLocale`, `CTFontCreateWithQuickdrawNameAndStyle`, `CTRunGetEmbeddedObject`),
and a second argument-ignoring function alongside `CTLineGetImageBounds`: **`CTRunGetImageBounds`
has the identical shape**, 17 instructions copying a global into the struct return. Sent to the
ctcompat track. CoreGraphics's 22 stubs over all exports are private `CGS*`, PDF and halftone
internals, none reachable from WebCore; CoreFoundation's two are `CFMachPortInvalidateAll` and
`___CFA2UC`.

### The fourth mode, which no static screen catches

cgcompat's find this round was `CGShading` silently discarding the alpha its function returns: the
signature matches, the arguments are read, and the behaviour is still wrong. That needs a runtime
probe on the box, not disassembly. Worth keeping in mind when a shim looks correct by inspection
and the output is still wrong.

---

## Behavioural probes: the failure mode no static screen reaches

The ABI screen finds functions whose shape is wrong. It cannot find a function whose signature
matches, whose arguments are read, and whose behaviour is still wrong. Three of those have now
turned up on this port, so they are worth probing deliberately rather than discovering through a
rendering bug.

Known so far, both from the cgcompat track: Tiger honours **none** of the twelve 10.5 Porter-Duff
blend modes, all of them compositing as `kCGBlendModeNormal` (`spike/blendtest.c`), and Tiger's
`CGShading` discards the alpha its function returns.

### `CGContextClipToMask` — correct for gray, silently fatal for anything else

`spike/clipmasktest.c`, runner `spike/run-clipmasktest.sh`. This was flagged as unmodellable after
the gradient work saw destination alpha that looked like mask times source colour. Measured, that
is not what happens.

| Mask | Result |
|---|---|
| DeviceGray, no alpha | **correct**, matches the documented semantics exactly |
| `CGImageMaskCreate` stencil | clips everything away |
| RGBA image | clips everything away |

With a gray mask the destination alpha is the mask sample alone and does not move when the fill
colour changes: white, red, mid grey and black all give `a=128` through a mask of 128. The colour
channels are colour times mask, which is what premultiplied means, so filling mid grey through
mask 128 gives `r=64 a=128` — and reading `r` as the alpha yields exactly "mask times source
colour". Verified across five mask values, four fill colours, and a four-pixel ramp with mask
0/85/170/255 under a red-to-blue colour ramp, where alpha came back 0/85/170/255 exactly.

The probe controls for the obvious objection that the rejected masks were malformed: both render
correctly through `CGContextDrawImage`, so they are well formed and `ClipToMask` is what rejects
them, with no error and no diagnostic.

**Consequence outside the shims.** `GraphicsContextCG::clipToImageBuffer`
(`GraphicsContextCG.cpp:1078`) passes an RGBA image, and the call site already carries a FIXME
saying the image needs to be grayscale. On Tiger that call does not mask, it blanks every
subsequent drawing operation in the clipped region. Sent to wkcmake; the fix is a grayscale
conversion at the call site, in WebCore rather than in compat. The symptom is misleading — no
crash, no error return, content simply absent — so it is worth checking first if masked content
goes missing.

### Text antialiasing — the knob exists, just not the one being used

`spike/fontsmoothtest.c`, runner `spike/run-fontsmoothtest.sh`. Measured on both text paths:
`CGContextSelectFont` with `CGContextShowTextAtPoint`, which needs no `CGFontRef` at all, and the
`CGFontRef` plus `CGContextShowGlyphsWithAdvances` path WebCore actually uses. Results are
identical on both. `CGFontCreateWithFontName` and `CGFontCreateWithDataProvider` return NULL on
Tiger, so the `CGFontRef` comes from `ATSFontFindFromName` and `CGFontCreateWithPlatformFont`.

| Entry point | Behaviour |
|---|---|
| `CGContextSetShouldSmoothFonts` | **no-op**, byte-identical pixels, never a colour fringe |
| `CGContextSetAllowsFontSmoothing` | **no-op** |
| `CGContextSetShouldAntialias` | works, context-wide, on paths and glyphs |
| `CGContextSetAllowsAntialiasing` | works, gates the should-flag |
| `CGFontSetShouldAntialias` (private) | **works, per font** |

So `cgcompat.c`'s mapping of `CGContextSetShouldAntialiasFonts` onto `SetShouldSmoothFonts`
silently drops the request. The faithful target is the private `CGFontSetShouldAntialias`: a glyph
run goes from 593 inked and 542 antialiased pixels to 235 and 0, and because the flag lives on the
font (bit 0 of a byte at `font+0x3c`, per the disassembly of the setter and of
`CGFontShouldAntialias`) shapes in the same context stay smooth, which the context-wide knob
cannot manage. Two hazards for whoever wires it: `CGContextGetFont` exists, but WebCore usually
sets the font after configuring state, and the flag mutates a shared, cached `CGFont`.

Rule 1 again, and the third time it has paid: this was found by grepping every Tiger CG export
matching smooth or antialias, not by reasoning about what should exist.

**`CGContextSetFontAntialiasingStyle` has nothing to map to.** Tiger exports no
`CGContextSetFontRenderingStyle` and nothing style-shaped; the complete smoothing surface is the
context Should/Allows pairs, their GState and RenderingState backings, the per-font flag,
`CGFontAllowsFontSmoothing` (no arguments, reads a process-wide global) and a
`__CGFontSmoothingMode` data symbol. A no-op is the honest implementation.

### Interpolation quality is binary

`spike/interptest.c`, runner `spike/run-interptest.sh`. Upscaling a 4x4 checkerboard to 32x32 once
per quality: **only `None` is distinct.** `Default`, `Low`, `Medium` and `High` render
byte-identically. All five values are stored and read back unchanged, so the rasterizer collapses
them rather than the setter rejecting them, which means a shim cannot detect the loss by reading
state back. `CGContextGetInterpolationQualityRange` reports `[0, 0]`, and that declaration was
verified against the disassembly rather than guessed, since the 10.4u SDK does not carry it.

The consequence: anywhere WebCore picks `Low` or `Medium` to trade quality for speed, Tiger gives
it `High`.

### The rest of the list, diffed against modern CoreGraphics

`spike/cgbehaviour.c`, runner `spike/run-cgbehaviour.sh`. This one builds and runs on **both**
Tiger and the host Mac, printing the same `KEY=value` lines, so modern CG is the reference and the
result is a diff rather than a judgement about what Tiger ought to do. That paid for itself twice
before it found anything: the host run showed my first transparency-layer test measuring nothing,
because grouping only shows against a *global* alpha and I had set a per-fill alpha; and it exposed
a y-axis mistake that made every point sample read an empty pixel **on both platforms**, which
reads as agreement rather than as a bug.

| Checked | Verdict |
|---|---|
| `CGPatternCreate` tiling argument | matches modern |
| `CGPatternCreateWithImage2` tiling argument | matches modern |
| `CGContextBeginTransparencyLayer` under scaled / rotated / composed CTM | matches modern |
| `CGContextSetLineDash` phase, and clearing the dash | matches modern |
| `CGContextClipToRects` | matches modern |
| `CGImageCreateWithMaskingColors` | matches modern |
| `CGContextSetShadowWithColor` across blur 0..32 | **differs: blur saturates** |
| `CGContextDrawTiledImage` (cgcompat's shim) | **bug: seams** |

**Pattern tiling is honoured.** `kCGPatternTilingNoDistortion` renders differently from the two
constant-spacing modes on *both* platforms, and the two spacing modes agree with each other on
both, so the argument is not being dropped. `kCGPatternTilingConstantSpacing`, which is what
WebCore passes at `PatternCG.cpp:77` and `GraphicsContextCG.cpp:530`, behaves as on modern.

**Transparency layers group correctly under a non-identity CTM.** Scaled, rotated and composed
transforms all show the same layer-versus-no-layer alpha drop as modern, within 0.05%.

**Shadows: a stable ratio, but the blur saturates.** Tiger carries 91.7% of modern's total shadow
ink, and that ratio holds from 0.905 to 0.949 across blur 0 to 32 — stable enough that a uniform
alpha correction would fix it. It should not be shipped, because total ink is not what is visibly
wrong. Up to blur 6 the geometry matches and the peak is 255 on both, so the 8% is edge
antialiasing and nearly invisible. From blur 8 up Tiger stops spreading: at blur 32 it covers 54%
of the area at 3.3 times the peak alpha. Those shadows are already too dark and too tight, and
scaling alpha would make them worse.

| blur | ink ratio | area ratio | peak Tiger/host |
|---|---|---|---|
| 0 | 0.911 | 0.911 | 255 / 255 |
| 6 | 0.914 | 0.912 | 255 / 255 |
| 8 | 0.919 | 1.005 | 243 / 247 |
| 16 | 0.916 | 0.698 | 195 / 128 |
| 32 | 0.949 | 0.543 | 134 / 41 |

**`CGContextDrawTiledImage` seams — a bug in the shim.** Tiger does not export the function at all,
so this is `cgcompat.c`'s draw loop. It covers exactly the right pixels in every clip tested, but a
fractional tile origin loses about 6% of the alpha to seams between tiles, where the real function
stays fully opaque. An integer-aligned origin gives exactly `inked * 255`, which proves the cause
is fractional destination rects antialiasing against each other rather than anything about the
clip. `GraphicsContextCG.cpp:517` passes a `FloatRect` straight from layout, so this fires on any
repeated background. The remedy is to round each tile's destination rect to the pixel lattice, or
to disable antialiasing around the loop.

---

## Open items that are not shim work

Three findings from this audit land outside `compat/` and are tracked here so they do not get lost
between tracks.

| Item | Owner | State |
|---|---|---|
| `GraphicsContextCG::clipToImageBuffer` passes an RGBA image to `CGContextClipToMask`, which on Tiger clips everything away and blanks subsequent drawing | WebCore, sent to wkcmake | needs a grayscale conversion at `GraphicsContextCG.cpp:1078`; the call site's own FIXME already says the image ought to be grayscale |
| Tiger honours none of the twelve Porter-Duff blend modes, all compositing as Normal | WebCore | **cannot be shimmed**: the mode is context state consumed by every later drawing call, not a parameter to intercept. `NativeImageCG`'s single-pixel colour read depends on `kCGBlendModeCopy` replacing an uninitialized buffer, so it returns a wrong colour |
| Protocol ext records, needed by JavaScriptCore's JSExport | build flags | **decided**: NOTES.md LINK RULE 2, never strip local symbols |
