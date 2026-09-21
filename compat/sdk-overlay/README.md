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
| `NSApplication.h` | `__unsafe_unretained` on an `id *` instance variable |

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

**NSApplication.h.** `id *_hiddenList;` is the same ARC error as
`NSNetServices.h` below. Those two are the only `id *` instance variables in the
whole 10.4 AppKit and Foundation header set.

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

`CGCompat.h` deliberately does **not** include
`<ApplicationServices/ApplicationServices.h>`. Reaching it from inside
`CoreGraphics.h` would drag QuickDraw's `Rect` and `Point` into every WebCore
translation unit. It includes `<CoreGraphics/CoreGraphics.h>` and
`<ImageIO/CGImageSource.h>` instead, which needs the ApplicationServices
subframework directory on `-F`; `tiger.cmake` and `compat/Makefile` both pass it.

**ImageIO is not overlaid and does not need to be.** Its `CGImageSource.h`
includes `<CoreGraphics/CoreGraphics.h>`, so `<ImageIO/ImageIO.h>` picks up the
shims through the hook. Its `CGImageSourceRef` typedef sits above that include,
so the type is declared before `CGCompat.h` runs in either include order.

**CGFloat has one owner: `CGBase.h` here.** `TigerCompat/CGCompat.h` keeps a
copy behind Apple's `CGFLOAT_DEFINED` guard purely as a fallback for builds that
do not put this overlay on `-F`, which is how `compat` itself builds. Same guard,
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

`-ObjC` also pulls in `NSOperationQueue`, which is built on libtigerdispatch, so
`-ltigerdispatch` becomes mandatory alongside `-ltigercompat` whether or not the
program uses a queue.

## Verifying the overlay

`spike/overlaytest.mm` is the check: 35 assertions covering the generic
collections, `NSInteger`, `NS_ENUM` / `NS_OPTIONS`, the annotation macros,
nullability, the NSEvent and NSWindow renames, `@available` being false for
10.12 and true for 10.4, `CGFloat`, `CFErrorRef`, and `NSMapTable` as both the
class and the C struct. `spike/run-overlaytest.sh` builds it in MRR and ARC with
`-Wall`, copies it to the Tiger box and runs it, and is the reference for the
flag order.
