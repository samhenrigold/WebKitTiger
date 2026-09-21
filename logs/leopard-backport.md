# Leopard 10.5.8 i386 frameworks on Tiger 10.4.11: feasibility

Verdict up front: **none of the candidates are usable.** The Apple TV 3.0.2 QuartzCore remains
the only foreign binary we can run. Tested live on `tiger` (10.4.11 / 8S2167), not inferred.

CoreText is the interesting case and gets the long treatment below. It **does load**, once five
non-lazy imports are supplied, and its Leopard-only exports are callable. It then cannot realize
a single font, because CoreFoundation 476's object layout is compiled inline into it.

## Where the binaries came from

`refs/MacOSXUpdCombo10.5.8.pkg` (805 MB, already downloaded by the audit agent) is a flat xar
package with a single gzip-cpio `Payload` (2.4 GB expanded). Extraction:

```bash
xar -xf MacOSXUpdCombo10.5.8.pkg Payload
gzip -dc Payload | cpio -idm './System/Library/Frameworks/...' './usr/lib/libobjc.A.dylib'
```

The file list is kept at `refs/leopard/payload-list.txt`; the `Payload` itself was deleted after
extraction since the `.pkg` reproduces it. i386-thinned keepers are in `refs/leopard/keep/`.

**CoreVideo and QTKit are not in this package.** A combo updater only ships changed files, and
those two came from QuickTime updates, so 10.5.8 left them untouched. They cannot be analysed
from this source. Per the Apple TV report both are at Tiger's own version anyway (CoreVideo
1.4.1, QTKit 1.0.0), and an ATV CoreVideo 1.5.0 is already extracted at
`atv/extracted/3.0.2/CoreVideo.framework`. No further effort spent on them.

## Results

Unresolved counts are undefined symbols that match nothing in any Mach-O under `sysroot/usr/lib`,
`sysroot/System/Library/Frameworks` or `.../PrivateFrameworks` (163,188 Tiger exports total),
minus the candidate's own exports. "hard" excludes the `$UNIX2003` / `$DARWIN_EXTSN`
conformance aliases, which are renameable (see below).

| Framework | Leopard version | Deps satisfied? | Unresolved (hard) | Load test on 10.4.11 | Verdict |
|---|---|---|---|---|---|
| CoreText | 110.5.0 | yes, all compat versions met | 39 (5 non-lazy) | **loads** with a 39-symbol shim, then crashes in `_CTFontEnsureFontRef` | no |
| ImageIO | 1.0.0 | yes | 18 (7) | **fails**, non-lazy `_kCGColorSpaceGenericRGBLinear` | no |
| QuartzCore | 1.5.8 | **no**, `/usr/lib/libffi.dylib` absent on Tiger | 68 (61) | **fails**, `unknown required load command 0x8000001F` | no |
| LaunchServices | 291.0.0 | yes | 60 (51) | not attempted, 51 hard privates | no |
| CFNetwork | 438.14.0 | yes | 37 (26) | not attempted | no |
| Security | 36371.0.0 | yes | 53 (37) | not attempted | no |
| libobjc.A | 227.0.0 (objc4-371) | yes | 17 (11) | **fails at init**, `_dyld_register_image_state_change_handler` | no |
| CoreVideo | n/a | — | — | not in the combo payload | n/a |
| QTKit | n/a | — | — | not in the combo payload | n/a |

Load commands are not the usual problem. Every Leopard binary here is still classic-format: no
`LC_DYLD_INFO`, no `@rpath`, no `LC_LOAD_UPWARD_DYLIB`. They carry `LC_SEGMENT_SPLIT_INFO`
(0x1e) and `LC_CODE_SIGNATURE` (0x1d), neither of which sets `LC_REQ_DYLD`, so dyld-46 ignores
both. Only QuartzCore trips the check, with two `LC_REEXPORT_DYLIB` (0x8000001f).

### Exact failures observed

