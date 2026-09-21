# 64-bit JavaScriptCore with the x86_64 JIT on Mac OS X 10.4.11

2026-09-20, agent `jit64`. Worktree `WebKit-jsc64`, branch `tiger-jsc64`, commit 45f51600.

## Verdict

It works. `jsc` built for `x86_64-apple-macosx10.4` from today's WebKit runs on
the 10.4.11 box with the full JIT (LLInt + baseline + DFG) and passes the same
smoke suite the i386 C-loop build passes, byte for byte against the host.

The 2,000,000-iteration loop from `spike/TigerBrowser/testpages/script.html`:

| Configuration | 2M loop | fib(30) | fib(25) |
|---|---|---|---|
| jsc64, full JIT (LLInt + baseline + DFG) | 51 ms | 55 ms | 4 ms |
| jsc64, `--useDFGJIT=false` (baseline only) | 122 ms | 118 ms | 9 ms |
| jsc64, `--useJIT=false` (LLInt asm interpreter) | 511 ms | 370 ms | 33 ms |
| i386 jsc, C loop (current port, from NOTES) | 2240 ms | — | 135 ms |
| Safari 4.1.3, i386, 2010 JIT (from NOTES) | 59 ms | — | — |
| Tiger's own 2007 WebKit, no JIT (from NOTES) | 5300 ms | — | — |

Six consecutive runs of the loop under the full JIT gave 51 ms every time.

So the 64-bit JIT is **44x faster than the i386 C-loop build we ship today** and
**slightly faster than Safari 4.1.3's own JIT** on the same machine. The JIT is
genuinely engaged, three ways: the 10x gap against `--useJIT=false`, the 2.4x
gap against `--useDFGJIT=false`, and `--reportTotalCompileTimes=true` reporting
2.2 ms of DFG compile time. `--dumpDisassembly=true` prints real x86_64 machine
code out of the JIT arena.

## The three findings that are not Tiger-specific

**WebKit assumes every x86_64 Mac has AVX.** `MacroAssemblerX86_64.cpp`'s
`collectCPUFeatures()` read `#if OS(DARWIN) / s_avxCheckState = Set` instead of
testing CPUID bit 28, on the reasoning that every Mac Apple still supports has
AVX. A 2007 Core 2 Duo (Merom) does not. The DFG emitted `vmovq %r10, %xmm0` and
the process died with SIGILL on its first compile. Restoring the CPUID check is
a one-line fix and costs modern machines nothing. This was the single hardest
failure to find, because the crash was inside JIT-generated code that no
debugger on the box can see.

**`vmTagFd()` passes a VM tag as mmap's fd.** `BVMTags.h` returns
`VM_MAKE_TAG(n)` for the `fd` argument on Darwin. That convention arrived in
10.5; 10.4's `mmap` rejects it with `EINVAL`. Every reservation failed, so
`StructureMemoryManager`'s eight halving attempts all came back null and
`RELEASE_ASSERT(g_jscConfig.startOfStructureHeap)` fired. The tag is only for
`vmmap` accounting, so returning -1 on Tiger costs nothing but diagnostics.

**`InlineCacheCompiler.h` uses an incomplete type.** It declares members
returning `CCallHelpers::Jump` and `CCallHelpers::JumpList` with only a forward
declaration of `CCallHelpers`. Every normal build happens to include
`CCallHelpers.h` first; `LLIntOffsetsExtractor`, reaching the header through
`BytecodeStructs.h`, does not.

## The Tiger-specific gaps

These are the places where the 10.4 x86_64 userland is poorer than either modern
macOS or the i386 side of the same machine.

