# 32-bit UI process + 64-bit content process on Mac OS X 10.4.11

Prototype for the WebKit2-shaped split: a 32-bit Cocoa UI process and a 64-bit content process.
`parent32` is the UI side, `child64` the content side. `make run` builds both and runs them on the box.

Numbers are in RESULTS.md; this file is the API and gotcha record.

## Does any of this work at all

Yes. Tiger 10.4.11 on a Core 2 Duo executes x86_64 user binaries: `hw.optional.x86_64` is 1, and
`/usr/lib/libSystem.dylib` on the box carries ppc, ppc64, i386 and x86_64 slices. The kernel is
32-bit (`uname -m` is i386) but it execs 64-bit user processes. This was the premise the whole
design rested on, so it was the first thing measured rather than assumed.

## What a 64-bit process can link against on Tiger, and why it shapes the split

This is the most consequential finding here, and it is not about IPC at all. Only three system
libraries on the box carry an x86_64 slice:

| library | slices |
|---|---|
| `libSystem.dylib` | ppc ppc64 i386 **x86_64** |
| `libstdc++.6.dylib` | ppc i386 ppc64 **x86_64** |
| `libz.1.dylib` | ppc ppc64 i386 **x86_64** |
| `libobjc.A.dylib` | i386 ppc |
| CoreFoundation, Foundation, AppKit, ApplicationServices, CoreServices | i386 ppc |
| OpenGL, QuartzCore | i386 ppc |
| libxml2, libcurl, libssl, libiconv | i386 ppc |

The x86_64 libSystem is a real one, not a stub: 3237 exported symbols against the i386 slice's 3413,
including pthreads, dlopen, kqueue, the `mach_vm_*` family, BSD sockets and `getaddrinfo`.

So the 64-bit content process gets libc, libstdc++, zlib and Mach, and nothing else. There is no
Objective-C runtime, no CoreFoundation, no CoreGraphics or CoreText, and no OpenGL on that side.
Everything else it needs must be built from source for x86_64. That has three consequences:

- All Cocoa, all Objective-C and all GL stay in the 32-bit UI process. The split is not a preference,
  it is forced by the slices that exist.
- "Load Leopard's x86_64 CoreGraphics and CoreText privately in the content process" needs more than
  those two binaries: they would drag in an x86_64 CoreFoundation and its dependencies, none of which
  Tiger has. Weigh that against cairo/freetype/harfbuzz before committing to it.
- curl, OpenSSL, libxml2 and iconv all have to be rebuilt for x86_64 regardless.

## 1. Launch and port handoff

- **No `posix_spawn` on Tiger.** There is no `spawn.h` in the 10.4 SDK and no `_posix_spawn` in
  libSystem. Use `fork` + `execl`, which is what `parent32` does.
- **`bootstrap_register` works and is not restricted.** On 10.5+ it was deprecated and later removed
  in favour of `bootstrap_check_in` with a launchd job, but on Tiger a plain process may register a
  name. The parent registers `org.webkittiger.ipcspike.<pid>`, passes the name to the child in argv,
  and the child calls `bootstrap_look_up`. The child inherits the bootstrap port across fork/exec, so
  both sides resolve in the same namespace with no extra plumbing.
- The child then sends the parent a **send right to its own port** and **its own task port**, both as
  port descriptors. Sending `mach_task_self()` this way avoids `task_for_pid` and the privilege
  question entirely.

## 2. Mach IPC across the ABI split

Layout compatibility, measured rather than assumed, by printing `sizeof` on both sides:

| type | i386 | x86_64 |
|---|---|---|
| `mach_msg_header_t` | 24 | 24 |
| simple message struct | 40 | 40 |
| message with a port descriptor | 48 | 48 |
| `mach_msg_ool_descriptor_t` | 12 | **16** |

- `mach_msg_port_descriptor_t` is 12 bytes in both ABIs, because `mach_port_t` is 4 bytes in both.
  Port passing is layout-compatible in both directions.
- `mach_msg_ool_descriptor_t` holds a pointer and so differs. **The kernel translates it anyway**: a
  256 KB out-of-line send from the 32-bit parent arrived in the 64-bit child with the correct size
  and intact contents at a valid 64-bit address. Out-of-line data is usable across the split.
