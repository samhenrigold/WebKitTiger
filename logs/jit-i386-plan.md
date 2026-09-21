# Resurrecting JSC's i386 (32-bit x86) JIT — feasibility research

Read-only research. No file under `WebKit/` was touched. Checkout is
`d2f52605`, 2026-09-20 per `NOTES.md`. Context: `spike`/whatever produced the
2.24s-vs-59ms C-loop comparison against Safari 4.1.3's WebKit 533.19 JIT on
the same box is not re-verified here — taken as given per the task.

## tl;dr

**This is a much bigger undertaking than "resurrect the assembler backend,"
and it got bigger again three weeks before this checkout.** The original
framing ("if JSVALUE32_64 is still maintained for ARMv7, resurrecting X86 is
only the assembler/backend layer") was a sound question to ask, but the
answer as of `d2f52605` is **no** — WebKit removed ARMv7 JIT support
entirely on 2026-08-01 (commit `857bd433`, −18,715/+465 LOC across 187
files) and removed the `JSVALUE32_64` 32-bit value representation itself the
very next day, 2026-08-02 (commit `29ceb3c0`, −6,378/+706 LOC across 173
files), explicitly because "32-bit JSValues aren't actually needed for cloop"
now that ARMv7 was the last consumer. **Both of those landed roughly six
weeks before this project's `WebKit` checkout was taken.** Resurrecting X86
JIT today means resurrecting the entire dual-word (payload+tag) value
representation across LLInt, Baseline JIT, and DFG from scratch, on top of
resurrecting the assembler/backend layer — not "only" the latter. §2 has the
detail; §4 has the LOC/effort consequence; §6 has the risk this creates for
project sequencing (the ground keeps moving out from under a JIT
resurrection, and it just moved a lot).

## 1. The X86 (32-bit) JIT removal — a multi-year, multi-step process, not one commit

There is no single "Remove support for x86 32-bit JIT" commit; it happened
in at least four discrete, dated steps, found by walking the path history of
the specific files the task named plus `offlineasm/x86.rb` and searching
GitHub's commit search:

| Date | Commit | Files | LOC | What |
|---|---|---|---|---|
| 2019-03-21 | `c529ce7e795f` "[JSC][x86] Drop support for x87 floating point" | not measured | not measured | Drops the legacy x87-FPU fallback path in the 32-bit x86 macro assembler (needed only for pre-SSE2 CPUs); a simplification step, not full removal — this is likely what the task's "2019-2021" guess was pattern-matching on, but it's a precursor, not the removal itself |
| **2021-08-20** | **`21ea32f9b3f3`** "[JSC] Remove MacroAssemblerX86" (bug 229331) | 5 | **−395/+15** | Deletes `Source/JavaScriptCore/assembler/MacroAssemblerX86.h` outright (the file the task named). Commit message: *"This patch removes MacroAssemblerX86, which allows simplifying some 32bit MacroAssembler code in a subsequent patch."* — i.e. this was understood at the time as one step in an ongoing simplification, not a final removal |
| **2024-05-21** | **`8e3653b4aa09`** "[JSC] Ensure using CLoop for x86 (32bit)" (bug 274452) | 7 | −181/+21 | **The actual "JIT off, LLInt-asm off too" switch.** Commit message: *"This patch ensures that CLoop is enabled on x86 (32bit) and dropping asm LLInt support."* Touches `CMakeLists.txt`, `llint/LLIntOfflineAsmConfig.h`, `llint/LowLevelInterpreter.asm`, `llint/LowLevelInterpreter32_64.asm`, `offlineasm/backends.rb`, `offlineasm/x86.rb`, `cmake/WebKitFeatures.cmake` — this is where 32-bit x86 stopped being able to run even the assembly-language LLInt interpreter, only the C++ CLoop interpreter, several years after the JIT tiers above it were already gone |
| 2024-08-20 | `05024b8ca961` "[JSC] Merge MacroAssemblerX86Common into MacroAssemblerX86_64" | 6 | −4733/+4647 (net ≈ −86, but a full rewrite/merge, not a trim) | Deletes `MacroAssemblerX86Common.h` (the task's third named file) by folding its contents directly into `MacroAssemblerX86_64.h`, since by this point nothing needed the "shared between X86 and X86_64" abstraction any more — there was no X86 left to share with |

**So: the last commit with a working 32-bit x86 *JIT* (Baseline/DFG, not
just LLInt) is the parent of `21ea32f9b3f3`, `82044153d434` (2021-08-20,
same day — the removal commit landed same-day as its own prerequisite,
typical for a reviewed single-purpose patch). The last commit with a working
32-bit x86 *LLInt in assembly* (interpreter tier, below Baseline) is the
parent of `8e3653b4aa09`, `1a8d6ca80b53` (2024-05-21).** Between 2021 and
2024, 32-bit x86 builds ran LLInt-asm with no JIT above it (three years of a
"JIT-less but not fully C-loop" configuration this project never saw, since
it started from a much later checkout).

`X86Assembler.h` itself (7496 lines in the current tree) — the shared
instruction-encoder file the task also named — **was never deleted**; it's
the encoder `MacroAssemblerX86_64.h` still calls, and it still contains
32-bit-mode *instruction encodings* (x86-32 and x86-64 share most of the ISA
encoding, `REX` prefixes are the main x86-64-specific wrinkle), because
x86-64 code generation sometimes still wants to emit a 32-bit-operand-size
instruction. This is a partial, incidental survival, not evidence that
32-bit *mode* (a fundamentally different calling convention, register file,
and pointer width) is supported — it isn't.

## 2. Today's value representation: `JSVALUE32_64` is gone, not just unused by X86

The task's framing assumed `JSVALUE32_64` (the dual-word payload+tag
`JSValue` representation, as opposed to `JSVALUE64`'s single-word NaN-boxed
representation) would still be alive for ARMv7 even after X86 dropped it,
making an X86 JIT resurrection "only" an assembler/backend problem layered
on an otherwise-intact 32-bit JIT pipeline. That was true for years — Igalia
did maintain ARMv7 JIT support using `JSVALUE32_64` long after X86 JIT was
gone (2021-2024+) — **but it stopped being true six weeks before this
checkout**:

- **2026-08-01, `857bd4334690`** — "[armv7] Remove ARMv7 JIT support" (bug
  320416). Full commit message: *"ARMv7 runs on CLoop now. We remove all
  32-bit jit references, and some dangling x86 references too."* 187 files,
  **−18,715/+465 LOC**. This is the commit that removed
  `dfg/DFGSpeculativeJIT32_64.cpp`, `jit/JITOpcodes32_64.cpp`,
  `llint/LowLevelInterpreter32_64.asm`, `offlineasm/arm.rb` (the 32-bit ARM
  offlineasm backend — confirmed absent from the current tree; only
  `arm64.rb`/`arm64e.rb` remain), and the ARMv7-specific
  `assembler/MacroAssemblerARMv7.h`/`ARMv7Assembler.h` (also confirmed
  absent — `ls Source/JavaScriptCore/assembler/` today shows only
  `ARM64*`/`X86_64*` files, no `ARMv7*`/`X86*` files at all).
- **2026-08-02, `29ceb3c03de3`** — "Remove 32-bit JSValues" (bug 320803).
  Full commit message: *"32-bit JSValues aren't actually needed for cloop,
  so let's remove them. While we are at it, we also remove
  NEEDS_ALIGNED_ACCESS."* 173 files, **−6,378/+706 LOC**. This is the
  removal of `JSVALUE32_64` itself — the `JSValue` union/struct split into
  tag+payload words, and every `#if USE(JSVALUE32_64)` branch throughout the
  interpreter, GC, and runtime.

**Confirmed by direct inspection of the current tree** (not just the commit
messages): `find Source/JavaScriptCore -iname "*32_64*"` returns **zero
files**. `Source/JavaScriptCore/llint/` has only `LowLevelInterpreter64.asm`
(no `32_64` variant). `Source/JavaScriptCore/dfg/` has
`DFGSpeculativeJIT.cpp` (18,392 lines, architecture-generic) and
`DFGSpeculativeJIT64.cpp` (no `DFGSpeculativeJIT32_64.cpp`).
`Source/JavaScriptCore/jit/JITOpcodes.cpp` (2,152 lines) is the single
unified file (no `JITOpcodes32_64.cpp`). `Source/JavaScriptCore/offlineasm/`
has `arm64.rb`/`arm64e.rb` (no `arm.rb`).

**Direct answer to the task's question 2: No, `JSVALUE32_64` does not still
have LLInt/Baseline/DFG support for ARMv7 (or anything else) in this
checkout.** ARMv7 itself runs CLoop exclusively now, same as this project's
Tiger port. The premise that made X86 resurrection "only" an assembler
problem is gone: **every tier (LLInt-asm, Baseline JIT, DFG) and the value
representation underneath all three tiers would need reconstruction**, most
of it from a pre-August-2026 WebKit checkout (or from this project's own
still-blobless `WebKit/` copy's git history, which — per `NOTES.md` and the
other agents' `refs/webkit-history/` fetches this session already
established — has none, being depth-1; a fresh non-blobless clone or a
targeted GitHub fetch of the pre-removal tree, exactly as this session did
for the curl/QTKit/layer-hosting topics, would be the way to get the source).

## 3. JIT memory model on Tiger — separately from the value-representation problem, this part is genuinely easier than it looks

Checked `Source/JavaScriptCore/jit/ExecutableAllocator.cpp` in the current
tree (architecture-neutral file, still present and buildable regardless of
the JIT-tier removals above, since something still needs to reserve
executable memory for e.g. the YarrJIT regex engine, RegExp JIT, and wasm —
none of those were removed):

- Modern macOS path (`OS(DARWIN)`, `:177-184`): `mmap(..., PROT_READ |
  PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON |
  (Options::useJITCage() ? MAP_EXECUTABLE_FOR_JIT_WITH_JIT_CAGE :
  MAP_EXECUTABLE_FOR_JIT), ...)`. `MAP_EXECUTABLE_FOR_JIT` (and the
  JIT-cage variant) are **modern macOS-only `mmap` flags — not `MAP_JIT`
  itself, a newer/renamed Apple-internal constant this codebase now uses in
  its place**, and neither exists in Tiger's headers or kernel. This
  confirms and extends what `logs/wkcmake-journal.md:206-207` already found
  in `WTF/posix/OSAllocatorPOSIX.cpp` ("no `MAP_JIT` (10.7)") — the same
  problem recurs here, one directory over, in the file that actually
  allocates JIT code memory rather than general executable memory.
- `#if OS(DARWIN) && HAVE(REMAP_JIT)` (`:189-334`) — a second, more elaborate
  path using `vm_protect`/Mach VM remapping for split
  writable/executable-alias mappings (hardened-runtime era W^X support).
  `REMAP_JIT` won't be `HAVE()` on Tiger (it's an iOS/later-macOS
  capability) so this whole block compiles out, which is actually
  **simpler**, not harder, for Tiger: xnu 792 (Tiger's kernel, per
  `NOTES.md`) predates code-signing enforcement and W^X entirely, so a
  single anonymous `mmap(nullptr, size, PROT_READ|PROT_WRITE|PROT_EXEC,
  MAP_PRIVATE|MAP_ANON, -1, 0)` — no `MAP_JIT`-equivalent flag needed at
  all, RWX pages just work — is both correct and sufficient. This is
  strictly less work than what any currently-supported Apple platform in
  this file needs, since none of the hardened-runtime/JIT-cage/entitlement
  machinery applies. `JIT_CAGE_ENABLED`/`Options::useJITCage()` simply needs
  to read `false` for Tiger, which it already would (JIT-cage requires
  arm64e pointer authentication in the first place — `x86` code paths in
  this same file, further down, already skip the cage entirely).