**The main thread's stack is reported wrong.** `pthread_get_stackaddr_np()`
returns the 32-bit constant `0xc0000000`, and `pthread_get_stacksize_np()`
returns 512 KB, even inside a 64-bit process whose stack actually sits just
below `0x00007fff5fc00000`. Secondary threads report correctly. WTF believed the
bogus origin, and the LLInt's `sanitizeStackForVM` started zeroing memory upward
from `0xc0000000` and faulted. `StackBounds.cpp` now asks `mach_vm_region()` for
the region containing a stack local and takes both ends from it, which is ground
truth for origin and size alike. This is the fix I am least happy about leaving
unexercised at depth: it is only used for the main thread, and only on this
platform, so a bad day would look like a stack-overflow check that never fires.

**No x86_64 CoreFoundation, Foundation or Objective-C runtime.** This removes,
for this target: `TimeZoneCocoa.cpp` and `USE(TIME_ZONE_CHANGE_NOTIFICATIONS)`
(`wtf/TimeZone.cpp`'s own no-op listener is the whole implementation), bmalloc's
`ProcessCheck.mm` (`gigacageEnabledForProcess()` falls back to the inline `true`,
`processNameString()` becomes `getprogname()`, which is what the .mm was asking
Foundation for), and the `<CoreFoundation/CoreFoundation.h>` include in the
public `WebKitAvailability.h`. The 10.4u SDK's Carbon headers are not merely
absent for x86_64, they are actively hostile: `MacTypes.h` opens with
`#pragma options align=mac68k`, which clang rejects for this target, and
`CoreServices.h` and `objc/objc.h` both `#error "64-bit not supported"`.

`wtf/darwin/OSLogPrintStream.mm` turned out to contain no Objective-C at all,
only an `#error` demanding ARC, so it is compiled as C++ here against the
`os_log` polyfill.

**No libdispatch, so no `os_log`/`os_unfair_lock` from the usual place.** The
i386 polyfill in `compat/dispatch` is built on CFRunLoop. Its `os.c` half is
pure libSystem and now has its own 64-bit build (`libtigerdispatch-x86_64.a`),
which supplies `os_unfair_lock` for libpas's `pas_lock` and `os_log` for
`OSLogPrintStream`. `pas_mte.h` and `pas_mte_config.h` include
`<dispatch/dispatch.h>` unconditionally on Darwin and are gated off (MTE is
ARM64-only regardless). bmalloc's `BUSE_OS_LOG` is off.

**`mach_exc.defs` is 10.5+.** 10.4 ships only `exc.defs`, the 32-bit exception
codes. `HAVE(MACH_EXCEPTIONS)` is undefined and the MIG step is skipped, so WTF
falls back to POSIX signal handlers.

**The 10.4u SDK's `libedit.dylib` has no x86_64 slice**, so `HAVE(READLINE)` is
off and the `jsc` shell's interactive REPL uses plain `getline`.

**Register names.** The 10.5 SDK renamed `struct mcontext`'s members to
`__es`/`__ss`/`__fs` and `x86_thread_state64`'s to `__rsp` and friends. On 10.4
both are unprefixed, exactly as the existing `PLATFORM(TIGER)` handling already
assumes for i386, so `MachineContext.h` and `PlatformRegisters.h` gained matching
64-bit branches.

**`getsegmentdata()` needed an LP64 form** (`compat/include/sdk-fill/mach-o/getsect.h`),
and `cfcompat.c`'s `_dyld_get_image_uuid()` was walking load commands from
`mh + 1` with a 32-bit `struct mach_header`, four bytes short of where they start
in a 64-bit image. That one would have silently returned a wrong UUID rather
than crashing.

**`CommonCrypto` is 10.5+**, so `RandomDevice` takes the `/dev/urandom` path,
and `struct stat` has no `st_birthtime`, both as on i386.

## Patch list

WebKit worktree, branch `tiger-jsc64`, 28 files, **209 insertions, 35 deletions**.
Most of that is comments; the executable change is well under 100 lines.

| File | Lines | What |
|---|---|---|
| `WTF/wtf/StackBounds.cpp` | +44 | `mach_vm_region()` main-thread stack bounds |
| `JavaScriptCore/runtime/MachineContext.h` | +21/-0 | unprefixed 64-bit register names |
| `WTF/wtf/PlatformJSCOnly.cmake` | +18/-4 | skip MIG, TimeZoneCocoa |
| `bmalloc/bmalloc/BPlatform.h` | +16/-1 | `BPLATFORM_TIGER64`, `BUSE_OS_LOG` off |
| `bmalloc/bmalloc/ProcessCheck.h` | +14/-1 | `getprogname()`, inline gigacage check |
| `WTF/wtf/cocoa/MemoryFootprintCocoa.cpp` | +14 | `TASK_BASIC_INFO` resident size |
| `libpas/src/libpas/pas_platform.h` | +12/-1 | `PAS_PLATFORM_TIGER64` |
| `bmalloc/bmalloc/BVMTags.h` | +8/-1 | no VM tag in mmap's fd |
| `WTF/wtf/PlatformHave.h` | +5/-2 | extend the TIGER block, `HAVE(READLINE)` off |
| `JavaScriptCore/assembler/MacroAssemblerX86_64.cpp` | +5/-1 | real AVX CPUID check |
| `JavaScriptCore/bytecode/InlineCacheCompiler.h` | +5 | include `CCallHelpers.h` |
| 17 others | +47/-25 | one- and two-line gates |

Outside the worktree:

| File | Lines | What |
|---|---|---|
| `toolchain/tiger64.cmake` | +95 (new) | x86_64 CMake toolchain file |
| `toolchain/bin/tiger-clang64{,++}` | +12 (new) | wrappers |
| `compat/include/sdk-fill/mach-o/getsect.h` | +28 | LP64 `getsegmentdata()` |
| `compat/cfcompat.c` | +14/-1 | LP64 UUID walk, `#ifndef __LP64__` on the CF/notify halves |
| `compat/Makefile` | +4/-1 | `cfcompat.c` in the x86_64 source list |

## Toolchain

`toolchain/sysroot-x86_64/usr` now holds, all built today for
`x86_64-apple-macosx10.4` with the same patched clang 21, the same 10.4u SDK and
the same cctools ld64 as the i386 side:

- libc++, libc++abi, libunwind (static) — `logs/runtimes-x86_64.log`
- compiler-rt builtins — 213 entry points, `build/builtins-x86_64`
- `libtigercompat.a`, `libtigerdispatch.a` — via `make -C compat{,/dispatch} ARCH=x86_64 install`
- ICU 76.1 static with data — `logs/dep-icu-x86_64.log`

Two things to know about reproducing it.

The compiler wrapper needs `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` for
the runtimes configure, because CMake's ABI probe links a test executable and
`-lc++` does not exist yet.

ICU's own data step did not run for the x86_64 configure (`DATASUBDIR` came out
empty where it is populated for i386, cause not chased), so `libicudata.a` was
installed as the 680-byte stub. The generated `icudt76l_dat.S` is architecture
neutral — it is a `.long` blob behind one symbol — so the i386 build's copy was
reassembled for x86_64 and archived, giving the same 31 MB `libicudata.a`. Watch
for this: linking the stub leaves ICU up but collation dead, and `ucol_open()`
returns null with `U_FILE_ACCESS_ERROR` rather than failing loudly. Also note
that `ar crs` onto an existing archive appends, so the stub member survived the
first attempt to replace it and kept winning.

