# The serializer-asymmetry guard

`logs/split-process-plan.md` §6.3 names serializer asymmetry the top risk of the
split, because it fails as silent wire corruption rather than as a build error.
This specifies the guard.

Read-only research; nothing under `WebKit/` was modified. Measured against the
fork at `88ed4f7`.

---

## 0. Two corrections to the framing, up front

**The plan understated the scope.** §2.0.5 says three CoreGraphics-flag-dependent
wire types need neutral encodings. That is right about the three *types* that
change shape, but the general problem is larger: across the 394
`.serialization.in` and 256 `.messages.in` files, **293 distinct feature-flag
conditionals** appear, and **69 serialization inputs carry at least one flag that
our two builds will disagree about**. `PLATFORM(COCOA)` alone appears **166
times**.

**The proposed check does not work as proposed.** Hashing the generated
serializer sources per side and comparing them would pass while the wire was
corrupt. `Source/WebKit/Scripts/generate-serializers.py` emits the conditionals
**verbatim** into the generated C++:

```python
# generate-serializers.py:629, :651, :753, :759, :776, :784, :838, :866, :878, :891
result.append(f'#if {type.condition}')
```

So both sides run the same script over the same input and get a **byte-identical
`.cpp`**. The divergence happens later, when each side's C++ compiler evaluates
those `#if`s against its own flags. A source hash is blind to exactly the failure
we are guarding against.

It is still worth having as a cheap second check — it catches a stale generated
file or a patch applied to one tree and not the other — but it is not the guard.
§3 specifies one that works.

---

## 1. What must agree, and what may differ

### 1.1 The rule

> **Every `.in` file compiled into both binaries must evaluate every one of its
> conditionals identically in both builds.**

Stated that way it is mechanically checkable and needs no reachability analysis
of which messages actually cross. Which `.in` files go into both binaries is
determined by the CMake source lists, which is **wkcmake's** domain — this
document assumes the intersection and checks it, rather than asserting what it
should be.

The corollary is the answer to "which of our flags may differ": **any flag that
appears in no shared `.in` file may differ freely.** That set should be derived
by the script in §3, not maintained by hand, so it stays correct as WebKit moves.

### 1.2 The must-agree set

293 distinct conditionals appear across the generator inputs. Extracted with:

```bash
find Source/WebKit -name '*.serialization.in' -o -name '*.messages.in' \
  | xargs grep -h -E '^#(if|elif)' \
  | sed -E 's/^#(if|elif)( defined)? *//' \
  | grep -oE '(ENABLE|USE|HAVE|PLATFORM|OS|CPU)\([A-Z0-9_]+\)' \
  | sort -u
```

Most are harmless — `PLATFORM(GTK)`, `USE(SOUP)`, `OS(WINDOWS)` are off on both
sides and stay off. The ones that matter are the ones our architecture makes
*structurally* different, and there are seven:

| Flag | Occurrences | 32-bit UI+render | 64-bit web | Agreeable? |
|---|---|---|---|---|
| **`PLATFORM(COCOA)`** | **166** | on | **off** | no |
| **`PLATFORM(MAC)`** | **59** | on | **off** | no |
| `USE(CF)` | 11 | on | off | no |
| `USE(APPKIT)` | 10 | on | off | no |
| `USE(CG)` | 6 | on | off | no |
| `USE(CORE_TEXT)` | 4 | on | off | no |
| `USE(UNIX_DOMAIN_SOCKETS)` | 8 | on | on | **yes — force on both** |

The last one is easy and should just be set identically. The other six are the
problem, and they cannot be made to agree by setting them, because each one
genuinely describes what its own process has.

### 1.3 The resolution: separate wire shape from implementation

A conditional in a `.in` file answers *"is this field on the wire?"*. The same
macro in a `.cpp` answers *"do I have this framework?"*. Those are different
questions that upstream never had to separate, because upstream never had two
processes of different platform character on one connection.

**Introduce a wire-flag set, forced identical on both sides, and use it only in
the generator inputs.**

```
TIGER_WIRE_COCOA      1   // both sides
TIGER_WIRE_CG         1   // both sides
TIGER_WIRE_CF         1   // both sides
TIGER_WIRE_APPKIT     1   // both sides
TIGER_WIRE_CORE_TEXT  1   // both sides
```