- **Keep every field in a message a fixed-width type, and check offsets, not just sizeof.** The
  classic i386 ABI 4-byte-aligns an 8-byte field while x86_64 8-byte-aligns it, so one `uint64_t`
  after an odd number of 4-byte fields shifts every later offset on one side only. Measured on the
  box: `{u32,u32,u32,u64}` is 20 bytes with the u64 at offset 12 on i386, and 24 bytes with it at 16
  on x86_64, while `{u32,u32,u64}` is 16 bytes with it at 8 on both. `common.h` carries
  `_Static_assert`s on the size and every field offset of each message struct; both compilers build
  that header, so a future field that breaks the layout fails the build instead of corrupting
  messages. Splitting a 64-bit value into two `uint32_t` also works and is what to reach for when a
  struct cannot be reordered.
- **Dispatch on `msgh_id`, never on a field in the body.** `msgh_id` is at a fixed offset in every
  message shape; a discriminator placed after a descriptor is not, because the descriptor changes
  size across the split. Getting this wrong deadlocked the first version of this spike: the child
  read the descriptor count where it expected an opcode, fell through, and never replied.

## 3. Shared memory

The 64-bit side owns the buffer, which is the right way round for a content process that renders:

1. child: `mach_vm_allocate` the double buffer, then `mach_make_memory_entry_64` over it;
2. child: send the memory entry port to the parent as a port descriptor;
3. parent: `vm_map` it into the 32-bit address space with `VM_FLAGS_ANYWHERE`.

This works unchanged across the split. The 32-bit parent maps a region allocated by a 64-bit task
and reads it directly.

**POSIX `shm_open` + `ftruncate` + `mmap` also works across the split, at the same speed**, for the
full 10.37 MB double buffer. The spike sets up both and measures them back to back; see RESULTS.md.
OpenGL uploads from either mapping at an identical rate. Choose between them on lifetime, not
performance: a memory entry is a port and dies with the process, while POSIX shm needs a global name
and an explicit `shm_unlink` that a crashed process will not perform.

Note the address spaces: the child's buffer sits above 4 GB (`0x102008000` in a sample run) and the
parent's mapping of the same pages is at `0x2008000`. Nothing may pass a raw address across the
boundary; only offsets. The protocol sends a buffer index.

## 4. OpenGL upload on the 32-bit side

A **CGL context over ssh works and is hardware accelerated** (`NVIDIA GeForce 8600M GT OpenGL
Engine`), via `kCGLPFAAccelerated` + `kCGLPFAPBuffer` and `CGLCreatePBuffer`. No window session is
needed, which was not the expected outcome. `GL_APPLE_client_storage` and `GL_APPLE_texture_range`
are both present.

The texture can be fed straight from the mapped shared pointer with no intermediate copy.

**Measure a GL upload by drawing with the texture, not by `glFinish` after specifying it.**
`GL_APPLE_client_storage` does not copy at specification time, so a benchmark that only uploads and
finishes reports an upload rate of about 16.8 GB/s, which is nonsense on this hardware. The numbers
in RESULTS.md upload, draw a full-screen textured quad, and `glFinish`, with a draw-only baseline
subtracted, so the upload figure is the marginal cost of the pixels.

## 5. Crash isolation

`task_set_exception_ports` on the child's task port works, and the 32-bit parent receives
`msgh_id` 2401 (`exception_raise`) when the 64-bit child faults on a null write. The parent needs no
special privilege because the child handed over its task port voluntarily at startup. That is enough
to detect content-process crashes and tear down or restart, which is what the UI side needs.

## Tiger gotchas collected here

- No `posix_spawn`; fork/exec.
- Compile the 64-bit side with `-fno-asynchronous-unwind-tables -fno-unwind-tables`. cctools ld64 can
  assert (`targetAtom != NULL`, ld.hpp:914) linking x86_64 at 10.4 when objects carry EH personality
  references. This spike has not hit it, being plain C, but the flags are in the Makefile as
  insurance.
- `bootstrap_register` is allowed, unlike on later systems.
- The 32-bit side uses `vm_map`; `mach_vm_*` is what the 64-bit side uses to allocate. Both are
  present on Tiger.
- The OOL descriptor is the only message element whose layout differs; the kernel translates it.
- `grep` treats a crashed process's log as binary; use `grep -a` when collecting results.
- Benchmarking over ssh measures the harness as much as the box. Runs overlapping other ssh sessions
  came out two to three times worse across every metric, monotonically with the amount of
  concurrency. Run one measurement at a time, with nothing else touching the machine.