## Verification

`spike/`-style differential check: the smoke script runs on the Tiger box under
`jsc64` and on this Mac under node, and the two outputs are identical line for
line — JSON round trip over 5000 objects, RegExp including named groups,
closures, `toISOString`/`toUTCString`, `Intl.DateTimeFormat`/`NumberFormat`
(de-DE)/`Collator` (de), NFC/NFD normalization, Turkish locale casing, classes,
arrow functions, template literals, `Map`, spread, `Symbol.iterator`, and a GC
stress round of 120,000 short-lived objects plus a 20,000-entry `Map`.

A caveat worth repeating from NOTES: the first version of that script had a
wrong expected constant for the closures case, and the Tiger run "failing" it
was the only reason I checked. A single-platform expectation is a guess; the
host comparison is the evidence.

Separately, `spike/`-style VM probe (`vmprobe.c`, run on the box) established
before any of the WebKit work that a 64-bit process on 10.4.11 can mmap up to
4 GB anonymously, can `mmap` RWX and execute from it, and can `mach_vm_map`
with an alignment mask. The RWX result is what makes the JIT possible at all:
Tiger has no `MAP_JIT`, no W^X enforcement and no code signing, so JSC's
executable allocator gets plain RWX pages and needs no dance.

## What a 64-bit content process would still need

