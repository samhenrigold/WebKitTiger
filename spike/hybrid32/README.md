# hybrid32 — a 32-bit process hosting 64-bit code in-process

Follow-up to `spike/ldt64`. There we established that a 32-bit task on 10.4.11 can
install an L=1 LDT code descriptor, `lcall` into it, execute genuine 64-bit
instructions, and keep the upper halves of r8–r15 across ~100 timer ticks. The
idea under evaluation was a mostly-32-bit process (native AppKit/CG/CT/CA) that
hosts 64-bit JSC/WebCore in the same address space, sharing memory instead of
IPC, with a 64→32 thunk layer for libSystem.

These four probes ask whether the *rest* of the runtime — signals, thread state,
the address space, threads and the thunk cost — holds up.

## How the probes are built

Each probe is a single i386 C file compiled with `toolchain/bin/tiger-clang`. The
64-bit guest code is written as `.code64` inline assembly inside the same file,
bracketed by global labels, and `memcpy`'d onto an RWX page at runtime
(`h32common.h`). No separate assembler step, no build system. The only rules are
that the guest must be position-independent (relative branches only, no symbol
references) and must make no libSystem or syscall of its own.

```
tiger-clang -O1 -o sigprobe sigprobe.c        # then scp to the box
```

| file | question |
|---|---|
| `h32common.h` | LDT setup, blob loader, `lcall` wrapper |
| `smoke.c` | sanity: does `.code64` inline asm + far call actually run long-mode code |
| `sigprobe.c` | Q1 signals: `ud2`, `hlt`, unmapped load, `int3`, `SIGALRM` |
| `stateprobe.c` | Q2 `thread_get_state` / `thread_set_state` on a thread parked in the guest |
| `asprobe.c` | Q3 mmap reach, small-code-model addressing ≥ 2 GB, `syscall` |
| `threadprobe.c` | Q4 two threads in long mode, 64→32→64 thunk cost |

Raw runs are in `*-run.txt`.

## Results