- `mprotect(..., PROT_READ|PROT_WRITE|PROT_EXEC)` /
  `mprotect(..., PROT_READ|PROT_EXEC)` (`:682-710`, the per-page
  writer-tracking batch-mprotect logic for the "fill-then-seal" allocation
  strategy) — plain POSIX `mprotect`, present on Tiger, no changes needed.
- Signal handling for OSR/exceptions: not separately audited in this pass
  (flagged, not done) — but Tiger's `sigaction`/`SA_SIGINFO` support is
  already established elsewhere in this project (WTF/compat layers use
  `sigaction` for other purposes per the toolchain notes) and JSC's OSR exit
  and StackOverflow-via-signal mechanisms on other 32-bit POSIX platforms
  historically (e.g. 32-bit Linux x86, which had a working JIT well past
  2010) used nothing Tiger's `sigaction` can't do — `ucontext_t`-based
  register inspection on i386 Darwin is a solved, ancient problem (10.4's
  `mcontext_t __ss` register struct is exactly what a JIT signal handler
  needs to read/rewrite `%eip` etc.). Low risk, not zero — needs a spike,
  not a redesign.

**Conclusion for point 3: the JIT memory-allocation and signal-handling
story is the *easy* part of this whole project.** Tiger's total absence of
modern hardening (`MAP_JIT`, W^X, code-signing, JIT-cage, pointer auth) means
the allocator code a resurrection would write is closer to what JSC needed
on Linux x86 in ~2010 than to what today's `ExecutableAllocator.cpp`
actually contains for any currently-supported platform. The
`OS(DARWIN)`-but-not-`HAVE(REMAP_JIT)` branch already mostly does the right
thing; it just needs the `MAP_EXECUTABLE_FOR_JIT`/cage flags stripped to
plain `MAP_ANON` for a `!HAVE(MAP_EXECUTABLE_FOR_JIT)`-style Tiger gate
(new, ~10-20 LOC), matching the pattern the project already used for
`OSAllocatorPOSIX.cpp`'s `MAP_JIT` gate.