The spike deliberately built only `jsc`. Extending this to a real content
process, from this angle:

**Memory model.** Nothing blocks it. 64-bit pointers, 64-bit `JSValue` (which is
what upstream kept when it deleted the 32-bit representation), the structure heap
and the gigacage all reserve and commit fine. The one real limit is that the
kernel is 32-bit (`xnu-792`, `RELEASE_I386`); per-process address space is large
but the machine has 6 GB and the kernel's own map is not. I reserved 4 GB in one
call without complaint but did not touch it.

**JIT memory.** Plain RWX `mmap` works and executes. No `MAP_JIT`, no
`pthread_jit_write_protect_np`, no JIT cage, no dual mapping. `ENABLE(JIT_CAGE)`
and `ENABLE(FAST_JIT_PERMISSIONS)` stay off. This is the easiest part of the
whole port and it is worth saying plainly: the reason the i386 port has no JIT is
that upstream deleted the 32-bit value representation, not that Tiger cannot host
a JIT.

**Thread APIs.** `pthread_setname_np` and `pthread_threadid_np` are compat stubs
already. Mach thread suspension and `thread_get_state` — which the GC needs to
scan other threads' registers conservatively, and the sampling profiler needs
constantly — were not exercised here, because `jsc -e` finishes before the GC
ever suspends a mutator that is doing anything. That is the largest untested
area and the first thing I would probe next: `thread_get_state` with
`x86_THREAD_STATE64` on a 64-bit task under a 32-bit kernel is exactly the shape
of thing 10.4 gets wrong, and `pthread_get_stackaddr_np` already proved the
point for the main thread. Secondary threads did report their stacks correctly,
which is mildly encouraging.

**Signals rather than Mach exceptions.** With `HAVE(MACH_EXCEPTIONS)` off, WTF's
`Signals.h` path is what handles the JIT's traps. `MachineContext.h` now reads
the right registers out of a 10.4 `ucontext`, but no signal was actually
delivered to JIT code during this spike. VM traps, the sampling profiler and
wasm fast-memory all depend on it, so this needs a targeted test before anything
relies on it.

**Everything above JSC is the real cost.** A 64-bit content process cannot use
CoreFoundation, Foundation, AppKit, CoreGraphics, CoreText, ImageIO, QuickTime or
the Apple TV QuartzCore, because none of them has an x86_64 slice on this system
and no amount of shimming creates one. WebCore as this project is building it is
a Cocoa-port WebCore. A 64-bit content process would have to be a non-Cocoa port
(rendering through our own stack) talking to a 32-bit UI process over IPC, which
is a different project from the current one, not an increment of it. The JIT
result above is a strong argument for putting JavaScript in a 64-bit process; it
is not, by itself, an argument that the rest of the engine can follow it there.

## Two things for other tracks

`make -C compat/dispatch ARCH=x86_64 install` installs the **i386** archive: the
`install` target copies `libtigerdispatch.a` rather than `$(LIB)`, which is
`libtigerdispatch-x86_64.a` for that ARCH. I copied the right file by hand.
Reported to the dispatch track.

`build/` is shared scratch and gets swept. `build/jsc64` was deleted out from
under this spike mid-session by another track's cleanup, which cost a full
rebuild. Worth either namespacing build directories or agreeing not to `rm -rf`
siblings.

---

# 2026-09-20 (later), agent `jsc64`: the three untested pieces, tested

