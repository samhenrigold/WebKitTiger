# Native and FreeType font identity on Tiger

`fontmanifest.c` records two distinct indices. `faceIndex` remains the index from
ATS local activation, used unchanged by `TigerCTFontForHandle` and native glyph
rasterization. `freeTypeIndex` is the face position in FreeType's container order:
the TTC offset array or the unsorted list of `sfnt` resources. FreeType 2.13.3's
`src/base/ftobjs.c:IsMacResource` explicitly requests `sort_by_res_id=FALSE` for
sfnt resources. Resource attribute bits are masked out of the 24-bit data offset.

For each native face, the generator asks public Tiger `ATSFontGetTable` for the
complete `head`, `hhea`, and `maxp` tables. Exactly one container face must match
every byte and length of all three. Missing tables, invalid containers and
ambiguous matches produce `freeTypeIndex: -1`; the engine can exclude them rather
than select a different face. Metrics are read from the matched FreeType face,
not from the native index. Resource lengths bound individual sfnt reads, and a
malformed resource is never skipped in a way that renumbers later faces.

Do not use PostScript names or the `name` table for this mapping. ATS synthesizes
PostScript aliases for fonts such as Stone Sans, and rewrites/expands the raw
`name` table for some other fonts. The actual diagnostic run
`logs/probes/20260923-211319-font-manifest-5eae6b9060/app.log` records Courier New
Regular's ATS name table at 9446 bytes versus the file's 5762 bytes. Its complete
head/maxp tables nevertheless match resource 3 exactly. The other three styles
show the same behavior. Stone Sans's name bytes happen to remain unchanged, so
testing that family alone would miss this issue.

Run `python3 tools/regenerate-font-manifest.py` to cross-build and generate a new
manifest under a unique `logs/probes` directory. It freezes source, binary,
native baseline and supervisor; records hashes; acquires the existing local and
Tiger leases; and cleans up only its own process group. `--build-only` contacts
no device. The helper validates resolution counts, runs the native handle
self-check, and compares native indices with the previous manifest. It does not
replace `logs/tiger-fonts.json` or any installed resource. Review its result and
coordinate the engine's `freeTypeIndex` consumer before promoting the manifest.

The successful target run
`logs/probes/20260923-211432-font-manifest-486916ff21/` contains the candidate
`fonts.json`, diagnostics, result summary, and provenance. All 176 native indices
are unchanged; all 174 previously supported native handles still resolve to
their named faces. All 174 now have a unique structural-table match. The same two
unsupported multiple-master fonts, HelveticaLTMM and TimesLTMM, remain -1.
Fifty-one native and FreeType indices differ. Courier New maps as follows:

| Face | Native faceIndex | freeTypeIndex |
| --- | ---: | ---: |
| Regular | 0 | 3 |
| Bold | 1 | 2 |
| Italic | 2 | 1 |
| Bold Italic | 3 | 0 |

The three Stone Sans faces retain indices 0, 1, and 2. Candidate manifest SHA-256:
`8b9399998179befbc2e0dfb258de4689b3666a02a64fa61fd74ab5eb0e6aa08e`.

`python3 tools/test_font_manifest.py -v` runs ten host tests, compiling the actual
container/matcher/metric functions under AddressSanitizer and UndefinedBehaviorSanitizer.
They cover reversed resource order, high resource attribute bits, TTC absolute
offsets, rewritten names, each required identity table, ambiguity, malformed
containers, matched metrics, and manifest validation/native-index preservation.
The device run, rather than these host fakes, establishes ATS table semantics.