Set to 1 on both, meaning "the wire carries the Cocoa shape". The 64-bit side
then encodes and decodes Cocoa-shaped messages without having Cocoa, which is
exactly what it must do: it is talking to a Cocoa process.

Two consequences to accept deliberately:

1. **The 64-bit side must be able to name the types in those blocks**, even
   though it cannot use them. In practice that means neutral stand-ins for the
   handful that are CF- or CG-typed — which is the §2.0.5 work, now correctly
   scoped: not three types in the abstract, but *whatever types the
   `TIGER_WIRE_*` blocks reference*. The extraction in §3 produces that list as
   a by-product.
2. **This is a real patch to the `.in` files**, mechanical but wide: rewriting
   `#if PLATFORM(COCOA)` to `#if TIGER_WIRE_COCOA` in the 69 affected files, and
   only there. It belongs in `toolchain/patches/` beside the cross-ABI patch, so
   it survives rebasing and is visible as a deliberate divergence.

The alternative — making the 64-bit side define `PLATFORM(COCOA)=1` wholesale —
is worse: it would change the meaning of 166 conditionals in the `.in` files and
thousands elsewhere, in code that genuinely has no Cocoa.

---

## 2. Scope: which files carry the problem

69 `.serialization.in` files carry at least one of the six unagreeable flags.
The concentration is heavy:

| File | Split-sensitive conditionals |
|---|---|
| `Shared/WebCoreArgumentCoders.serialization.in` | 37 |
| `Shared/WebProcessCreationParameters.serialization.in` | 15 |
| `Shared/Cocoa/WebCoreArgumentCodersCocoa.serialization.in` | 14 |
| `Shared/WebPageCreationParameters.serialization.in` | 12 |
| `Shared/WebEvent.serialization.in` | 10 |
| `Shared/RemoteLayerTree/RemoteLayerTree.serialization.in` | 8 |
| `Shared/Cocoa/CoreIPCPassKit.serialization.in` | 7 |
| `Shared/WebCoreFont.serialization.in` | 5 |
| `Shared/EditorState.serialization.in` | 5 |
| `Shared/XR/PlatformXR.serialization.in`, `Shared/Pasteboard.serialization.in`, both `NetworkProcess/*CreationParameters` | 4 each |
| 57 more | 1–3 each |

Two observations that make this tractable:

- **`WebCoreArgumentCoders.serialization.in` is the one that matters.** It is the
  shared type vocabulary every message draws on, and at 37 conditionals it is a
  day of careful work, not a week.
- **Several of the heavy files are not on the shared path at all.**
  `CoreIPCPassKit` is Apple Pay (off), `PlatformXR` is WebXR (off), and the
  `RemoteLayerTree` file is only live if we adopt that drawing area, which §1.5
  of the plan rejects. Turning the corresponding features off removes them from
  both builds and from the check, which is the cheapest possible fix and should
  be done first.

`Shared/WebCoreFont.serialization.in` deserves a specific note: it carries the
`FontPlatformDataAttributes` shape, which §2.0.4 of the plan already identifies
as needing de-CoreFoundation-ing in both directions. That work and this are the
same work.

---

## 3. The build-time guard

### 3.1 What it has to catch

A flag that appears in a shared `.in` file and evaluates differently in the two
builds. It must fail the build, and it must not require running a target binary
— we cross-compile i386 and x86_64 from an arm64 host, and the i386 side cannot
be executed here at all (Rosetta does not run i386).

### 3.2 The mechanism: preprocess, do not execute

The C preprocessor is the only thing that knows the answer, and we can run it
for both targets on the host. So:

1. **Extract** every conditional token from the `.in` files that CMake compiles
   into both binaries.
2. **Generate a probe** that expands each one to a stable line.
3. **Preprocess it twice**, once per side, with that side's real flags.
4. **Diff.** Any difference fails the build.

The probe generator emits, for each extracted flag `F(X)`:

```c
/* generated: WireFlagProbe.h */
WIRE_FLAG_LINE("ENABLE(GPU_PROCESS)", ENABLE(GPU_PROCESS))
WIRE_FLAG_LINE("PLATFORM(COCOA)",     TIGER_WIRE_COCOA)   /* remapped per §1.3 */
...
```

and the probe TU is:

```c
#include "config.h"          /* pulls wtf/Platform.h and cmakeconfig.h */
#define WIRE_FLAG_LINE(name, value) __WIRE__ name = value
#include "WireFlagProbe.h"
```