## 4. Effort estimate

### 4a. Resurrecting the assembler + `MacroAssemblerX86` + `x86.rb` 32-bit backend

Template: what `MacroAssemblerARM64.h` (**8,348 lines**, current tree)
implements as the architecture-specific half of the shared `MacroAssembler`
API surface defined in `MacroAssembler.h` (**2,545 lines**, architecture-
neutral dispatcher/wrapper). `MacroAssemblerX86_64.h` is **9,951 lines**
today post-merge with the old `MacroAssemblerX86Common.h` — that merged size
is a reasonable proxy for "how big a *from-scratch* X86-32 macro assembler
written against today's `MacroAssembler.h` contract would need to be,"
since X86-32 and X86-64 share the vast majority of their instruction
selection (same ISA family, same `X86Assembler.h` encoder) and differ mainly
in register count (8 vs 16 GPRs), calling convention, pointer width, and the
absence of `REX`-prefixed 64-bit operand forms.

- `X86Assembler.h` (7,496 lines) — **reusable with moderate modification**,
  not a rewrite. It already emits 32-bit-operand-size x86 instructions as a
  subset of what it does for x86-64 (the ISA is upward-compatible); the work
  is auditing for x86-64-only encodings (`REX.W`, the extra 8 GPRs `r8`-`r15`,
  RIP-relative addressing) that would need `#if CPU(X86)` fallbacks or
  exclusion, plus restoring the legacy 32-bit calling-convention argument
  marshalling this file's callers currently assume is x86-64's. Estimate:
  **1,500-2,500 LOC of edits/additions** to re-differentiate the two modes
  cleanly (some of this was exactly what `05024b8ca961`'s 4,647-line merge
  patch did in reverse — that diff is a near-exact map of what to undo, and
  is directly fetchable from GitHub as a reference the same way this
  session fetched the curl/QTKit/layer-hosting topics).
