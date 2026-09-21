# SDK overlay

Headers that sit in front of `sdk/MacOSX10.4u.sdk`, laid out the way the SDK
lays them out. `toolchain/tiger.cmake` puts `usr/include` on `-isystem` and this
directory on `-F`, both ahead of the SDK, so the copies here win.

The 10.4u SDK itself is never edited.

## How a framework overlay works

A framework binds by name. Once clang resolves `Foundation` to this directory,
every `<Foundation/*.h>` must be found here; it does **not** fall through to the
next `-F`. So each overlaid framework needs a complete `Headers` directory.

`make-overlay.sh` builds those directories as symlinks into the SDK, and never
touches a real file. So:

- **symlink** = untouched, tracks the SDK
- **real file** = ours, listed below with the reason

Re-run `./make-overlay.sh` after the SDK changes or a new framework is overlaid.

## usr/include

| File | Why |
|---|---|
| `AvailabilityVersions.h` | Copied verbatim from the Xcode 27 macOS SDK. Self-contained, no includes. Gives every `__MAC_xx` / `__IPHONE_xx` constant its real value |
| `Availability.h` | Ours. A 10.5 header the 10.4u SDK never had. Includes the two above, pins `__MAC_OS_X_VERSION_MIN_REQUIRED` at 1040, and defines the availability *attribute* macros as no-ops |
| `os/availability.h` | Ours. A 10.10 header. The `API_*` attribute spellings, also no-ops |

**Why the attributes are no-ops.** Marking a declaration
`__attribute__((availability(macos,introduced=13.0)))` at a 10.4 deployment
target turns every call to it into a hard error, and WebKit's headers are full
of them. Upstream WebKit hits the same problem against any non-internal SDK and
solves it the same way: the VFS overlay in `WebKitLibraries/AvailabilityOverlay`
that `OptionsCocoa.cmake` installs replaces the SDK's availability headers with
stubs. This is that overlay for Tiger. The version *constants* are real, so
`#if __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_15` still means what it says.

## Foundation.framework/Headers

| File | Why |
|---|---|
| `NSArray.h` | Lightweight generics on `NSArray` / `NSMutableArray` |
| `NSDictionary.h` | Lightweight generics on `NSDictionary` / `NSMutableDictionary` |
| `NSSet.h` | Lightweight generics on `NSSet` / `NSMutableSet` / `NSCountedSet` |
| `NSEnumerator.h` | Lightweight generics on `NSEnumerator` |
| `NSMapTable.h` | Renames the opaque C struct to `NSMapTableCStruct` |
| `NSNetServices.h` | `__unsafe_unretained` on an `id *` instance variable |

**Generics.** The 10.4 classes are not parameterized, so `NSArray<NSString *> *`
does not parse, and a category cannot retrofit a type parameter. Only the
`@interface` lines change: the parameter is declared on the class and repeated
on each of its categories, which is what clang requires. Method signatures keep
their original `id` types — `id` converts to and from the specialized type, so
the parameter does its job at the use site without rewriting a 1990s header.

**NSNetServices.h.** `id * _reserved;` is "pointer to non-const type 'id' with
no explicit ownership" under ARC, a hard error, and the Cocoa port compiles
WebKit with ARC. It is the only `id *` instance variable in the 10.4 Foundation
headers.

**NSMapTable.** The 10.4 header does `typedef struct _NSMapTable NSMapTable;`,
which collides with the class Foundation gained in 10.5. `@interface NSMapTable`
against that typedef is "redefinition of NSMapTable as a different kind of
symbol". The typedef is renamed, the C functions are rewritten to it by a macro
that is `#undef`'d at the end of the header, and
`TIGER_NSMAPTABLE_TYPEDEF_RENAMED` tells `<TigerCompat/NSCompat.h>` it may
declare the class. The class itself lives in `compat/nscompat-maptable.m`.

## AppKit.framework/Headers