```
dlopen(/tmp/leopard/LeopardCoreText, 5): Symbol not found: _kLSItemQuarantineProperties
  Expected in: /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices

dlopen(/tmp/leopard/LeopardImageIO, 5): Symbol not found: _kCGColorSpaceGenericRGBLinear
  Expected in: .../CoreGraphics.framework/Versions/A/CoreGraphics

dlopen(/tmp/leopard/LeopardQuartzCore, 6): no suitable image found.  Did find:
	/tmp/leopard/LeopardQuartzCore: unknown required load command 0x8000001F

dyld: Symbol not found: _dyld_register_image_state_change_handler
  Referenced from: /tmp/leopard/objcdir/libobjc.A.dylib
  Expected in: /usr/lib/libSystem.B.dylib
```

Those first two failures name *data constants*, which are non-lazy, so the image never finishes
loading and `RTLD_LAZY` behaves identically to `RTLD_NOW`. But the count of such symbols is small,
and that turns out to matter: see the CoreText section, where supplying five of them is enough to
make the image load.

## CoreText: how much of the WebCore gap it would close

Leopard CoreText exports 268 symbols against Tiger's 243. Diffed against
`logs/api/missing-CT.txt`, it provides **17 of the 66** functions WebCore needs and Tiger lacks:

```
CTFontCopyAvailableTables            CTFontDescriptorCreateCopyWithAttributes
CTFontCopyFullName                   CTFontDescriptorCreateCopyWithFeature
CTFontCopyGraphicsFont               CTFontDescriptorCreateMatchingFontDescriptor
CTFontCreateForCharacters            CTFontDescriptorCreateMatchingFontDescriptors
CTFontCreatePathForGlyph             CTFontDescriptorCreateWithAttributesAndOptions
CTFontCreateUIFontForLanguage        CTFontGetGlyphCount
CTFrameGetLineOrigins                CTFontGetLigatureCaretPositions
CTFramesetterSuggestFrameSizeWithConstraints
CTLineGetTrailingWhitespaceWidth     CTFontGetVerticalTranslationsForGlyphs
```

The remaining 49 stay missing. Critically, **the whole `CTFontManager*` family is absent** —
`CTFontManagerCreateFontDescriptorFromData` and friends arrived in 10.6, so the `@font-face`
web-font path is not what this buys. Also absent: `CTFontDrawGlyphs`, `CTFontShapeGlyphs`,
`CTFontHasTable`, the `CTRun*Span` accessors, and everything text-style related.

So the ceiling is 17 functions, none of them the ones that hurt most.

### Making it load: only five symbols are non-lazy

The 39 unresolved symbols are not equal. Classifying them through the indirect symbol table
(`__IMPORT,__pointers` is non-lazy, `__IMPORT,__jump_table` is lazy) gives **5 non-lazy and 34
lazy**. Only the five block the load:

```
___CFRuntimeClassTableSize                  _kCGColorBlack
_kATSAutoActivationConfirmDontShowAgainKey  _kLSItemQuarantineProperties
_kATSAutoActivationConfirmResultKey
```

Four are `CFStringRef` constants on paths no text layout touches. The fifth is an `int`.

Getting them supplied needs care, because two-level namespace binds each import to a named
dylib. Two approaches were tried:

- **Clearing `MH_TWOLEVEL`** makes the image flat, and it appears to load. It is a trap. Tiger's
  own CoreText is already in every GUI process via ApplicationServices, so `dlsym` on the flat
  handle returns *Tiger's* addresses. `dladdr` showed every symbol resolving into
  `/System/Library/.../CoreText`, and `CTFontGetSize` returned 0 because Tiger's private CT takes
  `double` where the 10.5 prototype says `CGFloat`. This exactly reproduces ctcompat's ABI
  finding, and it means a flat-namespace result proves nothing. **Also note the install name must
  be changed first**: a `dlopen` of a binary whose `LC_ID_DYLIB` matches an already-loaded image
  returns the existing image and looks like success.
