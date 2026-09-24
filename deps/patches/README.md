# FreeType resource attributes on Tiger

`freetype-resource-attributes.patch` fixes FreeType 2.13.3's resource-fork reader.
The four-byte field combines eight resource attribute bits and a 24-bit unsigned
data offset. `FT_Raccess_Get_DataOffsets` rejected a signed-negative combined
value before masking off the attributes. Tiger's installed Courier New suitcase
uses attributes `0xE0`, so all four valid faces were rejected.

The patch removes that premature signed check and preserves the existing low
24-bit mask and subsequent stream bounds checks. It changes no font payload,
font fallback, hinting, or rasterization policy.

`deps/build-deps-x86_64.sh` applies it before building FreeType. During an idle
build window, `deps/build-freetype64.sh` rebuilds only FreeType with the same
Tiger/Core 2 target flags. Neither recipe should run while the Mini is building:
installed sysroots are frozen inputs for the complete build lease.

Run `python3 spike/font-open/run-probe.py` for the target regression. It compares
the installed patched archive with the saved pre-patch archive, opening actual
Tiger fonts on the target; it does not copy or redistribute font files. Font
container order is a separate issue, documented in `spike/fontmanifest.md`.