| File | Why |
|---|---|
| `NSEvent.h` | The 10.12 `NSEventType*` / `NSEventMask*` / `NSEventModifierFlag*` renames, plus `NSEventModifierFlags` and the `NSEventPhase` type |
| `NSWindow.h` | The 10.12 `NSWindowStyleMask*` renames |

**The renames.** In 10.12 Apple respelled every event type, event mask and
modifier flag without changing a single value. WebKit uses the new spelling at
189 sites across about 20 files and the old spelling at none, so the mapping is
total and mechanical. They are macros rather than new enumerators, so both
spellings name the same constant and nothing is redeclared. `NSWindowStyleMask*`
is the same story, 13 sites.

Two constants have no Tiger equivalent and get their real values, so that a test
against a Tiger event or window is simply always false: `NSEventTypePressure`
(10.10 force touch) and `NSWindowStyleMaskFullScreen` /
`NSWindowStyleMaskFullSizeContentView`.

`NSEventPhase` is a type only. Tiger's scroll events carry no phase, so the
accessors that return one live in `<TigerCompat/AppKitCompat.h>` with the rest
of the 10.7+ NSEvent methods, which need implementations rather than header
surgery.

**NSApplication.h is deliberately not overlaid.** `id *_hiddenList;` in it is
the same shape as the `NSNetServices.h` ivar below, and it did fail to parse
under ARC at one point, but it no longer does with the patched clang, in any
combination of `-fobjc-arc`, `-Xclang -fobjc-arc`, `-Wall`, ObjC and ObjC++ that
was tried. So there is nothing to fix. Those two are the only `id *` instance
variables in the whole 10.4 AppKit and Foundation header set, so if the
diagnostic ever comes back, that pair is the complete list.

## CoreFoundation.framework/Headers

| File | Why |
|---|---|
| `CFError.h` | A 10.5 header. Declares the opaque `CFErrorRef` only |
| `CoreFoundation.h` | The SDK's, plus an include of `CFError.h` |

Tiger's CoreFoundation has neither the CFError type nor its API. The type
exists here so that out-parameters spelled `CFErrorRef *` compile; every Tiger
implementation behind one writes NULL. `compat/include/TigerCompat/CTCompat.h`
defines the same typedef behind the same guard macro and can drop it now.

## CoreGraphics.framework/Headers

| File | Why |
|---|---|
| `CGBase.h` | The SDK's, plus `CGFloat` and its limits |
| `CoreGraphics.h` | Generated hook: the SDK's, plus `<TigerCompat/CGCompat.h>` |
| `CGContext.h` | Same |
| `CGGeometry.h` | Same |
| `CGPath.h` | Same |
| `CGImage.h` | Same |
| `CGColor.h` | Same |
| `CGBitmapContext.h` | Same |
| `CGColorSpace.h` | Same |
| `CGFont.h` | Same |
| `CGDataProvider.h` | Same |
| `CGAffineTransform.h` | Same |
| `CGPDFDocument.h` | Same |

**The hooks.** WebCore includes `<CoreGraphics/CoreGraphics.h>` at 52 sites and
names eleven sub-headers directly at the rest, and expects every modern CG and
ImageIO function, constant and enum to be declared. The 10.4u SDK declares none
of them. Each header above is a generated three-line file that includes the
SDK's real header and then `<TigerCompat/CGCompat.h>`. Regenerate with
`./CoreGraphics.framework/Headers/make-cg-hooks.sh`; `make-overlay.sh` leaves
them alone because they are real files.

They include the SDK header by **absolute path**, not `#include_next`.
`#include_next` does not work here: the SDK's CoreGraphics is a *subframework*
of ApplicationServices, so there is no second framework directory named
CoreGraphics to fall through to, and it fails with "file not found". The
absolute path is what the overlay's symlinks already hardcode.