| # | question | answer |
|---|---|---|
| 1a | mcontext flavor/size in a handler for a fault taken in long mode | i386 only. `uc_mcsize` = 600 = `I386_MCONTEXT_SIZE` (`x86_EXCEPTION_STATE32` + `x86_THREAD_STATE32` + `x86_FLOAT_STATE32`), every signal, every time |
| 1b | does `SA_64REGSET` (0x0200) change that | no. `sigaction` accepts the flag in a 32-bit process and the frame is still 600 bytes |
| 1c | faulting RIP | truthful, as a 32-bit `eip`. `ud2` → eip = fault address exactly; `hlt` → exact; unmapped load → exact, and `es.faultvaddr` / `si_addr` = 0x30000000, the real target; `int3` → fault+1 (normal trap semantics) |
| 1d | CS in the frame | **trap path reports 0x17** (the default 32-bit user CS — the long-mode selector is simply not there). **Interrupt path reports the real 0x8f** (`SIGALRM` during the guest loop) |
| 1e | r8–r15 / upper halves of rax–rdi anywhere in the frame | **no**. Scanning the 600-byte mcontext, the ucontext, and 2 KB of sigframe stack for `0xC0FFEE00` and `0x1111xxxx` finds nothing (one hit in one alarm run was stale stack data, not a register slot) |
| 1f | can the handler skip the instruction and resume the 64-bit code | it can move `eip` and return, but **execution resumes in 32-bit mode**. Writing `cs = 0x8f` into the mcontext is ignored. The guest's `STORE` sequence then decodes as 32-bit garbage |
| 1g | non-fault signal (`SIGALRM` via `setitimer`) while in the guest loop | **also fatal**. The mode marker (an instruction encoded identically in both modes) reads 1, not 2, after exactly one `SIGALRM`: the thread is permanently demoted to 32-bit at the same address |
| 1h | handler that far-jumps back instead of returning | re-enters long mode (the next fault reports `cs=0x8f`), but **r8–r15 are already destroyed** by the signal delivery path — the guest faults immediately on a garbage r13 |
| 2a | `thread_get_state(x86_THREAD_STATE32)` on a thread parked in the guest | `KERN_SUCCESS`, count 16. `eip` = 0x5049 (the spin loop, exact), `esp` truthful, and `cs = 0x8f` — so you *can* detect that a thread is in long mode |
| 2b | `thread_get_state(x86_THREAD_STATE64)` | `KERN_INVALID_ARGUMENT` (4), nothing returned |
| 2c | `thread_get_state(x86_THREAD_STATE)` (generic) | `KERN_SUCCESS`, count 44 out of 44, but `tsh.flavor = x86_THREAD_STATE32`, `tsh.count = 16`. The 64-bit arm of the union is never used |
| 2d | `thread_set_state(x86_THREAD_STATE64, r15 = 0xFEEDFACE12345678)` | `KERN_INVALID_ARGUMENT` (4); the guest's r15 was still the sentinel after resume |
| 2e | side result | r8 and r15 *do* survive `thread_suspend` / `thread_resume` — they are preserved, just invisible |
| 3a | mmap reach | hints are honoured from 0x40000000 all the way to **0xff000000**, and the pages are readable and writable. 0xfff00000 and above fail with ENOMEM. 0x90000000/0xa0000000 get moved (dyld shared region) — and `MAP_FIXED` there unmaps libSystem and kills the process instantly |
| 3b | unhinted allocations | stay low: a 512 MB and even a 2 GB `MAP_ANON` both land at 0x2008000 |
| 3c | 64-bit code, absolute disp32 to an address ≥ 2 GB (`movl 0x80000000, %eax`) | **broken, and worse than a crash**: the disp32 sign-extends to 0xffffffff80000000 and the process *hangs* — the kernel cannot deliver that fault and retries forever. Watchdog-killed at 6 s |
| 3d | same page via `movabs` + `[reg]` | works, reads 0x5EEDFACE |
| 3e | rip-relative load with the blob itself loaded at 0x80010000 | works, reads 0x5EEDFACE, and `lea`'d address = 0x80010042 = exactly right |
| 3f | `syscall` from long mode in a 32-bit task | **kernel panic**. The box went down hard — no console output, no reboot, power cycle required. Forking does not contain it. Do not run `asprobe --danger-syscall` |
| 1i | does masking signals (`sigprocmask` SIG_BLOCK all) protect the guest | **yes for the signal itself** — with a 10 ms repeating `SIGALRM` pending, 0 handlers ran, the guest stayed in long mode and completed all 200 M iterations. This is the only viable mitigation, and it works |
| 1j | but is the guest's state safe then? | **no, and this is the worst finding.** With signals blocked *and the timer off entirely*, plain preemption still corrupts state: 106–286 corrupt iterations per 200 M across six runs. The split is total: **r15 mismatches = 0 over 1.2 G iterations; the upper half of rdi mismatches 106–286 times per 200 M** (≈ once per 4 ms of execution). r8–r15 are preserved across preemption; the upper 32 bits of the *legacy* registers (rax, rcx, rdx, rsi, rdi …) are not — the kernel saves and restores them as 32-bit. `ldt64/ldt32host` only ever checked r15, so its "survived preemption" conclusion was too broad |
| 4a | two threads in long-mode loops for 2 s each | clean: 1.18 G and 1.18–1.20 G iterations, **0 corruptions each**, with distinct sentinels so a cross-thread leak would show. Two threads in long mode at once is fine (this loop keeps its 64-bit value in r15) |
| 4b | 64→32→64 round trip, empty thunk (`lret` only) | **131.0 ns/call** over 1 M calls (~280 cycles at 2.16 GHz) |
| 4c | round trip calling a local 32-bit C function | **140.2 ns/call** over 1 M calls |
| 4d | round trip calling a **libSystem** function (`getpid`, `gettimeofday`, `write`) | **cannot be done.** The call itself succeeds — the thunk's marker shows `getpid` returned the correct pid — and then the `lret` back into the long-mode segment wedges or #GPs. Not lazy binding (pre-binding every stub changes nothing) and not syscalls as such: a raw `int $0x80` in the thunk returns the right pid and gets back to long mode fine. It is specifically libSystem's own stubs (the commpage `sysenter` path) that poison the return |
| 4e | is the far-call bridge reliable | **no.** ~8 failures against ~1.4 × 10⁹ transitions, but at wildly variable intervals — observed after 3 k, <1 k, 24 k, 78 k, 90 k, 93 k, 224 k and 5.77 M transitions, against other runs of 200 M clean. Always a #GP (trapno 13, garbage error code) or #PF landing back at the guest entry with `cs=0x17`, i.e. the return to long mode silently didn't take. Cause not isolated. At 131 ns/call that is a crash roughly every 25 s of continuous cross-calling |

## Verdict

See the dated section in `NOTES.md`.