- `MacroAssemblerX86.h`/`MacroAssemblerX86Common.h` equivalent — **write
  from scratch against today's `MacroAssembler.h` contract**, using the
  deleted 2021 `MacroAssemblerX86.h` (395 LOC at deletion, fetchable from
  GitHub at `82044153d434`) and 2024 pre-merge `MacroAssemblerX86Common.h`
  (fetchable at `05024b8ca961`'s parent) as source material, but the
  `MacroAssembler.h` interface they'd need to satisfy has grown
  substantially since 2021-2024 (compare `MacroAssemblerARM64.h`'s current
  8,348 lines against what it likely was in 2021 — not measured in this
  pass, flagged). Estimate: **3,000-5,000 LOC**, using the old files as a
  60-70%-reusable starting skeleton rather than a green field.
- `offlineasm/x86.rb`'s 32-bit backend — the file survives today for
  x86-64; per §1, its 32-bit code paths were stripped in the 2024-05-21
  commit (−181/+21 LOC at that step, but that step only disabled LLInt-asm,
  it didn't remove the Baseline-JIT-relevant 32-bit offlineasm register/
  calling-convention definitions, which per §1's timeline were more likely
  already gone by 2021). Estimate: **300-600 LOC** to restore 32-bit
  register naming, calling convention, and stack-frame layout in the Ruby
  offlineasm backend, using the pre-2024-05-21 file (fetchable at
  `1a8d6ca80b53`) as source.
- `LLIntOfflineAsmConfig.h`/`LowLevelInterpreter.asm` gating — restore the
  `#if CPU(X86)` inclusion of a 32-bit LLInt build, reversing the specific
  6 files `8e3653b4aa09` touched. Estimate: **~50-100 LOC** (mostly gate
  restoration, not new logic) — but this alone gets you **LLInt only**
  (interpreter tier, the thing C-loop already provides in slower C++ form);
  it does not get you Baseline or DFG.
- **Baseline JIT** (`jit/JIT*.cpp`, `jit/JITOpcodes.cpp` 2,152 lines
  today) — per §2, these are now fully `JSVALUE64`-shaped; restoring
  32-bit-value-aware Baseline JIT code generation for every opcode is a
  full second pass on top of the assembler work, using the deleted
  `JITOpcodes32_64.cpp` (fetchable at `857bd433`'s parent,
  `<parent-sha not captured in this pass, flagged>`) as source. Estimate:
  **4,000-8,000 LOC** — this file family was one of the largest deleted in
  the August 2026 ARMv7 purge.
- **DFG JIT, 32_64 tier** (`dfg/DFGSpeculativeJIT32_64.cpp`, deleted
  2026-08-01, size not measured in this pass but was one of 187 files in a
  −18,715-line deletion alongside `DFGSpeculativeJIT.cpp`'s
  architecture-neutral 18,392-line current sibling) — **the single largest
  remaining piece.** Estimate: **8,000-15,000 LOC**, wide range because this
  file's actual pre-deletion size wasn't measured (flagged, not done — a
  `git show 857bd433^:Source/JavaScriptCore/dfg/DFGSpeculativeJIT32_64.cpp
  | wc -l` against a fetched copy would resolve this to an exact number in
  under a minute, worth doing before committing to a schedule).

**Running total for a full LLInt+Baseline+DFG 32-bit x86 JIT, written
against *today's* (post-August-2026) `MacroAssembler`/JSC architecture:
roughly 17,000-31,000 LOC**, not counting integration/build-system work,
debugging, or the almost-certain discovery of additional call sites that
assumed `JSVALUE64` when `USE(JSVALUE32_64)` was deleted (grep hygiene after
a `git revert`-style resurrection would itself be a multi-week task, since
`29ceb3c03de3` alone touched 173 files — every one of those is a place a
resurrection has to re-examine, not just the JIT-tier files listed above).

### 4b. A cheaper alternative: LLInt-only, skip Baseline/DFG entirely

Given the 2021-2024 window already proved a 32-bit-x86-LLInt-without-JIT
configuration was buildable and shipped (three years of real-world use, not
speculative), **the smallest real win is LLInt-asm alone**: restore the six
files `8e3653b4aa09` touched (§1, ~50-100 LOC of gate restoration) plus
enough of `X86Assembler.h`/a minimal 32-bit offlineasm backend to assemble
LLInt's hand-written `.asm` opcodes (LLInt doesn't need the full
`MacroAssembler` C++ API — it only needs offlineasm's Ruby-level x86
lowering, a much smaller surface than Baseline/DFG's C++ macro-assembler
calls). This is **the ARM64_32/x32-style "32-bit pointers, fast interpreter,
no JIT" middle ground**, and it's the one closest to what's actually still
recently proven to work. Rough LOC: **500-1,500** (offlineasm x86 backend
restoration + LLInt config gates), an order of magnitude cheaper than full
Baseline/DFG, though also an order of magnitude smaller a win — LLInt alone
is JSC's interpreter tier, meaningfully faster than CLoop (it's real
assembly instead of a C++ switch-dispatch loop) but nowhere near Baseline
JIT's speedup, let alone DFG's. **No concrete multiplier is asserted here for
LLInt-vs-CLoop speed on this exact 2M-iteration workload** — not measured,
would need the same kind of spike this project already ran for the CLoop
vs. Safari-4.1.3-JIT comparison (flagged as the next concrete step if this
path is pursued).

