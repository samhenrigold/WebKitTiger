# Pinning to the last WebKit with a working x86-32 JIT — feasibility

Read-only research, `gh api`/WebFetch/WebSearch against github.com/WebKit/WebKit,
bugs.webkit.org, and Debian's changelog. No file under `WebKit/` touched.
Companion to `logs/jit-i386-plan.md` (which asks "can we resurrect x86-32 JIT
on top of *today's* tree" and answers "17k-31k LOC, JSVALUE32_64 itself is
gone"). This document asks the different question: instead of porting JIT
code forward, **pin the whole tree back** to a revision where it still works.

## tl;dr

The x86-32 JIT was not killed by one decision at one time; it died port by
port over three years, and the *last* rites (deleting `MacroAssemblerX86.h`)
landed literally minutes after Igalia fixed a 32-bit DFG compilation bug —
i.e. it was still being actively maintained and used (by GTK/WPE on Linux
i386) right up to the moment it was deleted, just no longer on Apple's own
platforms. **Recommended pin point: commit `82044153d434` (2021-08-20T17:47:48Z),
the parent of the removal commit `21ea32f9b3f3`.** Below that, JSVALUE32_64,
ARMv7 JIT, and MacroAssemblerX86/X86Common are all present and were exercised
by CI (Linux GTK/WPE bots), just never by an Apple Mac bot. Cost: ~5 years of
missed web-platform features and security fixes, frozen forever (see §3), for
a JIT that still needs real Mac/Tiger-specific bring-up work, not a free win
(see §4).

## 1. The x86-32 JIT death timeline (multiple steps, multiple ports)

| Date | Commit/bug | Port | What |
|---|---|---|---|
| 2018-05-25 | r232212, refined r232719 (bug [185989](https://bugs.webkit.org/show_bug.cgi?id=185989)) | **Windows (WinCairo)** | JIT disabled for 32-bit Windows builds. Reporter's stated reason: *"This code is not being maintained. For some 32 bit apps, the JIT doesn't provide a performance benefit. Disabling the JIT will also save memory."* Windows-specific; GTK/Mac untouched. |
| 2018-06-12 | bug [182886](https://bugs.webkit.org/show_bug.cgi?id=182886) "Disable JITs on 32-bit platforms by default" | proposed: all 32-bit | **RESOLVED WONTFIX.** Igalia pushed back hard: their data showed 24 fps with JIT vs. 6 fps without on real ARMv7/MIPS content, and they had CI covering it. This is the load-bearing fact for the whole plan — as late as mid-2018, 32-bit JIT (generically, not just x86) was contested but *defended and kept*, not abandoned. |
| ~2019-06 | [webkit-dev thread](https://lists.webkit.org/pipermail/webkit-dev/2019-June/030718.html), "Windows 32-bit support?" | Windows | By this point contributors state plainly that JIT "does NOT work on 32-bit x86" on Windows (consistent with 2018's disable) and point to CLoop/AppleWin as the fallback. Mac and GTK not asserted broken here. |
| **2021-08-20T17:47:48Z** | `82044153d434`, bug 229293, "Fix DFG compilation of StringCharAt in 32 bits jsc debug build", Mikhail R. Gadelha (Igalia) | GTK/WPE (generic 32-bit DFG) | **The last commit with the JIT intact**, and it is itself an active bug fix to the DFG 32-bit tier — i.e. someone was still running and debugging 32-bit JIT content three minutes before it was deleted. This is the pin candidate. |
| 2021-08-20T17:50:51Z | `21ea32f9b3f3`, bug [229331](https://bugs.webkit.org/show_bug.cgi?id=229331) "[JSC] Remove MacroAssemblerX86", Yusuke Suzuki, r=Mark Lam | X86 (32-bit) specifically | Deletes `assembler/MacroAssemblerX86.h` (−395/+15 LOC, 5 files). Bug text is a bare one-line title with no rationale; reviewed and landed same day. This removes the x86-32-specific half of the macro assembler; ARMv7 JIT and JSVALUE32_64 both survive this commit untouched. |
| 2024-05-21 | `8e3653b4aa09`, bug 274452 | x86 (32-bit), all ports | "Ensure using CLoop for x86 (32bit)": drops 32-bit LLInt-*asm* too, so any x86-32 port (there weren't really any left) goes fully CLoop. |
| 2026-08-01 / 2026-08-02 | `857bd4334690` / `29ceb3c03de3` | ARMv7 / all | ARMv7 JIT removed, then JSVALUE32_64 itself removed six weeks before this project's checkout — covered in `logs/jit-i386-plan.md` §2. |

**Which ports "still built it," concretely:** WinCairo's 32-bit JIT was off
from mid-2018 on — pinning to save Windows's JIT buys nothing, it was already
gone. The only port with continuously-CI-exercised 32-bit JIT through
2021-08-20 is **GTK/WPE on Linux i386**, corroborated independently by
Debian's own packaging history: Debian's webkit2gtk changelog explicitly
says *"WebKit generates SSE2 instructions with its JIT compiler"* and that
Debian's own patch was only to disable JIT and force CLoop **for non-SSE2
i386 hardware** (old Pentium-class CPUs) — i.e. on SSE2-capable i386
(anything Pentium III/Athlon XP-class or newer, which covers the Tiger
target Core 2 Duo many times over), Debian shipped the real JIT, not CLoop,
up until upstream removed the capability outright. (Debian's `trixie`
release, 2025, later "enabled JIT again" for i386 in a different context —
that is downstream of the *2026* JSVALUE32_64 removal noise and not
informative about 2021-era health.) **Apple never ran an x86-32 JIT bot on
this code in the timeframe that matters**: Mac dropped 32-bit app support
architecture-wide well before 2021 (Catalina, 2019), and WebKit's own Mac
port had been 64-bit-only in Apple's own build config for years by then, so
"Apple bot coverage of x86-32 JIT" is effectively zero for the whole window
in this table — the only real-world validation this code got after ~2014 was
Linux/GTK.

## 2. Recommended pin revision

**`82044153d434c9dcef0d00a30f0d3c41fe2d9d22`** (2021-08-20T17:47:48Z), the
immediate parent of the MacroAssemblerX86 removal. Rationale for picking the
parent of the deletion commit rather than something earlier with a safety
margin: the deletion commit itself is a pure file-removal with no logic
change (confirmed 5 files touched, −395/+15, in bug 229331), so its parent is
byte-for-byte "the tree with the JIT" and gives the *newest possible*
starting point — maximizing every other axis (feature completeness, security
fixes, compiler/toolchain compatibility) subject to the JIT constraint. There
is no meaningfully "safer" earlier commit to prefer over this one: the JIT
was not mid-bitrot at this point (the commit immediately before is itself a
32-bit JIT bug fix, not a "last gasp" symptom), so there's no evidence an
earlier 2019 or 2020 revision would be more trustworthy, only older and
missing more since-landed unrelated fixes.

A **release tag is not a better choice here**: Safari 15's branch point
(`safari-612-branch`) was cut from `main` around September 2021, i.e. *after*
`82044153d434`/`21ea32f9b3f3` — the JIT is already gone on that branch too.
There is no Safari/Safari Technology Preview tag that both (a) postdates
`82044153d434` and (b) predates the removal; the pin has to be a raw `main`
commit, not a shipped-Safari tag.

## 3. What the 2021 tree lacks vs. 2026 — web-platform and security cost

This is the real price of this option, and it compounds every year the pin
sits unmoved (see §5's maintenance risk — same shape as `jit-i386-plan.md`
§6's "ground keeps moving" risk, but pointed backward instead of forward).

**Web-platform features landed 2021→2026 that a 2021-08-20 pin will never
get** (Safari/WebKit shipped these across Safari 15 through the in-flight
Safari 27 line; this project's own checkout, per `NOTES.md`, is already at
`d2f52605`, itself mid-Safari-27):
- CSS: `:has()`, container queries, CSS nesting, cascade layers (`@layer`),
  subgrid, `@scope`, CSS anchor positioning, `text-wrap: balance`, trig/color
  functions (`oklch`, `color-mix()`), View Transitions.
- JS/JSC language features landed after August 2021: class static blocks,
  `Array.prototype.group`/`groupToMap`, `Array.fromAsync`, `Object.groupBy`,
  the RegExp `v` flag / set notation, `Array` "change by copy" methods
  (`toSorted`/`toReversed`/`with`), decorators, `Temporal` (in progress
  across WebKit), import attributes/`with { type: "json" }`, WeakRef/FinalizationRegistry
  had just landed pre-2021 but iteration helpers and other stage-4 proposals
  since are absent.
- Wasm: GC, tail calls, multi-memory, exception handling, and IPInt (WebKit's
  Wasm in-place interpreter, unrelated to the JS JIT question in §4) all
  postdate the pin.
- Media/graphics: WebCodecs, WebGPU (WebKit shipped it after 2021), AVIF
  decode, and multiple Interop-2022-through-2025 focus-area fixes across
  forms, editing, media, and pointer/touch events.
- This list is representative, not exhaustive — a full audit would walk
  WebKit's "Safari `N` release notes" and webkit.org "In WebKit" tag from
  Safari 15 through the current Safari 27 series; not done in this pass
  (flagged).

**Security cost:** roughly five years of WebKit/JSC CVE fixes never
backported — Safari 15 through 27 correspond to dozens of disclosed WebKit
CVEs a year (memory corruption in DOM/layout, JSC type confusions, Wasm
sandbox escapes). A pinned 2021 tree is a known-exploitable browser engine
by construction; this is an acceptable trade *only* because the deployment
target (a personal Tiger box, not internet-facing at scale) already accepts
comparable risk from the OS itself (10.4.11, EOL since 2009, no further
security updates — see `NOTES.md`'s "BOX STATE FROZEN" entry). Worth stating
plainly to the user as a cost, not waving away: this is qualitatively worse
than the 2026-tree option, which at least tracks upstream JSC/WebCore
hardening even though it lacks a JIT.

## 4. Cocoa-port viability of the 2021 pin — how much Tiger porting work transfers

- **WebKitLegacy present:** yes — WebKitLegacy (WebKit1) was not scheduled
  for removal until 2025-2026-era discussions; it is fully present and
  building at this revision, same as the 2026 checkout this project already
  targets.
- **CMake Cocoa support:** **exists**, contrary to the plan brief's guess.
  `Source/cmake/OptionsMac.cmake` was present and actively edited at this
  time — GitHub history shows a "Update Mac-specific CMake files" commit
  2021-04-26 and continued edits through late August 2021 (e.g.
  "[CMake] ICU 61.2 is required to build WebKit since r281375", 2021-08-27,
  one week after the pin point). So the CMake-based Cocoa build this
  project's `wkcmake` track already built against the 2026 tree is not a
  2026-only convenience; it should carry over with modest adjustment, not a
  rewrite. This significantly de-risks the "CMake configuration may not
  transfer" caveat in the task brief.
- **Curl backend:** present. Per `NOTES.md`'s own curl-history finding,
  `ResourceHandleCurl.cpp` and friends were only removed 2023-03-20
  (`f57e6ee12e74`) — well after this pin point, so the *current* WK1 curl
  backend plan (restore from the 2023-03-19 snapshot already checked into
  `refs/webkit-history/curl-resourcehandle/`) becomes unnecessary at a 2021
  pin: the backend is simply still in-tree, unmodified, no restoration step.
  This is a net *simplification* versus the 2026-tree plan.
- **QTKit/media backend:** the Mac QTKit `MediaPlayerPrivateQTKit` backend
  was removed in 2018, i.e. **already gone at the 2021 pin**, same as the
  2026 tree. No difference here — this project's QTKit-based video plan
  (`logs/qtkit-plan.md`) is equally necessary (or unnecessary) at either
  pin point; it was never going to be "free" from an older checkout.
- **Swift usage / ObjC ARC status:** WebKit's Cocoa-side Swift adoption
  (WebKit's Swift overlay/bridging work) is a post-2021 development in
  earnest; a 2021 pin has effectively zero Swift in the build, which is
  *less* porting surface for this project (no Swift toolchain story needed
  for Tiger at all) — a genuine simplification. ARC status of
  WebCore/WebKitLegacy at 2021 was already largely ARC-converted for
  Cocoa-specific Objective-C++ files (that migration mostly completed
  2016-2019); no material difference from 2026 expected here, not directly
  verified in this pass (flagged).
- **C++ standard / compiler requirements:** WebKit required C++17 by 2021
  (the C++20 migration and associated compiler-version floor increases are
  substantially a post-2021 story). This project's toolchain (`tiger-clang`,
  patched Apple clang 21 per `NOTES.md`) is far newer than anything C++17
  needs and will compile a 2021-era tree with room to spare — likely *less*
  friction than compiling the 2026 tree's newer C++ standard usage against
  the same compiler, though this project has already solved that problem
  for the current tree, so it's a wash rather than a win.
- **Net verdict:** the compat layer (`compat/`, `toolchain/`, the ObjC
  fragile-ABI/ARC bridging, the SDK overlay) transfers wholesale — none of
  it is WebKit-version-specific, it's Tiger/toolchain-specific. The
  WebKit-side CMake and curl work done so far *also* substantially transfers
  or becomes moot (curl backend just exists, no restoration needed). The
  parts that would need to be redone: any WebCore/JSC-internals fix already
  made against the 2026 tree during this project's M2 WebCore pass (per
  `NOTES.md`'s "97/557 failed" entry) would need to be re-derived against
  2021-era source, since the exact files/line numbers differ five years
  back — a re-application, not a rewrite, but real work, not free.

## 5. Effort to get x86-32 JIT actually working at the pin, on Tiger specifically

This is the part the task brief anticipated ("Igalia's ARMv7 32_64 upkeep at
the time implies the shared 32_64 tiers were healthy") and the evidence in
§1 confirms directly: `82044153d434` itself is Igalia fixing a **DFG** 32-bit
bug, meaning at the pin point LLInt-asm, Baseline, *and* DFG are all being
exercised for 32-bit, not just alive in source form. This makes the 2021 pin
categorically cheaper than the 2026-tree resurrection in `jit-i386-plan.md`
(17k-31k LOC estimate, because JSVALUE32_64/DFGSpeculativeJIT32_64/etc. don't
exist there at all): **here they exist, build, and were CI-green on GTK/WPE
the same week.** The work is not "write the 32-bit value representation and
three JIT tiers from scratch," it's:

1. **Confirm the Mac/Cocoa build path actually compiles+links this code at
   all.** X86 JIT bugs specific to Windows were disabled in 2018 explicitly
   because they weren't maintained *there*; nothing found in this pass
   suggests a parallel "disabled on Mac" flag existed (the Mac/Cocoa build
   was already 64-bit-only by policy, so ENABLE_JIT for x86-32 on Mac was
   likely simply never exercised by a build, rather than explicitly turned
   off) — meaning this is genuinely unknown/unbuilt territory for the Cocoa
   port specifically, not a bitrotted-but-working configuration. Needs a
   build spike, not assumed to "just work" because GTK's did.
2. **Port `jit/ExecutableAllocator.cpp`'s Tiger `mmap`/`mprotect` gate**,
   same finding as `jit-i386-plan.md` §3: this part is *easier* on Tiger
   than on any currently-supported platform (no MAP_JIT, no W^X, no
   code-signing enforcement, xnu 792 predates all of it) and that finding is
   unchanged by which tree it's applied to — the 2021-era
   `ExecutableAllocator.cpp` will have the same POSIX `mmap`/`mprotect` shape,
   modulo needing to check it doesn't yet have the 2026-era `MAP_EXECUTABLE_FOR_JIT`
   complexity to strip out (a 2021 file is very likely *simpler* here, not
   harder — not directly diffed in this pass, flagged).
3. **Fix whatever Mac/Cocoa-specific gaps surface** — calling-convention
   assumptions, `PlatformCPU.h`/`PlatformOS.h` gating that assumed "if
   `PLATFORM(MAC)` then 64-bit," any `#if OS(DARWIN) && CPU(X86_64)` that
   should have been `CPU(X86)`-inclusive but wasn't because no one tested
   it. This is the real unknown and the main reason to spike before
   committing: unlike the Windows disable (which had specific named test
   failures — regexp/worker per bug 185989) there's no comparable bug trail
   for "x86-32 JIT on Mac specifically is broken," which cuts both ways:
   no known blocker, but also no evidence of it ever passing on Apple's own
   platform.
4. **No W^X/MAP_JIT/Spectre-mitigation constraints on the Tiger target** —
   worth restating from `jit-i386-plan.md` §3: several of the concerns
   that made 32-bit JIT contentious upstream (bug 182886's Spectre/Meltdown
   mitigation argument) are moot for a single-user, pre-2010 kernel target
   with no other untrusted code sharing the process/host in a way that
   matters.

**Estimate: low hundreds to low thousands of LOC** of Tiger/Cocoa-specific
JIT bring-up fixes plus the `ExecutableAllocator.cpp` gate (~10-50 LOC, per
`jit-i386-plan.md` §3's sizing) — an order of magnitude or two below the
17k-31k LOC estimate for resurrecting onto the 2026 tree, because the value
representation and all three JIT tiers already exist, compile, and were
proven on a sibling 32-bit port (Linux/GTK) the same week as the pin point.
This is a spike-sized effort (days), not a rewrite-sized one (weeks-months),
*if* step 1's build-path assumption holds — which is the one real unknown
and should be the very first thing tried, before committing to the rest of
this plan.

## 6. Comparison table

| | Keep 2026 tree, interpreter only | **Pin to `82044153d434` (2021-08-20), resurrect x86-32 JIT** | 64-bit content process (spiked separately) |
|---|---|---|---|
| JS speed vs. Safari 4.1.3 JIT baseline (2.24s vs 59ms, `NOTES.md`) | No change: interpreter-speed, ~38x slower on the test loop; only percent-level CLoop tuning available | Should close most of the gap — JIT vs. CLoop is the whole reason for the 38x number; not benchmarked on Tiger in this pass | Would use JSC's real 64-bit JIT (JSVALUE64), the fastest tier available anywhere in WebKit today, but only inside a 64-bit process |
| JIT engineering cost | None (decision already made, per `jit-i386-plan.md`) | Low hundreds-thousands of LOC (§5) *if* the Cocoa-build assumption in §5.1 holds; unknown until spiked | Separate track; not estimated here (out of scope for this document) |
| Web-platform currency | Full — tracks live upstream `main` (this project's own checkout is `d2f52605`, 2026-09-20) | **Frozen at 2021-08-20**, missing 5 years of CSS/JS/Wasm features (§3) | Full — same upstream tree as column 1, JIT question is orthogonal |
| Security posture | Tracks upstream fixes up to the fork point; still stale the moment it forks, but the freshest of the three | **~5 years of unpatched WebKit/JSC CVEs**, frozen forever unless re-pinned later (worse than column 1 by construction) | Same as column 1 |
| WebKitLegacy/Cocoa porting work done so far | Fully applicable (it's the current base) | Compat layer transfers wholesale; CMake Cocoa support already existed in 2021 (§4) so less re-work than feared; curl backend needs *no* restoration (still in-tree, §4); WebCore M2 fixes need re-derivation against 2021 line numbers | Applies to whichever WebKit-version base is chosen; independent axis |
| Ongoing maintenance burden | Standard "rebase against upstream" burden, same as any fork | A second, permanent fork axis on top of the Tiger-porting fork: upstream will never again touch 32-bit x86 JIT, so nothing to merge forward from there, and every Tiger-side fix has to happen on an ever-more-dated base with no security backport path (same shape as `jit-i386-plan.md` §6's "ground keeps moving," pointed at a fixed historical snapshot instead) | Standard rebase burden; no second fork axis for the JIT itself |
| Biggest open risk | None new (decision made) | Unknown whether x86-32 JIT was *ever* exercised on Apple's own Cocoa/Mac build — no bug trail either way (§5.1); must spike before committing | Scope/complexity of a 64-bit content process on a 32-bit-only OS API surface — separate investigation |

## Recommendation

Do the **§5.1 build-path spike first, cheaply, before deciding.** It answers
the one unknown that determines whether "pin to 2021" is a
days-to-low-weeks project or a project with a hidden landmine (a JIT that
compiles on Linux/GTK but has never once run on a Cocoa/Mac target and turns
out to need substantial calling-convention or ABI archaeology once tried).
If the spike is clean, pinning to `82044153d434` is a genuinely attractive
middle option: it buys most of Safari 4.1.3's JIT speedup for a bounded,
days-scale engineering cost, at the price of freezing the web platform and
security posture at 2021 — a real cost but one this project has already
implicitly accepted at the *OS* layer (Tiger itself is 17 years EOL) and
could reasonably accept at the *browser-engine* layer too, as a deliberate,
disclosed trade rather than an accident. If the spike surfaces real Cocoa/x86
JIT breakage, the cost estimate in §5 should be treated as a floor, not a
number to commit a schedule to, and the 2026-tree interpreter-only path
(already decided per `jit-i386-plan.md`) remains the safe default.

## Not done in this pass

- Did not attempt to build or run anything — source/bug-tracker research
  only, per the task's read-only scope (no file under `WebKit/` touched).
- Did not diff 2021-era `ExecutableAllocator.cpp` against the current tree
  directly (§5 point 2, flagged) — would take one `gh api`/`git show` call
  against a fetched copy of `82044153d434`.
- Did not do a full feature-by-feature audit of Safari 15-through-27 release
  notes for §3's list — it is representative from general knowledge of the
  period, not exhaustive; a full audit would walk webkit.org's release notes
  and "In WebKit" posts commit-by-commit.
- Did not verify WebCore/WebKitLegacy ARC-conversion completeness at the
  2021 pin directly (§4, flagged).
- Did not check whether any Apple internal (non-public) bot ever built an
  x86-32 Mac JIT configuration after ~2014 — only public bugzilla/mailing-list/
  GitHub evidence was available to this pass, and it is silent on the
  question in both directions (§5.1's core finding).
