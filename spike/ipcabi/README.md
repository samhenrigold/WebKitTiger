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
