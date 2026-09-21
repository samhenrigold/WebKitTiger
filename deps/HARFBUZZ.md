# HarfBuzz on Tiger: OpenType shaping proof

`deps/spike-tests/test_harfbuzz.c`, cross-compiled with tiger-clang++, scp'd to the Tiger
box along with `spike/ctprobe-data/DejaVuSans.ttf` (the same TTF `spike/ctprobe.c` uses; it
carries a real `fi` GSUB ligature and Arabic initial/medial/final substitution), run over ssh.

`hb_shape` is called with no explicit feature list, so HarfBuzz's own defaults apply
(`liga` and Arabic joining are on by default):

```
fi: 1 glyphs: 5039
  -> ligated into a single glyph
arabic joined (beh-seen-meem): 3 glyphs: 5337 5291 5256
arabic isolated: beh=1365(1) seen=1376(1) meem=1389(1)
  compare: beh  isolated=1365 vs joined(initial)=5337  (differs)
  compare: seen isolated=1376 vs joined(medial)=5291  (differs)
  compare: meem isolated=1389 vs joined(final)=5256  (differs)
```

- Latin "fi" collapses from 2 input characters to **1 output glyph** -- the GSUB `liga`
  ligature substitution ran.
- Each Arabic letter shaped alone (isolated form) gets a different glyph ID than the same
  letter shaped as part of the joined run (initial/medial/final contextual form) -- GSUB's
  Arabic joining lookups ran, not just a 1:1 cmap lookup.

(An earlier pass used SF Pro / SF Arabic and saw "fi" come out as 2 glyphs -- not a
HarfBuzz bug, that font just doesn't carry an `fi` GSUB ligature. DejaVu Sans does, so it's
the sharper proof.)
