# A 64-bit content process on Tiger: spike results

Verdict: **the premise holds and the JIT half is proven. The graphics half is not.**

64-bit processes really do run on this 10.4.11 box, Leopard 10.5.0's x86_64 CoreFoundation,
CoreGraphics and CoreText all load into one, CoreFoundation works, and every prerequisite a
JIT needs passes. CoreGraphics then faults inside `CGBitmapContextCreate`, and that is where
the spike stops.

Everything below ran on the box. Nothing is inferred from the binaries alone.

## 1. 64-bit processes work on Tiger 10.4.11

This was the part most likely to end the spike immediately, because 10.4 was never
advertised as 64-bit on Intel. It is nonetheless true.

The kernel is `xnu-792.25.20 RELEASE_I386`, 32-bit, on a Core 2 Duo (`MacBookPro3,1`).
`sysctl hw.cpu64bit_capable` does not exist, that sysctl being a Leopard addition. But
Tiger ships **x86_64 slices of both `libSystem.B.dylib` and `dyld`**, alongside ppc, ppc64
and i386, and the 32-bit kernel executes 64-bit user processes:

```
$ ./hello64
sizeof(void*) = 8
kernel        = Darwin 8.11.1
machine       = i386
```

Built with the stock toolchain retargeted by hand, since the wrapper is i386-only:

```bash
toolchain/llvm-tiger/bin/clang -target x86_64-apple-macosx10.4 \
  -isysroot sdk/MacOSX10.4u.sdk -mmacosx-version-min=10.4 -fno-stack-protector \
  --ld-path=toolchain/cctools/bin/i386-apple-darwin8-ld -o hello64 hello64.c
```

The cctools linker builds x86_64 despite its `i386-apple-darwin8-` prefix, and the 10.4u SDK
carries x86_64 `crt1.o`, `dylib1.o` and `libSystem.B.dylib`.

Tiger's x86_64 libSystem is a real library, not a stub: **3230 exports** against i386's
3413. What Tiger has in x86_64 beyond it is thin — `libgcc_s`, `libstdc++.6.0.4`, `libz`,
`libncurses`, `libmx`, `libmathCommon`, and Accelerate/vecLib. **No CoreFoundation, no
CoreGraphics, no Cocoa, no ObjC runtime.**

That absence is the whole reason this idea is different from the 32-bit one. In a 32-bit
GUI process Tiger's own CoreFoundation is already loaded and a second one cannot coexist.
In a 64-bit process there is nothing to collide with, so the entire Leopard userland can be
brought in privately.

## 2. What Leopard's x86_64 frameworks need from Tiger

The existing `refs/leopard-9a581/` tree was thinned to i386 in place, so the x86_64 slices
were gone. Re-extracted from `OS installers/leopard_9a581_userdvd.dmg`, `BaseSystem.pkg`,
into `refs/leopard-9a581-x86_64/root/` keeping every slice. Leopard 10.5.0 has x86_64 for
all of it.

Resolving each framework's x86_64 undefined symbols against Tiger's x86_64 libSystem plus
the Leopard libraries that would ship alongside:

| Framework | undefined | unresolved |
|---|---|---|
| **CoreText** | 391 | **0** |
| ColorSync | 219 | 1 |
| ATS | 334 | 3 |
| libobjc | 132 | 3 |
| CoreGraphics | 624 | 9 |
| CoreFoundation | 419 | 12 |
| Foundation | 1613 | 19 |

Those are small numbers, and they are the interesting ones: the frameworks WebCore actually
draws through need almost nothing Tiger lacks. CoreText needs *nothing*.

The full closure is larger. Walking dependencies from CoreText and CoreGraphics reaches **25
libraries**, because CoreGraphics pulls in CoreServices, which pulls in CarbonCore,
LaunchServices, Metadata, SearchKit, Security and DiskArbitration. Across all 25 there are
3094 undefined symbols and **112 unresolved**, grouped as:

| group | count | what wants it |
|---|---|---|
| CommonCrypto and DES/AES primitives | 24 | Security |
| file quarantine (`qtn_*`) | 27 | LaunchServices |
| launchd (`launch_*`, `posix_spawn*`, `vproc_*`) | 13 | CoreServices |
| `fenv` | 8 | CoreGraphics' math |
| everything else | 40 | scattered |

Almost none of that is on a drawing path. It is the price of the umbrella dependency, and
cutting CoreServices out of the closure would remove most of it.

### Load commands