Picking up the three things the spike above left open — conservative GC across
threads, POSIX signal delivery into JIT code, and FTL — plus RSS and JIT memory
numbers. Worktree `WebKit-jsc64`, branch `tiger-jsc64`, now at `46273728`;
binary from `build-jsc64-core2` (Release, `-march=core2`, FTL on).

Two real bugs fell out, one of them fatal to anything that ever interrupts
optimised JavaScript.

## Verdict, short

| Question | Answer |
|---|---|
| `thread_get_state(x86_THREAD_STATE64)` on 10.4's 32-bit kernel | **Truthful**, including a JIT rip |
| Conservative GC with several JS threads under the JIT | **Works**, 21,527 collections, no corruption |
| POSIX signals delivered into JIT memory | **Works**, but `hlt` arrives as SIGILL — bug, fixed |
| Wasm traps via the fault handler | **Works**, signaling memory, 20,000 traps clean |
| FTL | Was already enabled and tiering up; **3.5x on the 2M loop** |
| `jsc --footprint` | Returned 0 on every run — bug, fixed |

## 1. Conservative GC across threads

`thread_get_state` is the one call the whole conservative collector rests on, so
it gets a lie detector, not a smoke test: **`spike/jsc64/tgstate64.c`**. A victim
thread parks with five sentinels in the callee-saved registers, the main thread
suspends it and reads its state:

```
tgstate64: pointer size 8, x86_THREAD_STATE64_COUNT 42, sizeof(x86_thread_state64_t) 168
PASS  count returned 42 (want 42)
PASS  r12 = 0x1234deadbeef0012   ... r13, r14, r15, rbx all verbatim
PASS  rsp 0x100485f00 is a 64-bit address (not a 32-bit-looking one)
PASS  rsp 0x100485f00 inside the victim stack [0x100406000,0x100486000)
PASS  rip 0x10000162e is inside park() at 0x100001740 (delta 274)
PASS  victim local 0x100485f04 is within [rsp, stack top)
PASS  rip 0x1001e2008 is inside the RWX mapping at 0x1001e2000 (JIT-code rip is reported)
PASS  2000 suspend/get_state/resume rounds, 0 bad
ALL PASS
```

So, unlike `pthread_get_stackaddr_np`, this one does **not** lie to a 64-bit
process. Registers, stack pointer and instruction pointer are all right, and the
rip is right even when the thread is executing from a plain RWX mapping, which is
what JSC's executable allocator hands the JIT here.

At the JSC level, **`spike/jsc64/gcthreads.js`**: four JS threads (main plus three
`$.agent` workers), each building 4,000 live objects whose only reference is a
local inside a hot, JIT-compiled function, calling `gc()` 160 times each, with
`--collectContinuously=true` so the collector is also firing on its own. Each
round re-derives the expected sum and compares — a missed root shows up as wrong
data, not just as a crash.

```
worker seed=1000 gcs=160 ... worker seed=2000 ... worker seed=3000 ...
threads=4 explicit_gcs=640
PASS gcthreads
```

`--logGC=1` on the same run: **21,527 collections**, 19.7 s wall. Every VM's
collector runs on its own thread, so each of those collections suspended a
*different* thread and read its registers through the call above.

Run again with `--sample` (sampling profiler on top): 2,465 profiler samples —
each one a suspend + `thread_get_state` + JIT-frame stack walk of a running
mutator — concurrent with the 640 explicit GCs. Clean.

The sampling profiler is also the tidiest end-to-end proof that the register read
lands in JIT code and is interpreted correctly:

```
Total samples: 95 -- 92 in 'fib', FTL: 91 (95.8%), Baseline: 1, C/C++: 3
Hottest bytecodes:  23  'fib#CntvZP:FTL:bc#61 <-- fib#CntvZP:FTL:bc#32'
```

Attributing a sample to an inlined FTL bytecode index requires the rip *and* the
frame pointer out of a suspended thread to both be right.