## 5. Alternative: a leaner interpreter instead of resurrecting JIT machinery

`IPInt` (`Source/JavaScriptCore/wasm/WasmIPInt*`, confirmed present:
`WasmIPIntGenerator.{cpp,h}`, `WasmIPIntSlowPaths.{cpp,h}`,
`WasmIPIntPlan.{cpp,h}`, `WasmIPIntTierUpCounter.h`,
`WasmFunctionIPIntMetadataGenerator.{cpp,h}`) is **WebAssembly-only** — "IP"
is "in-place interpretation" over Wasm bytecode directly, a design specific
to Wasm's already-typed, already-validated bytecode format. **There is no JS
equivalent in this tree.** JS's interpreter tier is LLInt
(`Source/JavaScriptCore/llint/`), which already *is* JSC's answer to "a
leaner/faster interpreter than a naive switch loop" — it's a hand-written
assembly interpreter (or, on platforms without an assembly LLInt like this
project's CLoop-only Tiger target, a C++ transliteration of the same
bytecode dispatch, `LowLevelInterpreter.cpp`). **CLoop already is JSC's
"minimal fast interpreter" fallback tier** — there isn't a second, leaner
one to reach for instead of LLInt/JIT resurrection; CLoop vs. LLInt-asm vs.
Baseline vs. DFG is already the full tier ladder, and this project sits at
the bottom rung by necessity (no assembler backend for the target CPU/mode).
The only lever this section identifies that isn't "resurrect a JIT tier" is
**tuning CLoop itself** — not investigated in this pass (flagged): whether
`LowLevelInterpreter.cpp`'s C++ dispatch uses computed-goto/`__builtin_expect`
hints GCC/Clang can exploit well on i386, whether Tiger's `clang` build
(per `NOTES.md`, Apple clang 21 targeting `i386-apple-macosx10.4`) generates
worse dispatch code than a same-vintage GCC would have for the original
2009-era JIT comparison's baseline, and whether any CLoop-specific
`Options::` flags (inlining thresholds, etc.) are tuned for the workload.
This is real but almost certainly a **percent-level**, not
multiples-level, lever compared to actual code generation — not a
substitute for the 2.24s-vs-59ms gap the task is trying to close.

