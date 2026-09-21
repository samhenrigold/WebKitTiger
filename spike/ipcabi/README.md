# Proving the cross-ABI IPC wire fix on the box

`make run` builds one source four ways, {i386, x86_64} x {patched, unpatched}, and runs a 32-bit and
a 64-bit process against a shared buffer, encoding in one and decoding in the other, both directions.

## What is real and what is mirrored

**Real:** `wtf/ArgumentCoder.h` is included from the `tiger-ipc-abi` worktree and compiled for both
targets, so `IPC::wireAlignmentOf` in the patched build is the patched definition itself.

**Mirrored:** `IPC::Encoder` and `IPC::Decoder` are not compiled here. `Encoder.h` includes the
generated `MessageNames.h`, and no build in this tree has ever configured `Source/WebKit`, so that
header does not exist and there are no IPC translation units to build. The two functions that
determine the wire format are transcribed verbatim into `abiwire.h` from `Encoder.cpp` and
`Decoder.h` on the branch. The real decoder rounds the buffer pointer where this rounds the offset,
which is equivalent because `Decoder`'s constructor rejects a buffer whose base is misaligned.

The only difference between the patched and unpatched builds is the alignment expression:
`IPC::wireAlignmentOf<T>` against `alignof(T)`, which is what the header said before the fix.

## Result

The message mixes widths on purpose: `uint8`, `uint64`, `double`, `int32`, a length-prefixed string,
an optional, a `Vector` of 8-byte values, and a nested struct, for 22 encoded fields.

| build | direction | fields | mismatched | encoded | decoded | values |
|---|---|---|---|---|---|---|
| unpatched | i386 to x86_64 | 22 | **21** | 96 | 136 | CORRUPT |
| unpatched | x86_64 to i386 | 22 | **21** | 112 | 124 | CORRUPT |
| patched | i386 to x86_64 | 22 | 0 | 112 | 112 | OK |
| patched | x86_64 to i386 | 22 | 0 | 112 | 112 | OK |

Unpatched, the divergence starts at the first 64-bit field and never recovers:

| field | i386 encodes at | x86_64 decodes at |
|---|---|---|
| `b:u64` | 4 | 8 |
| `c:double` | 12 | 16 |
| `d:i32` | 20 | 24 |
| `s.len:u64` | 24 | 32 |
| `opt.val:u64` | 44 | 50 |
| `vec.count:u64` | 52 | 51 |
| `nested.y:u64` | 88 | 56 |

The string's `uint8` characters keep their relative spacing but sit 8 bytes late, and from
`opt.val` onward the decoder loses the thread completely: it is reading 1-byte-aligned rubbish, so
its positions crawl forward one byte at a time while the encoder's advance by eight. A 96-byte
message is read as 136 bytes.

Patched, both sides produce 112 bytes and agree on every offset, and the result is symmetric: it
does not matter which architecture is the parent.

## Fifth offender: span element size

`ArgumentCoder<std::span<T>>` hands the range to `Encoder::encodeSpan`, which bulk-copies
`size * sizeof(T)`. `wireAlignmentOf` fixes where a field starts, not how wide it is, so a composite
element whose layout differs between the ABIs corrupts silently. `spancheck.cpp` uses the real
`isWireStableSpanElement` from the branch and measures the elements on the box:

| element | i386 | x86_64 | verdict on i386 | verdict on x86_64 |
|---|---|---|---|---|
| `{u32, i32, float, u8}` | 16 | 16 | stable | stable |
| `{u32, u64}` | **12** | **16** | stable (cannot tell) | **rejected** |
| `{u32, {u32, u64}}` | **16** | **24** | stable (cannot tell) | **rejected** |

A span of a hundred `{u32, u64}` is 1200 bytes from the 32-bit side and 1600 from the 64-bit side.

`make spanreject` compiles the case that must fail. It reports 0 rejections for i386 and 2 for
x86_64, which is the asymmetry stated in the trait's comment: on i386 a struct holding a `uint64_t`
already reports `alignof` 4 and is indistinguishable from a safe one. **Both builds have to run**,
the same requirement `ptrdiff_t` imposes.

The nested case is the one worth remembering: nothing declared in `NestsAMixedField` is 8 bytes
wide, so a review that reads field types alone would pass it.

## Audit of the hand-written coders

The generated coders inherit the fixes; these are the hand-written paths.

### Found and fixed

| file:line | what | class | fix |
|---|---|---|---|
| `StreamConnectionEncoder.h:63` | a **second encoder** padding by `alignof(T)` | alignment | pad by `wireAlignmentOf` |
| `StreamConnectionEncoder.h:60` | its `encodeSpan` bulk-copies `sizeof(T)` | element size | `isWireStableSpanElement` assert |
| `StreamConnectionBuffer.h:101,108` | `enum ClientOffset : size_t` and `ServerOffset : size_t`, inside the `Header` **both processes map** | width | `uint32_t`, plus layout asserts |
| `ArgumentCoders.h:125` | `ArrayReferenceTuple` calls `encoder.encodeSpan` directly, bypassing `ArgumentCoder<std::span<T>>` and its check | element size | assert on every element type |
| `unix/ConnectionUnix.cpp:91` | `AttachmentInfo`, blitted raw onto the socket like `MessageInfo` | raw memcpy | `uint8_t` field, layout asserts |
| `WTF/wtf/ArgumentCoder.h` | `long double` was banned by me on a false premise | my error | ban removed |

The stream encoder is the important one. It is a wholly separate encoder from `IPC::Encoder`, with
the same alignment bug, and its own comment already assumed a `uint64_t` lands on 8 — which was only
true on x86_64. The `size_t` offsets are the sharpest: they are the atomic synchronisation state in
shared memory, so the two processes would disagree on both the header layout and the width of the
word they synchronise through.

`long double` deserves calling out because the error was mine. I banned it claiming 4-byte alignment
on i386 against 16 on x86_64. Measured on the box it is 16 bytes with 16-byte alignment on both, so
it is wire stable and the ban is gone. `spancheck.cpp` now asserts that.

### Checked and clean

| what | why it is fine |
|---|---|
| `ArgumentCoders.cpp:51,107` string coders | `span8`/`span16` are `LChar`/`UChar`, fixed width |
| `VectorArgumentCoder<true, ...>` fast path | routes through `ArgumentCoder<std::span<T>>`, and is gated on `is_arithmetic` |
| `SharedMemoryHandle` | a 32-bit fd or mach right, plus a deliberate `uint64_t m_size` |
| `SharedBufferReference` | `uint64_t` on the wire; the `size_t` members are local |
| `StreamConnectionBuffer::headerSize()` | `alignof(std::max_align_t)` measured 16 on both |
| `StreamConnectionEncoder::messageAlignment` | `alignof(MessageName)`, an `enum : uint16_t`, 2 on both |
| pointer-width IDs | none encoded |

### The sixth class is absent, for now

`CGFloat`, `NSInteger` and `NSUInteger` do not appear anywhere in `Platform/IPC`. `CGFloat` would be
a genuine sixth class if it did, being `float` on i386 and `double` on x86_64, which changes both
width and alignment. It will appear if the Cocoa coders are ever brought into this transport, so the
check is worth repeating then rather than assuming this result holds.