Then:

```bash
tiger-clang   -E -P $UI_FLAGS  WireFlagProbe.c | grep '^__WIRE__' | sort > ui.flags
tiger-clang64 -E -P $WEB_FLAGS WireFlagProbe.c | grep '^__WIRE__' | sort > web.flags
diff -u ui.flags web.flags || { echo "WIRE FLAG DRIFT"; exit 1; }
```

Preprocessing only, no code generation, no linking, no execution. It runs in
well under a second and is exact.

### 3.3 Wiring it into CMake

A custom target that both process targets depend on, so it runs before either
compiles:

```cmake
# WireFlagCheck.cmake — owned jointly with wkcmake
add_custom_command(
    OUTPUT  ${CMAKE_BINARY_DIR}/wire-flags.stamp
    COMMAND ${PYTHON_EXECUTABLE} ${WKT}/tools/extract-wire-flags.py
            --sources ${SHARED_SERIALIZATION_INPUTS}
            --out     ${CMAKE_BINARY_DIR}/WireFlagProbe.h
    COMMAND ${WKT}/tools/check-wire-flags.sh
            ${CMAKE_BINARY_DIR}/WireFlagProbe.h
            ${CMAKE_BINARY_DIR}/wire-flags.stamp
    DEPENDS ${SHARED_SERIALIZATION_INPUTS}
            ${WKT}/tools/extract-wire-flags.py
    COMMENT "Checking IPC wire flags agree between the 32-bit and 64-bit builds")

add_custom_target(WireFlagCheck DEPENDS ${CMAKE_BINARY_DIR}/wire-flags.stamp)
add_dependencies(WebKit_UIRender WireFlagCheck)
add_dependencies(WebKit_Web      WireFlagCheck)
```

`SHARED_SERIALIZATION_INPUTS` is the intersection of the two source lists, which
wkcmake computes — the check does not guess it.

**An allowlist, used sparingly.** `extract-wire-flags.py` takes
`--may-differ FLAG` for the case where a flag genuinely should differ and the
difference is known not to reach the wire. Every entry needs a comment saying
why. The expectation is that this list stays empty; if it grows past two or
three, the `TIGER_WIRE_*` remapping in §1.3 is being under-applied.

### 3.4 The second, cheaper check

Add the source hash too, since it costs nothing and catches a different class of
problem — a stale generated file, or the cross-ABI patch applied to one tree and
not the other:

```bash
find "$UI_DERIVED"  -name 'GeneratedSerializers*' -o -name '*MessageReceiver.cpp' \
  | sort | xargs shasum -a 256 | shasum -a 256
```

compared against the same over the 64-bit derived sources. Because the generator
emits conditionals verbatim, **these hashes should be identical**, and an
inequality is itself the signal.

### 3.5 A runtime backstop

Belt and braces, and cheap: put the flag fingerprint in the connection handshake.
Each side computes a 64-bit hash of its own `ui.flags`/`web.flags` content at
build time, bakes it into a constant, and sends it as the first message. A
mismatch closes the connection with a clear diagnostic instead of rendering
garbage. About 30 lines, and it protects against the case the build-time check
cannot see: two binaries built at different times from different configurations
and then run together.

---

## 4. How `wireAlignmentOf` interacts

The lead asked whether the generated coders inherit the fixed alignment
automatically. **They do, for every path the generated code actually takes.**

### 4.1 Why it is inherited

`toolchain/patches/webkit-ipc-cross-abi.patch` changes the single choke point:

```cpp
// Source/WebKit/Platform/IPC/Encoder.h, encodeSpan
- constexpr size_t alignment = alignof(T);
+ // Not alignof(T): that is 4 for 8-byte scalars on i386 and 8 on x86_64.
+ constexpr size_t alignment = wireAlignmentOf<T>;
```

with

```cpp
// Source/WTF/wtf/ArgumentCoder.h
template<typename T>
inline constexpr size_t wireAlignmentOf = (alignof(T) < 8 && sizeof(T) >= 8) ? 8 : alignof(T);
```

and the matching seek in `Decoder`. Generated serializers emit member-by-member
`encoder << member`, which reaches `ArgumentCoder<T>::encode` →
`encoder.encodeObject(value)` → `encodeSpan(singleElementSpan(object))`. So every
scalar a generated coder writes goes through the patched function. **No generated
code needs regenerating and no `.in` file needs changing for alignment.**