## 6. Risks

- **The ground keeps moving, and it just moved a lot, right before this
  checkout.** Two commits landed six weeks before `d2f52605` that
  specifically removed the last non-X86 consumer of the exact
  infrastructure (`JSVALUE32_64`, 32-bit JIT tiers) this task hoped to
  reuse. This is not a one-time correction to a stale assumption — it's
  evidence that upstream is actively *simplifying away* 32-bit support
  project-wide (ARMv7 was, per the commit message, moved to CLoop rather
  than kept on JIT — the same fallback this project already uses), which
  means every future `WebKit` checkout this project pulls from upstream is
  more likely to have *less* 32-bit-adjacent code to build from, not more.
  A resurrection effort should assume it is working against a permanently
  fixed historical source snapshot (fetched once, as this session already
  did for other deleted-code topics), not something that stays reconcilable
  with a live upstream tracking branch.
- **Scope creep from "port an assembler" to "port a value representation."**
  §2's finding means the effort in §4a is a floor, not a ceiling — every one
  of the 173 files `29ceb3c03de3` touched is a candidate for a missed
  `USE(JSVALUE32_64)` dependency a resurrection would need to re-add, most
  of them outside the JIT directories entirely (GC barriers, `JSValue.h`
  itself, `JSCJSValue.h`, property storage, `Structure`/`StructureID`
  encoding — anywhere a `JSValue` is stored, compared, or boxed).