**One upstream thing noticed, not fixed.** `Thread::getRegisters()` returns
`metadata.userCount * sizeof(uintptr_t)` as the byte length of the register
block, but `userCount` is in `natural_t` (4-byte) units: 42 * 8 = 336 bytes for a
168-byte `x86_thread_state64_t`. `MachineThreads::tryCopyOtherThreadStack` then
copies 336 bytes out of a 168-byte stack local, so the conservative scan includes
168 bytes of the caller's frame. It is an over-approximation, which is safe for a
conservative collector, and it is not Tiger-specific (ARM64 has the same 2x), so
it is left alone. Worth knowing before anyone reads it as a real root.

## 2. Signal delivery into JIT code — one fatal bug

`HAVE(MACH_EXCEPTIONS)` is off (`mach_exc.defs` is 10.5+), so every trap arrives
as a POSIX signal. **`spike/jsc64/sigjit64.c`** executes each trap instruction out
of an RWX mapping and reports which signal it produces, where rip lands, and
whether the handler can steer execution:

```
sigjit64: RWX arena at 0x1001e2000, guard page at 0x1001e3000
PASS  hlt (VMTraps halt)     -> SIGILL  (si_code 3), rip == stub+0 in RWX memory
PASS  ud2                    -> SIGILL  (si_code 1), rip == stub+0
PASS  int3 (breakpoint)      -> SIGTRAP (si_code 1), rip == stub+1   (trap, not fault)
PASS  load from PROT_NONE    -> SIGBUS  (si_code 2), rip == stub+0
PASS  store to PROT_NONE     -> SIGBUS  (si_code 2), rip == stub+0
PASS  load from null         -> SIGSEGV (si_code 1), rip == stub+0
PASS  rip rewritten in the ucontext took effect on return (recoverStub ran)
PASS  rax written in the ucontext took effect (saw 0xc0ffee0badf00d)
ALL PASS
```

The last two lines matter as much as the first six: every JSC handler recovers by
rewriting rip in the `ucontext` to point at a thunk, and on 10.4 that write does
take effect for a 64-bit process. `MachineContext.h`'s unprefixed-register branch
is reading and writing the right fields.

**The bug.** Line one: `hlt` is what `CodeBlock::installVMTrapBreakpoints()`
patches over the DFG's invalidation points, and `hlt` from user mode is a **#GP**
fault. Which signal a #GP becomes is the kernel's choice. `VMTraps::SignalSender`
registers its handler only for `Signal::AccessFault` (SIGSEGV/SIGBUS), but
xnu-792 reports a #GP in a 64-bit task as `EXC_BAD_INSTRUCTION` with code `0xd`
(`EXC_I386_GPFLT`) — so it is delivered as **SIGILL**, misses the handler, hits
the default action and kills the process.

Symptom before the fix, `jsc --watchdog=1500` on a JIT-compiled infinite loop:

| tier | before | after |
|---|---|---|
| `--useJIT=false` (LLInt) | terminated cleanly, exit 3 | same |
| `--useDFGJIT=false` (baseline) | terminated cleanly, exit 3 | same |
| `--useFTLJIT=false` (DFG) | **killed, signal 4, exit 132** | terminated cleanly, exit 3 |
| full JIT | **killed, signal 4, exit 132** | terminated cleanly, exit 3 |

LLInt and baseline survived because they have no invalidation points to patch;
only optimised code gets `hlt` written into it. The crash log was
`EXC_BAD_INSTRUCTION (0x0002) / Code[0]: 0x0000000d` with rip in the JIT arena
and `Unable to generate backtrace for 64 bit task` — which is why this needed the
probe rather than a debugger.

Fixed in **`7d65712c`**: register the same handler for
`Signal::IllegalInstruction` as well, scoped to
`CPU(X86_64) && !HAVE(MACH_EXCEPTIONS)`. It is safe because the handler returns
`SignalAction::NotHandled` for any pc that is not a JIT pc with an installed trap
breakpoint, and WTF then chains to the previous handler or restores the default.

