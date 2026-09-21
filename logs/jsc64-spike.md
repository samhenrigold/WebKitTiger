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