Each hook appends `CGCompat.h` only at the **outermost** CoreGraphics include,
behind a `TIGER_CG_HOOK_ACTIVE` guard. CoreGraphics headers include one another,
so without the guard a unit that includes a leaf such as `CGGeometry.h` reaches
`CGCompat.h` before the rest of CoreGraphics has been emitted, and `CGCompat.h`'s
own include of `CoreGraphics.h` is then a no-op because the umbrella's include
guard is already set. `CGColorSpaceRef` ends up undeclared.

**A hook must stay as narrow as the header it wraps.** Anything `CGCompat.h`
includes is inflicted on every unit that includes *any* CoreGraphics header, so
its include list is a constraint rather than a convenience. It names the ten
specific sub-headers it needs and pulls neither umbrella:
`<CoreGraphics/CoreGraphics.h>` reaches `CGRemoteOperation`, `CGSession`,
`CGPSConverter` and `CGEvent`, and through them all of
`<CoreServices/CoreServices.h>`, which is how CarbonCore's `check` and Finder's
`Marker` collided with JavaScriptCore; `<ApplicationServices/...>` adds
QuickDraw on top. `<ImageIO/CGImageSource.h>` is out for the same reason, since
it includes the CoreGraphics umbrella itself, so `CGImageSourceRef` is
forward-declared behind that header's own include guard.

Measured with `clang -H`: each of the eleven hooked sub-headers pulls zero
CoreServices umbrella headers. `<CoreGraphics/CoreGraphics.h>` pulls four, the
same four the SDK's own umbrella pulls without any overlay.

**ImageIO is not overlaid and does not need to be.** Its `CGImageSource.h`
includes `<CoreGraphics/CoreGraphics.h>`, so `<ImageIO/ImageIO.h>` picks up the
shims through the hook.

**CGFloat has one owner: `CGBase.h` here.** `TigerCompat/CGCompat.h` keeps a
copy behind Apple's `CGFLOAT_DEFINED` guard as a fallback for consumers reached
without this overlay on `-F`, such as a hand-run spike. `compat`'s own build is
*not* one of them: its Makefile passes `-Fsdk-overlay`, and a framework-style
include from inside an absolutely-included SDK header still goes back through
the search path, so the SDK's `CoreGraphics.h` asking for
`<CoreGraphics/CGBase.h>` lands here (verified with `clang -H`). Same guard,
same definition, so whichever is reached first wins and they cannot collide.

`CGFloat` arrived in the 10.5 SDK. On i386 CoreGraphics is float-based
throughout, so `typedef float CGFloat` makes a modern prototype ABI-identical to
what Tiger exports. `compat/include/TigerCompat/CGCompat.h` defines the same
thing behind the same guard macro and can drop it now.

Tiger's CoreText is the exception and does *not* follow CGBase: every by-value
scalar it takes is a `double`. See `compat/CT-SURVEY.md`. CoreText headers are
deliberately **not** overlaid from the 10.5 SDK for that reason; use
`<TigerCompat/CTCompat.h>`.

## Not overlaid, and why

- **CoreText.** The 10.5 prototypes are ABI-wrong against Tiger's binary.
- **Foundation runtime gaps** (`NSUUID`, `firstObject`, subscripting, fast
  enumeration, ...). Those are declarations plus implementations, not header
  surgery; they live in `compat/include/TigerCompat/NSCompat.h` and
  `compat/nscompat*.m`.
- **Headers the SDK simply never had** and that need no SDK content
  (`execinfo.h`, `mach/vm_page_size.h`, ...). Those live in
  `compat/include/sdk-fill`, which is reached through its own `-isystem`.

## CoreText.framework/Headers

Owned by the ctcompat track. **Every file here is ours**; none is a symlink,
because Tiger's CoreText ships no headers at all. The framework binary exists
inside `ApplicationServices.framework/Frameworks` and exports 243 symbols, but
Apple never shipped a public header for it until 10.5.