- **Keeping two-level and repointing the imports** works. The binary has 2240 bytes of header
  padding, so an `LC_LOAD_DYLIB` for a shim can be appended, `ncmds` and `sizeofcmds` bumped, and
  each of the 39 undefined symbols' library ordinal rewritten in the high byte of `n_desc` to the
  new ordinal. With a private install name, `dlopen` then returns a genuinely separate image and
  `dladdr` confirms calls land in `/tmp/leopard/LeopardCT`.

With a shim defining all 39, **Leopard CoreText loads on 10.4.11 and all its Leopard-only exports
resolve**, including the 17 above.

Three of the 34 lazy CoreGraphics imports map cleanly onto older Tiger entry points, which is
worth recording on its own:

| Leopard CoreText wants | Tiger 10.4.11 already has |
|---|---|
| `CGFontGetGlyphAdvancesForStyle` | `CGFontGetGlyphTransformedAdvances` |
| `CGFontGetGlyphBBoxesForStyle` | `CGFontGetGlyphTransformedBBoxes` |
| `CGFontGetGlyphBBoxes` | `CGFontGetGlyphBoundingBoxes` |
| `CGContextShowGlyphsAtPositions` | `CGContextShowGlyphsWithAdvances` (positions differenced to advances) |

### Why it is still unusable: CoreFoundation's object layout is inlined into it

`CTFontCreateWithName` succeeds. The first accessor called on the result dies:

```
#0  0x90a594c7 in objc_msgSend ()
#1  0x0020a006 in _CTFontEnsureFontRef ()
#2  0x0020a054 in CTFontGetSize ()
#3  0x000029c6 in main ()
```

Disassembling `_CTFontEnsureFontRef` shows what it does. It reads the object's first word,
compares it against a table entry and then against `0xfff`, and if both tests say "not a native
CTFont" it registers the selector `ctFontRef` and sends it to the object:

```
0x209fcb  je     +136                       ; native CTFont, done
0x209fcd  cmp    $0xfff,%ecx
0x209fd3  jbe    +136
0x209fe8  call   dyld_stub_sel_registerName  ; "ctFontRef"
0x20a000  call   *0x5c2ec(%ebx)              ; objc_msgSend(self, ctFontRef)
```

The selector string was read out of the binary under gdb; the receiver is the `CTFontRef`
CoreText itself had just returned. On Tiger the comparison fails, so CoreText decides its own
object is a toll-free-bridged `NSFont`, messages it, and `objc_msgSend` jumps to address 0.

This is CoreFoundation 476's toll-free-bridge object layout, compiled inline into CoreText.
Tiger's CoreFoundation 368 lays CF objects out differently, so every object CoreText creates
against Tiger's CF looks foreign to CoreText. It is not one entry point to patch: the test is
inlined into every function that touches a CT object, and the only real fix is Leopard's
CoreFoundation, which cannot be swapped for the reasons in the last section.

**This is not path-specific.** `CTFontCreateWithGraphicsFont`, reached through
`ATSFontFindFromName` and `CGFontCreateWithPlatformFont`, hits the identical crash. The font
backend is unreachable by any route.

Two incidental findings from the same runs:

- Leopard CoreText calls the Leopard ATS FontObject privates during descriptor creation.
  `FOCopyVariationInfo` returning anything non-NULL crashes it inside `CFDictionarySetValue`
  (`TBaseFont::ProcessVariationsData`), so those five `FO*` entry points would need real
  implementations as well, not stubs.
- On Tiger, `CGFontCreateWithDataProvider` returns NULL for both `.ttf` and `.dfont`, and
  `CGFontCreateWithName` returns NULL. `ATSFontFindFromName` plus `CGFontCreateWithPlatformFont`
  is the only working route to a `CGFontRef` on this system.

### What the remaining 34 would have cost anyway