Better than expected. No `LC_DYLD_INFO`, no `@rpath`, no `LC_LOAD_UPWARD_DYLIB` anywhere:
these are classic 64-bit images with `LC_SEGMENT_64`, which dyld-46 handles.

The one problem is **`LC_REEXPORT_DYLIB` (0x8000001f)**, which Leopard introduced and
dyld-46 refuses outright. `CoreServices` carries ten, being an umbrella that re-exports its
sub-frameworks. Tiger's dyld says:

```
/tmp/leo64/CoreServices: unknown required load command 0x8000001F
```

`spike/demote-reexports.py` rewrites the command word to `LC_LOAD_DYLIB` (0x0c). The two
are the same `dylib_command` structure, so nothing else changes: the dependency still loads
and only the re-export is dropped. That costs nothing here because the process runs
flat-namespace, where a lookup searches every loaded image anyway.

## 3. What actually loads and runs

Two libraries carry the gap. `spike/leo64shim.c` holds the 37 real implementations,
`spike/leo64stubs.c` the remaining 89 as honest failures that announce themselves when
called. Both are inserted with `DYLD_INSERT_LIBRARIES`, and the process runs with
`DYLD_FORCE_FLAT_NAMESPACE=1` so that two-level bindings to libSystem can fall through to
them.

The real implementations worth naming:

- `OSAtomicCompareAndSwapPtr`, `...PtrBarrier` and `...Long` forward to Tiger's
  `OSAtomicCompareAndSwap64`, which is the same operation at this width.
- `fmax`, `fmin`, `exp2`, `flsl`, `nan` — C99 that Tiger's x86_64 libm does not export.
- `stat64`/`fstat64` are `stat`/`fstat`, since x86_64 stat is already 64-bit;
  `realpath$DARWIN_EXTSN` and `select$DARWIN_EXTSN`/`$1050` are the plain calls.
- `bootstrap_look_up2`/`register2` over Tiger's originals.
- **`dyld_register_image_state_change_handler`**, which is the one that mattered. objc4
  learns about images through it and Tiger's dyld has no such call, which is exactly what
  killed the 32-bit libobjc attempt. Tiger does have `_dyld_register_func_for_add_image`,
  which replays every already-loaded image at registration and then fires per new one. The
  shim drives the Leopard-style handler with a one-element batch from each callback. **This
  works**: CoreFoundation links libobjc, and it loads and runs.

### Results on the box

```
pointer width: 8 bytes

/tmp/leo64/CoreFoundation    loaded
/tmp/leo64/CoreGraphics      loaded
/tmp/leo64/CoreText          loaded

CF  CFStringCreateWithCString  0x100953080  length=14
CG  CGColorSpaceCreateDeviceRGB 0x102605560
```

**All three load.** That is the headline: Leopard 10.5.0's x86_64 CoreFoundation,
CoreGraphics and CoreText all resolve and initialise inside a 64-bit process on Tiger
10.4.11.

**CoreFoundation works.** Tested on its own: `CFStringCreateWithCString` returns a real
string, `CFStringGetLength` returns 23 for a 23-character string, `CFGetTypeID` returns 7
which is CFString, `CFGetRetainCount` returns 1, and `CFRunLoopGetCurrent` returns a run
loop. The ObjC runtime underneath it initialised, which the image hook above made possible.

**CoreGraphics half works.** Colour spaces are real objects:

```
deviceRGB  = 0x102605560  CFTypeID=256 (CGColorSpaceGetTypeID=256)  components=3
deviceGray = 0x1026055c0  components=1
```

**`CGBitmapContextCreate` faults.** Every variant tried: 8 bits per component with
premultiplied-last alpha, with skip-last, grayscale with no alpha, and a null colour space.
The process dies with SIGSEGV inside the call, before it returns.

What was ruled out. It is not the stubs: CoreFoundation passes its own test with both
libraries inserted, and loading CoreGraphics and CoreText alongside does not disturb it. It
is not argument shape: `CGRect` is 32 bytes and therefore passed in memory, which an earlier
version of the test got wrong, and fixing that did not change the fault. Tracing shows
`vproc_swap_integer` called twice immediately before the crash, but making it report success
rather than failure did not help either.

**It is also not the cross-linker.** cctools ld64 has a known x86_64 crash in its classic
stub pass, which raised the possibility that it had quietly mis-linked the shim. So the
shim, the stubs and the probe were all recompiled to objects, copied to the box, and
relinked there with Xcode 2.5's own `ld64-62.1`. Both builds crash at the same call:

```
cross-linked  EXIT=139
box-linked    EXIT=139
```

