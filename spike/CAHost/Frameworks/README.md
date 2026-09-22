# Rebundled QuartzCore

`QuartzCore.framework/Versions/A/QuartzCore` is Leopard's i386 QuartzCore (1.6.0) rebased on
disk with `tools/rebase-dylib.py` to `__TEXT 0x60000000` (`__DATA 0x70000000`), prebinding
cleared. `QuartzCore.leopard-original` is the untouched input.

Why: the original is a split-seg dylib prebound into the 10.4 shared-region range
(0x93c42000). Loading it there made dyld either map it into the system-wide shared region
(`shared_region_make_private_np` in every process that linked it) or slide it at load
and fault in `doRebase` (split-seg relocations are `__DATA`-relative; see the tool).
At a fixed address below the shared region dyld maps it like any dylib: no shared
region, no runtime rebase. Verified 2026-09-22 with a load-time-linked and a dlopen test
program on the box.

Regenerate:

    python3 tools/rebase-dylib.py QuartzCore.leopard-original QuartzCore 0x60000000