These declare the **modern public CoreText API**, spelled the way WebCore calls
it, bound to what the 10.4.11 binary actually does. `CTDefines.h` explains the
three kinds of declaration; `compat/CT-SURVEY.md` has the full reasoning and the
disassembly behind it. In short:

| Kind | How it is declared |
|---|---|
| Tiger's ABI already matches | normally |
| Tiger reads the size as a by-value `double` | with `double`, not `CGFloat`. The caller's float converts at the call site, so WebCore's source is unchanged |
| Tiger's differs, does nothing, or does not exist | modern prototype plus an `__asm__` label onto the adapter or shim in `libtigercompat` |

| File | Why |
|---|---|
| `CTDefines.h` | Ours. Shared types, `CGFloat`, `CFErrorRef`, and the `CT_TIGER_ADAPTER` asm-label macro. Read this first |
| `CoreText.h` | Ours. The umbrella |
| `CTFont.h` | Ours. Carries four of the ten asm-label adapters |
| `CTFontDescriptor.h`, `CTFontTraits.h`, `CTFontCollection.h` | Ours |
| `CTFontManager.h` | Ours. Tiger has no font manager at all; every function is a shim over `ATSFontActivateFromMemory` |
| `CTLine.h`, `CTRun.h` | Ours. Carry the other six adapters |
| `CTFrame.h`, `CTFramesetter.h`, `CTTypesetter.h` | Ours |
| `CTParagraphStyle.h`, `CTStringAttributes.h`, `CTTextTab.h`, `CTGlyphInfo.h` | Ours |
| `SFNTLayoutTypes.h` | Ours, one line. Modern SDKs re-export it from CoreText; on Tiger it only ever lived in ATS, so this forwards to `<ATS/SFNTLayoutTypes.h>` |

**These headers declare no SPI.** WebCore declares CoreText SPI for itself in
`PAL/pal/spi/cf/CoreTextSPI.h`, and that header and this overlay are designed to
be included together: they share no type and no enumerator. Adding an SPI
declaration here would collide with it. `libtigercompat` still *implements* the
SPI; only the declaration lives on WebCore's side.
## Linking against libtigercompat: `-Wl,-ObjC` is mandatory

Not an overlay matter, but it is discovered the same way and costs an afternoon
if missed. Almost all of `libtigercompat.a`'s Foundation surface is Objective-C
categories. A static archive member is only pulled into a link when it defines a
symbol something referenced, and a category defines no symbol, so without
`-ObjC` the member never joins the link. Nothing is said at build time; the
program dies at runtime on the first category method, as

```
*** -[NSCFArray objectAtIndexedSubscript:]: selector not recognized
```

It has to be `-Wl,-ObjC`. A bare `-ObjC` is a *compiler* flag that retargets the
source language to Objective-C and breaks any C++ in the same command.

`-ObjC` pulls in *every* member that defines a class or category, so
libtigercompat's own dependencies stop being optional. A program that links it
this way needs all of:

```
-ltigercompat -ltigerdispatch
-framework Foundation -framework AppKit -framework ApplicationServices
```

`-ltigerdispatch` because `NSOperationQueue` is built on it, and the two
frameworks because of the NSEvent, NSView, NSWindow and NSColor shims in
`compat/nscompat-appkit.m`. This is true whether or not the program itself
mentions a queue, a window or a colour. WebKit links all of these anyway; a
small test program does not, and that is where it bites.

## Verifying the overlay

`spike/overlaytest.mm` is the check: 37 assertions covering the generic
collections, `NSInteger`, `NS_ENUM` / `NS_OPTIONS`, the annotation macros,
nullability, the NSEvent and NSWindow renames, `@available` being false for
10.12 and true for 10.4, `CGFloat`, `CFErrorRef`, and `NSMapTable` as both the
class and the C struct. `spike/run-overlaytest.sh` builds it in MRR and ARC with
`-Wall`, copies it to the Tiger box and runs it, and is the reference for the
flag order.