The `requires`-clause ban on `long`, `unsigned long` and `long double` is
likewise inherited: it sits on `ArgumentCoder<T>` itself, so a future `.in` field
of one of those types fails to compile in the generated file.

### 4.2 The residual hole, and that it is currently closed

`wireAlignmentOf` fixes *alignment*. It does not fix *size*, and for a struct the
two differ independently:

| | i386 | x86_64 |
|---|---|---|
| `alignof(uint64_t)` | 8 | 8 |
| `alignof(struct { uint32_t; uint64_t; })` | **4** | **8** |
| `sizeof(struct { uint32_t; uint64_t; })` | **12** | **16** |

`wireAlignmentOf` maps that struct's alignment to 8 on both, so padding agrees —
but a bulk write of `sizeof(T)` bytes would still disagree, 12 against 16.

That only matters if a **struct** reaches `encodeSpan` as a whole. Checked:

- `Vector<T>` and `FixedVector<T>` dispatch on `std::is_arithmetic<T>::value`
  (`ArgumentCoders.h:518`, `:579`), so only scalars take the bulk path; structs
  go element-wise and decompose.
- `generate-serializers.py` has **no** memcpy, trivially-copyable or bitwise fast
  path. Generated coders are always member-by-member.
- **`ArgumentCoder<std::span<T>>` (`ArgumentCoders.h:60-70`) calls
  `encodeSpan` for any `T`.** That is the hole.

Auditing every span in the generator inputs, only two have a non-scalar element
type, and **both are safe**:

| Type | Layout | Verdict |
|---|---|---|
| `WebCore::FloatSegment` | `{ float begin; float end; }` — 8 bytes, align 4, both sides | safe |
| `WebCore::PathDataLineColorThickness` | `{ PathDataLine line; PackedColor::RGBA color; float thickness; }` — all 4-byte scalars, 24 bytes, align 4, both sides | safe |

So the hole exists in principle and nothing currently falls into it.

### 4.3 Keeping it closed

Add a static assertion where the hole is, so a future non-agreeing span element
fails to compile rather than corrupting the stream:

```cpp
// ArgumentCoders.h, in ArgumentCoder<std::span<T, Extent>>::encode
static_assert(std::is_arithmetic_v<std::remove_cv_t<T>>
    || (alignof(T) <= 4 && sizeof(T) % 4 == 0),
    "A span of a composite type is written in bulk, so its size and alignment must "
    "be identical in the 32-bit and 64-bit processes. Types with 8-byte members "
    "differ; encode element-by-element instead.");
```

That belongs in the same patch as `wireAlignmentOf`, and it is the fifth offender
class rather than a new mechanism.

---

## 5. Order of work

1. **Turn off the features that remove files from the check entirely** — Apple
   Pay, WebXR, service controls, model process. Cheapest possible reduction of
   scope, and it is CMake-only.
2. **Force `USE(UNIX_DOMAIN_SOCKETS)` on for both.** One line; it is the one
   unagreeable flag that is trivially agreeable.
3. **Land the `std::span` static assertion** into the cross-ABI patch.
4. **Build the `WireFlagCheck`** (§3) against the current flags. It will fail
   immediately and loudly, with a list — that list is the actual work queue, and
   it is better to see it now than to discover it one wrong pixel at a time.
5. **Introduce `TIGER_WIRE_*`** and remap the conditionals in the shared `.in`
   files, largest file first, until the check passes.
6. **Add the source hash and the handshake fingerprint** as the cheap backstops.

Step 4 before step 5 deliberately: the check should exist before the fix, so the
fix has a definition of done.

---

## 6. Coordination

The flag lists and the shared-input intersection belong to **wkcmake**, who owns
the per-process CMake configuration. What this document asks of that track:

- the intersection of the two source lists, as
  `SHARED_SERIALIZATION_INPUTS`, so the check has an exact scope rather than
  scanning all 650 generator inputs;
- the per-process flag sets as CMake variables the probe can be preprocessed
  against;
- a decision on whether `TIGER_WIRE_*` lives in `cmakeconfig.h` per process or in
  a shared header included by both, which affects whether the remap patch touches
  `Platform.h`.

Sent to wkcmake with this document.
