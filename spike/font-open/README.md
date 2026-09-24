# FreeType resource-fork opening on Tiger

Run `python3 spike/font-open/run-probe.py` to build and run this regression using
Tiger's existing local/remote lease and idle guard. The default also links the
same source against `build/handoff/libfreetype-before-resource-attributes.a` as a
negative control. `--build-only` makes no remote connection;
`--without-negative-control` is available when the saved archive is absent.
`TIGER_HOST` selects the target, defaulting to `tiger-eth`.

The runner freezes the C source and each linked static archive into a unique
`logs/probes` directory, records source/archive/binary/tool hashes and compiler
arguments, and transfers only the two executables. It does not rebuild or modify
FreeType, the toolchain, compatibility libraries or installed font files. Each
executable has a 15-second alarm, with an additional exec-surviving 20-second
remote watchdog. Remote binaries live in a unique directory that is removed
while the lease is held. Raw stdout, stderr, exit codes and parsed results remain
local. No font bytes are copied or committed.

The C probe opens all four Courier New faces through both `/Library/Fonts/Courier
New` and its `..namedfork/rsrc` path. It requires all four expected PostScript
names, selects the Unicode charmap, sets 22-point size at 72 dpi, loads `S`
unhinted and renders it to a gray bitmap. Positive checks require successful APIs,
a nonzero Unicode glyph, positive metrics, and actual nonzero raster coverage.
Both path aliases must yield matching names, metrics, dimensions and bitmap
checksums at every FreeType index. Monaco and Courier dfont faces are independent
positive controls. Any failure makes the executable exit nonzero. The Python
runner accepts the old archive only when **all eight Courier New opens fail**,
both controls pass, the complete summary is present and the executable exits 1.

Run `python3 spike/font-open/test_run.py -v` for the six local result-checker tests,
including missing records, API/metric failures, alias mismatch and a broken
positive control. These tests do not replace the target run.

## Verified target result

`logs/probes/20260923-211837-font-open-b71aec686b` completed on actual Tiger with
FreeType 2.13.3. The rebuilt executable exited 0: all eight Courier New cases and
both controls passed. The saved old archive exited 1: normal-path opens returned
error 2 and named-fork opens error 8 for every Courier New face. Monaco and Courier
still opened and rendered successfully, with the same metrics and bitmap checksums
under both archives.

| FreeType index | PostScript name | S bitmap | FNV-1a checksum |
| --- | --- | --- | --- |
| 0 | CourierNewPS-BoldItalicMT | 13 × 15 | b5ec195a |
| 1 | CourierNewPS-ItalicMT | 12 × 14 | f856bb54 |
| 2 | CourierNewPS-BoldMT | 11 × 15 | f547bc2f |
| 3 | CourierNewPSMT | 11 × 14 | 008edd7e |

Both path forms produced exactly the table above. Each Courier New face has
2048 units/em, Unicode `S` glyph 54, and unhinted advance 845/64 pixels at 22 pt.
The ordering is FreeType's resource order, not ATS's native face order.

The rebuilt archive SHA-256 was
`e9178c4d518ee01a7ba3acd032321330fb0236815ae2b2ae0d402a6678c9fef4`;
the saved archive was
`f2926bdfa435632b5dd7e53d8ecffd201c1a4784f78218107eadda972e3b8b53`.
The executed C source SHA-256 was
`d0c7919d2aebda697aaef0d87a83fe1ef6520c4159f83b55af2544d26e12217e`.
The saved logs also pass the subsequently strengthened API/metric consistency
checks in the current runner. This establishes real font opening and rasterization
for the dependency fix; it does not establish browser font selection or a match
between FreeType and native CoreText rendering.