This was load-bearing for far more than `--watchdog`: VMTraps is how the watchdog,
`Heap`'s stop-the-world handshake for an unresponsive mutator, termination
requests, and any asynchronous interruption of optimised code all reach running
JavaScript. Every one of them would have killed the process.

**Wasm traps.** **`spike/jsc64/wasmtrap.js`** hand-assembles a module (sections
built programmatically — hand-counted LEB sizes were wrong three times) exporting
loads, stores, `unreachable` and `i32.div_s`:

```
memory bytes: 65536
PASS  in-bounds load / in-bounds store
PASS  load past the end:       RuntimeError: Out of bounds memory access
PASS  store past the end:      RuntimeError: Out of bounds memory access
PASS  load just past the end:  RuntimeError: Out of bounds memory access
PASS  unreachable:             RuntimeError: Unreachable code should not be executed
PASS  i32.div_s by zero:       RuntimeError: Division by zero
PASS  i32.div_s overflow:      RuntimeError: Integer overflow
trap storm: 20000/20000 traps in 946 ms
PASS  usable after the storm
PASS wasmtrap
```

That the out-of-bounds cases really go through the signal handler rather than an
emitted bounds check: `--crashIfWasmCantFastMemory=true` does **not** crash, so
the memory is `MemoryMode::Signaling` (the 4 GB fast reservation succeeds on
10.4), and in Signaling mode `WasmOMGIRGenerator`/`WasmBBQJIT64` emit no bounds
check — the access itself faults. Per-trap cost of the full SIGBUS round trip on
this machine: **47 µs**. `--useWasmFastMemory=false` and
`--useWasmFaultSignalHandler=false` are the controls; both give identical results
through the software path.

## 3. FTL

FTL was **already enabled** in `build-jsc64-core2` (`ENABLE_FTL_JIT:BOOL=ON`) and
already tiering up — the sampling profiler above puts 95.8% of `fib` samples in
FTL code. It was *not* on for the 51 ms number in the section above: that came
from `build/jsc64`, whose cache has `ENABLE_FTL_JIT:BOOL=OFF`. So `build-jsc64-ftl`
is a duplicate of `build-jsc64-core2` minus `-march=core2`; it can go.

`spike/jsc64/bench.js`, best of three after a warm-up, on the box. Every case
self-checks its result. The 2M loop is the identical loop from
`spike/TigerBrowser/testpages/script.html`, so it is directly comparable with the
51 ms recorded above.

| | full JIT (FTL) | no FTL (DFG) | baseline only | LLInt |
|---|---|---|---|---|
| 2M loop (`x += i % 7`) | **13 ms** | 46 ms | 46 ms | 136 ms |
| fib(30) | **32 ms** | 47 ms | 103 ms | 362 ms |
| strings (200k concat + join) | 56 | 60 | 155 | 181 |
| objects (300k allocs + walk) | 105 | 108 | 359 | 647 |
| sort 200k numbers | 204 | 247 | 284 | 403 |
| regexp (20k matches) | 25 | 24 | 34 | 81 |
| float math (1.5M sqrt/sin) | 243 | 290 | 423 | 631 |
| bitops (3M) | 40 | 63 | 489 | 914 |
| **total** | **718 ms** | 885 ms | 1893 ms | 3355 ms |
| RSS (current = peak) | 92 MB | 101 MB | 127 MB | 70 MB |
| peak JIT bytes allocated | 53,088 | 46,336 | 32,736 | — |
| total compile time | 207 ms (FTL 185, DFG 19, baseline 2.6) | 24 ms | — | — |

Against the recorded baselines for the same 2M loop on the same machine:

| | 2M loop |
|---|---|
| jsc64, FTL | **13 ms** |
| jsc64, DFG (the 51 ms from the first spike; 46 here with `-march=core2`) | 46–51 ms |
| Safari 4.1.3, i386, 2010 JIT | 59 ms |
| our shipping i386 jsc, C loop | 2240 ms |
| Tiger's own 2007 WebKit | 5300 ms |