Beyond the five, the lazy imports are genuine Leopard-era additions in four groups: two
CoreFoundation 476 internals, seven Leopard CoreGraphics 409 privates (four of which map to older
Tiger names, above), 18 Leopard ATS 238 privates including the `FO*` font-object set, and the
ICU 36 UText API that Tiger's libicucore 32.0.0 predates. Even with all of them implemented, the
ceiling remains the 17 functions listed earlier, and the CoreFoundation layout problem is
unaffected.

There is also a standing hazard if it ever did work: Tiger's own CoreText is already loaded by
ApplicationServices in every GUI process, so two CoreTexts would coexist, registering duplicate
CF type IDs, with a `CTFontRef` from one invalid in the other.

**Conclusion for the ctcompat track: stay on Tiger's private CoreText and keep shimming it.** The
17 functions Leopard adds are confirmed genuinely absent from Tiger rather than privately renamed,
so they do need writing. `CTFontShapeGlyphs` is not in Leopard either, so there is no binary of
this vintage to copy its behaviour from.

## libobjc: the specific reason it dies

Leopard's objc4-371 does implement the ObjC 2 API on the same fragile i386 ABI, and dyld does
prefer it by leaf name under `DYLD_LIBRARY_PATH`. The override takes effect; the process then
dies during libobjc's own initialization.

Its 17 unresolved symbols:

- `_dyld_register_image_state_change_handler` — **the blocker.** objc4-371 uses it in `_objc_init`
  to learn about loaded images and register classes. Tiger's dyld-46 does not export it, and
  Tiger's `_dyld_register_func_for_add_image` has a different contract, so no rename or alias
  fixes it. It is called before anything else, so nothing gets far enough to matter.
- Six `$UNIX2003` aliases (`_open`, `_close`, `_write`, `_fsync`, `_fcntl`, `_pthread_cond_init`)
  — renameable, see below.
- `_OSAtomicCompareAndSwapPtr{,Barrier}`, `_OSAtomicCompareAndSwapLong` — Leopard libSystem
  additions, but on i386 they are ABI-identical to Tiger's `_OSAtomicCompareAndSwap32{,Barrier}`,
  so a string-table rename would work.
- Seven `auto_*` symbols — libauto GC only, never reached with GC off.

Baseline confirmed on the box first: with Tiger's libobjc, `NSString`/`NSArray` work and
`objc_setAssociatedObject`, `class_addMethod`, `objc_allocateClassPair`, `object_getClass`,
`method_exchangeImplementations` and `objc_msgSendSuper2` are all absent, as expected. With the
Leopard override the process never reaches `main`. **`compat/objc2compat.m` stays the only path.**

Worth adding: even past the dyld symbol, Tiger's CoreFoundation 368, Foundation 567 and AppKit 824
were built against objc4-227 and reach into runtime internals. Running them on a 371 runtime would
be a second, larger gamble. This lead is closed.

## QuartzCore: strictly worse than what we already have

Two independent blockers. `LC_REEXPORT_DYLIB` (on libobjc and CoreVideo) is a Leopard load
command with `LC_REQ_DYLD` set, so dyld-46 refuses the image outright — the error is quoted
above. And it links `/usr/lib/libffi.dylib`, which does not exist on Tiger at all. Of its 61 hard
unresolved symbols, 12 are `ffi_*`, 12 are ObjC 2 runtime calls (`class_addMethod`,
`object_getClass`, `property_getName`, ...) that Tiger's libobjc lacks, and the rest are Leopard
CoreGraphics and `CGS*` layer-context privates.

There is also nothing to gain. Leopard's QuartzCore 1.5.8 has 45 CA classes to the Apple TV
1.6.0's 43, but the difference runs the wrong way for us:

| Only in Leopard 1.5.8 | Only in ATV 1.6.0 |
|---|---|
| `CAPDFLayer`, `CAScriptContext`, 13 `CAJS*` scripting-bridge classes | `CAShapeLayer`, `CAGradientLayer`, `CALayerHost`, `CAValueFunction`, `CAReplicatorLayer`, `CALayerArray`, `CAMatchMoveAnimation`, `CARenderObject`, `CAMLWriter`, the `CACGPath*` coding proxies |