- **No hardware validation path.** Nothing in this pass tested that any of
  the surviving 32-bit-adjacent code (`X86Assembler.h`'s residual 32-bit
  encodings, `ExecutableAllocator.cpp`'s Tiger-viable `mmap` path) actually
  produces correct machine code on real i386 Tiger hardware — this is
  research from source reading, not a spike. The existing 2.24s CLoop
  baseline is real (per the task); nothing about a JIT's correctness or
  actual speedup on *this specific* Core 2 Duo, this xnu 792 kernel, and
  this Apple clang 21 toolchain has been measured.
- **Maintenance burden after landing.** Even the "cheap" LLInt-only path
  (§4b) creates a permanently-diverged fork of `offlineasm/x86.rb` and the
  LLInt config that upstream will never reconcile (per the first risk
  above) — every future rebase against a newer WebKit checkout re-applies
  this patch by hand, forever, since upstream is deleting this exact
  surface, not maintaining it in parallel somewhere this project could pull
  from.
- **Debuggability.** A resurrected JIT on an architecture upstream abandoned
  five years ago (Baseline/DFG) to three weeks ago (LLInt-asm's last living
  32-bit relative, ARMv7) has no upstream test coverage, no CI, and no bug
  reports to learn from — every miscompile this project's own JS test
  content triggers is this project's to diagnose alone, on top of already
  debugging the rest of the Tiger port.

## Not done in this pass

- Did not measure the pre-deletion LOC of `dfg/DFGSpeculativeJIT32_64.cpp`,
  `jit/JITOpcodes32_64.cpp`, or `assembler/MacroAssemblerARMv7.h`/
  `ARMv7Assembler.h` directly (§4a's DFG-tier estimate is the widest-ranged
  number in this document as a result) — fetching them from GitHub at
  `857bd4334690^` (parent of the ARMv7 removal commit) the same way this
  session fetched the curl/QTKit/layer-hosting topics would resolve this to
  exact figures quickly.
- Did not measure `MacroAssemblerARM64.h`'s size in 2021 (around when
  `MacroAssemblerX86.h` was deleted) to sanity-check whether the "write a
  new 32-bit macro assembler against today's `MacroAssembler.h` contract"
  estimate in §4a should be scaled down (2021-era API surface) or is
  correctly sized against today's (2026-era, larger) surface — used
  today's ARM64 file size throughout as the conservative (larger) proxy.
- Did not audit Tiger's `sigaction`/`ucontext_t` support for OSR-exit and
  stack-overflow signal handling in any JSC-specific way (§3's signal-
  handling paragraph is a plausibility argument from general Darwin/i386
  knowledge, not a code read of how JSC's `Signals.cpp`/`MachineContext.h`
  actually use them on any currently-supported 32-bit-pointer platform,
  since none remain in the current tree to compare against directly).
- Did not spike-test or benchmark anything — this document is source
  research only, per the task's read-only scope.