FTL is worth **3.5x on the tightest loop** and 1.23x across the set, for 185 ms of
extra compile time and, interestingly, *less* RSS than the DFG-only run (92 vs
101 MB) — FTL replaces DFG code and lets the profiling data go.

JIT memory is tiny in absolute terms: the executable pool reserves **1 GB of
address space** (`fixedExecutableMemoryPoolSize`) but the high-water mark of
actually-allocated executable memory across the whole benchmark set is **53 KB**,
committed 4 KB at a time. A 1 GB reservation is fine here — `vmprobe` had already
shown a 64-bit process on 10.4 reserving 4 GB in one call — but it is worth
remembering that the kernel is 32-bit and this is one reservation per process.

## 4. `jsc --footprint` reported 0 — second bug

`ProcessMemoryFootprint::now()` selects its implementation with
`__has_include(<libproc.h>)` and is written entirely against `proc_pid_rusage()`.
libproc is 10.5+, so on this target the probe finds nothing, falls through to the
final `#else`, and returns `{ 0, 0 }` silently. Every RSS number printed by the
shell was zero, which is the quiet kind of wrong.

`WTF::memoryFootprint()` already had a Tiger path (`TASK_BASIC_INFO`'s
`resident_size`), so the fix reuses it. Checked rather than assumed, given the
`pthread_get_stackaddr_np` precedent: **`spike/jsc64/rss64.c`** touches 200 MB and
watches the counter:

```
before: TASK_BASIC_INFO(64) resident 741376 (0 MB), virtual 48 MB
after:  TASK_BASIC_INFO(64) resident 210509824 (200 MB), virtual 248 MB
PASS  resident size grew by 200 MB after touching 200 MB
PASS  virtual size grew by 200 MB
      getrusage ru_maxrss went 0 -> 0 (NOT populated on 10.4)
```

`TASK_BASIC_INFO` (which is `TASK_BASIC_INFO_64` under LP64) is truthful on the
32-bit kernel. Recorded alongside it: **`getrusage()`'s `ru_maxrss` exists in the
struct but is never populated on 10.4**, so there is no lifetime-peak counter to
report at all; `peak` is now the high-water mark over the samples the process has
taken. Fixed in **`46273728`**.

## Files

| Path | What |
|---|---|
| `spike/jsc64/tgstate64.c` | `thread_get_state(x86_THREAD_STATE64)` lie detector |
| `spike/jsc64/sigjit64.c` | which signal each trap instruction produces from RWX memory; ucontext write-back |
| `spike/jsc64/rss64.c` | whether 10.4 will tell a 64-bit process its own RSS |
| `spike/jsc64/gcthreads.js` | four JS threads allocating under the JIT across hundreds of GCs |
| `spike/jsc64/wasmtrap.js` | wasm traps through the fault signal handler, plus a 20k trap storm |
| `spike/jsc64/bench.js` | the timing set (self-checking) |

Run on the box from `~/jsc64`: `./tgstate64`, `./sigjit64`, `./rss64`,
`./jsc wasmtrap.js`, `./jsc --collectContinuously=true gcthreads.js`,
`./jsc --footprint bench.js`, `./jsc --watchdog=1500 /tmp/spin.js`.

## What is still untested here

- `Thread::suspend`/`getRegisters` under *memory pressure*: the GC stress ran with
  a healthy heap. Nothing suggests a problem, it simply was not tried.
- The main thread's `StackBounds` fix from the first spike is still only exercised
  shallowly — a bad day there looks like a stack-overflow check that never fires.
  A deep-recursion test that expects a `RangeError` at a sane depth would close it.
- `int3`/`SIGTRAP` is proven at the OS level but JSC only emits it for debugging
  options, so no JSC path was driven through it.
- `build/jsc64` was moved to `build-jsc64-spike-orig` (out of the shared `build/`
  sweep path) rather than deleted; it is the FTL-off tree that produced the
  original 51 ms and can be removed whenever.