Every class WebCore's `platform/graphics/ca` actually messages is in the ATV build and several are
missing from Leopard's. **Keep using `atv/extracted/3.0.2/QuartzCore.framework`.**

## ImageIO, LaunchServices, CFNetwork, Security

**ImageIO** is the closest to viable — only 7 hard unresolved symbols, all Leopard CoreGraphics
additions (`CGColorSpaceCreateWithICCProfile`, `CGColorSpaceGetModel`,
`CGDataProviderCreateWithFaultDataCallback`, `CGDataProviderUngetBytePtr`, `kCGColorSpaceSRGB`,
`kCGColorSpaceGenericRGBLinear`, plus `FSGetVolumeParms`). But it buys nothing. Tiger's ImageIO
exports 698 symbols to Leopard's 376, and everything Leopard adds is camera-RAW metadata keys
(`kCGImagePropertyMakerNikon*`, `kCGImagePropertySourceCIFF*`, DNG) and `CGImagePlugin*` /
`CGImageReadSession*` codec-plugin privates. The `CGImageSource` / `CGImageDestination` API
WebCore decodes images through is the same in both. No reason to pursue it.

**LaunchServices** needs 51 hard privates that are pure Leopard system plumbing: the entire
`_qtn_*` file-quarantine API (24 symbols, a Leopard kernel/libsystem feature), the `_SFL*`
shared-file-list API (11), `_FSPathSetQuarantineData` and friends, `_spawn_via_launchd`,
`_sqlite3_prepare_v2` (Tiger ships SQLite 3.1, which predates it). Not loadable, and WebKit needs
almost nothing from it.

**CFNetwork** repeats the Apple TV finding with a different symbol set: it needs the `CFError`
API (`CFErrorCreate`, `kCFErrorDomainPOSIX`, ...), which arrived in CoreFoundation 476 and is
absent from Tiger's 368, plus three `__CFSocket*` privates and four Leopard `SSL*`/`Sec*` entry
points. Low priority anyway — curl 8.14 with TLS 1.2 and SNI already works on the box.

**Security** needs 37 hard symbols: the whole CommonCrypto surface (`CCCrypt`, `CCHmac`,
`CC_SHA1`, `CC_MD5`, the `aes_*` and `osDes*` primitives — CommonCrypto became public in 10.5),
`CFErrorCreate` again, `_proc_pidpath` and `_csops`. Not loadable.

## The one general lever, for the record

Most of these binaries were compiled against the 10.5 SDK's UNIX 2003 conformance variants, so
they import `_open$UNIX2003`, `_write$UNIX2003`, `_realpath$DARWIN_EXTSN` and so on. Tiger's
libSystem 88.3.9 has none of those aliases, only the plain names. Because Mach-O string-table
names are null-terminated at an offset, writing a `\0` over the `$` renames the import in place
without moving anything — `_open$UNIX2003` becomes `_open`, which Tiger exports. The same trick
maps `_OSAtomicCompareAndSwapPtr` onto Tiger's ABI-identical `_OSAtomicCompareAndSwap32`.

That would clear 6 to 16 symbols per framework. It is not enough for any candidate here: the
hard residue is 7 to 61 symbols in every case, and it is always the genuinely new Leopard API
that matters. Noting it because it is the right tool if a *smaller* Leopard binary ever looks
worth porting.

A second lever exists for two-level-namespace data constants: a symbol's library ordinal lives in
`n_desc`, so it can be repointed at a different `LC_LOAD_DYLIB` entry — a shim dylib supplying,
say, `kCGColorSpaceSRGB`. Fiddly but real. Again, not enough on its own for anything above.

## Why CoreGraphics, CoreFoundation, Foundation and AppKit are not candidates at all

Not a symbol question. Each is loaded into every GUI process before our code runs, under a fixed
absolute install name:

| Framework | Install name Tiger loads |
|---|---|
| CoreFoundation | `/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation` |
| Foundation | `/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation` |
| AppKit | `/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit` |
| CoreGraphics | `.../ApplicationServices.framework/Versions/A/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics` |

dyld resolves a two-level import by the exact install name recorded in the importing binary, so a
private copy at another path is a *second* image, not a replacement: two CFRuntime class tables,
two sets of `NSString` and `CGContext` class and type identifiers, objects from one invalid in the
other. `DYLD_FRAMEWORK_PATH` could force the swap, but then AppKit 824.44 and CoreGraphics 258.77
would be running against a CoreFoundation they were not built for, and Leopard's own versions pull
in Leopard dyld and libSystem features that 10.4.11 lacks. The only correct swap is the whole OS,
which is not what we are doing. This is the same conclusion the Apple TV track reached about its
CoreGraphics, for the same reason.

libobjc is the only exception, and only because dyld resolves `/usr/lib/libobjc.A.dylib` by leaf
name under `DYLD_LIBRARY_PATH`. That is why it got a real test rather than an argument. It still
fails.

## Kept

`refs/leopard/keep/`, 58 MB, all i386-thinned:

| File | Why |
|---|---|
| `CoreText-10.5.8.i386` | reference for shimming the 17 reachable CT functions, per the "match Apple's implementation, by disassembly if needed" rule |
| `CoreGraphics-10.5.8.i386` | reference: the CG glyph and color entry points Tiger lacks |
| `CoreFoundation-10.5.8.i386`, `Foundation-10.5.8.i386`, `AppKit-10.5.8.i386` | reference for Leopard-only behaviour we reimplement |
| `libobjc.A-10.5.8.i386` | objc4-371 binary, cross-check for `compat/objc2compat.m` |
| `ImageIO-10.5.8.i386`, `QuartzCore-10.5.8.i386` | completeness; neither is usable |
| `CoreTextFull.bridgesupport` | Leopard's own machine-readable CT signatures, useful for header work |

`refs/leopard/payload-list.txt` is the full 60,469-entry file list of the combo payload, so any
other Leopard binary can be pulled from the `.pkg` without re-listing it.

`refs/leopard/tools/` holds the reusable pieces: `repoint-imports.py`, which appends an
`LC_LOAD_DYLIB` into a classic Mach-O's header padding and rewrites the library ordinals of chosen
undefined symbols, plus the CoreText shim and the two test programs. The patcher is the generally
useful one for any future foreign binary.

The audit agent's six loose `refs/leopard/*.i386` binaries duplicate several of these; left in
place rather than deleted, since that track may still be using them.

## Reproducing

```bash
cd /Users/shg/Developer/WebKitTiger/refs/leopard
xar -xf ../MacOSXUpdCombo10.5.8.pkg Payload
gzip -dc Payload | cpio -idm './System/Library/Frameworks/*' './usr/lib/libobjc.A.dylib'

# repoint the unresolved imports at a shim, give it a private install name, load on the box
python3 refs/leopard/tools/repoint-imports.py refs/leopard/keep/CoreText-10.5.8.i386 \
        /tmp/CTpatched <symbol-list> /tmp/leopard/ctshim.dylib
toolchain/bin/tiger-install_name_tool -id /tmp/leopard/LeopardCT /tmp/CTpatched
toolchain/bin/tiger-clang -dynamiclib -o ctshim.dylib refs/leopard/tools/ct-shim.c \
        -install_name /tmp/leopard/ctshim.dylib -compatibility_version 1.0.0 \
        -current_version 1.0.0 -framework ApplicationServices -ltigercompat
scp -O /tmp/CTpatched ctshim.dylib tiger:/tmp/leopard/
ssh tiger 'cd /tmp/leopard && printf "run\nbt 12\nquit\n" | gdb -q ./cttest'

# libobjc override
ssh tiger 'DYLD_LIBRARY_PATH=/tmp/leopard/objcdir /tmp/leopard/objctest'
```