That rules the toolchain out and points at CoreGraphics itself or at something it expects
from the system that a headless Tiger process does not provide. The cause is still not
isolated.

Linking on the box, for anyone who needs it: ld64-62.1 wants `-dylib_install_name` rather
than `-install_name`, `-dylib_compatibility_version` and `-dylib_current_version` rather
than the modern spellings, and it will not supply the startup objects itself, so
`/usr/lib/dylib1.o` goes on a dylib link and `/usr/lib/crt1.o` on an executable one.

**CoreText was not exercised.** It loads, but the planned calls need a context to draw into.

## 4. What a JIT needs, measured in a 64-bit process

This is the part that decides whether the idea is worth more work, and it passes completely.

```
ok   mmap PROT_READ|WRITE|EXEC
ok   execute code written into an RWX page
ok   mmap PROT_READ|WRITE
ok   mprotect RW -> RX
ok   execute after the W^X flip
ok   mach_vm_allocate 1 MB
ok   mach_vm_protect to RWX
ok   mach_vm_deallocate
ok   pthread_attr_setstacksize 16 MB
ok   pthread_create with a 16 MB stack
reserved 64 GB of address space in 1 GB chunks (64 mappings)
ok   at least 4 GB of address space reservable

11 passed, 0 failed
```

Machine code written at run time executes, both from a page mapped RWX outright and from one
flipped RW to RX afterwards. The `mach_vm` family is present and can make a mapping
executable. Thread stacks can be sized up. And the address space is genuinely 64-bit: 64 GB
reserved in 1 GB chunks without complaint, which is the thing a 32-bit process cannot give
and which JavaScriptCore's allocators like.

## What this means

The idea is sound and the hardest-looking parts are done. 64-bit processes run; the Leopard
stack loads; CoreFoundation works; the ObjC image-notification problem that blocked the
32-bit route has a working answer here; and the JIT prerequisites are all satisfied.

The open question is graphics, and it is a real one. Until `CGBitmapContextCreate` works
there is no rendering, and CoreText cannot be exercised at all.

Before this becomes a plan rather than a spike, three things need doing:

1. **Isolate the `CGBitmapContextCreate` fault.** A 64-bit debugger on the box would settle
   it in minutes; Tiger's gdb was not tried against an x86_64 process. Failing that, the
   next step is narrowing which of the 112 shimmed symbols CoreGraphics touches on that path
   by making every one of them trace.
2. **Replace flat namespace with import repointing.** `DYLD_FORCE_FLAT_NAMESPACE` across an
   entire OS's worth of libraries is a spike technique, not a shipping one: it flattens every
   lookup in the process and invites collisions. The honest version repoints the missing
   imports at the shim per binary, which is what `refs/leopard/tools/repoint-imports.py`
   already does for 32-bit. It needs 64-bit support: `nlist_64` is 16 bytes rather than 12,
   and the header and `LC_SEGMENT_64` offsets differ.
3. **Decide whether CoreServices comes along.** It contributes most of the 112 missing
   symbols and none of the drawing. If CoreGraphics can be made to load without it, the
   surface shrinks to something defensible.

Worth saying plainly: this spike says nothing about whether WebKit itself can be built
x86_64 for this target, only that the platform underneath it is more capable than assumed.

## Files

| Path | What |
|---|---|
| `spike/leo64shim.c` | the 37 real implementations, including the dyld image hook |
| `spike/leo64stubs.c` | the remaining 89, generated, each announcing itself if called |
| `spike/demote-reexports.py` | rewrites `LC_REEXPORT_DYLIB` to `LC_LOAD_DYLIB` in place |
| `spike/leo64test.c` | loads CF, CG and CT and exercises them |
| `spike/jit64probe.c` | the JIT prerequisite checks above |

`refs/leopard-9a581-x86_64/` holds the re-extracted Leopard tree with all slices, and is
outside version control like the rest of `refs/`.

### Reproducing

```bash
# stage the closure, thinned to x86_64, install names rewritten flat
python3 spike/demote-reexports.py <staged binaries>
scp -O <staged> <shim> <stubs> <test> tiger:/tmp/leo64/
ssh tiger 'cd /tmp/leo64 && DYLD_LIBRARY_PATH=/tmp/leo64 DYLD_FORCE_FLAT_NAMESPACE=1 \
  DYLD_INSERT_LIBRARIES=/tmp/leo64/leo64shim.dylib:/tmp/leo64/leo64stubs.dylib ./leo64test'
```

`LEO64_TRACE=1` makes the shim announce the calls it serves.
