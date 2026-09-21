# WebKitTiger working notes (shared by all agents)

Goal: modern WebKit (WebKitLegacy/WebKit1 flavor of the Cocoa port) cross-compiled from this Apple Silicon Mac
for Intel Mac OS X 10.4.11 (i386, fragile ObjC runtime, no JIT/C-loop JSC, no CoreAnimation).

## Layout
- `toolchain/env.sh`      source in **bash** (not zsh: zsh doesn't word-split $VARS). Defines tcc/tcxx/tobjc/tnm/tar_.
- `toolchain/bin/tiger-clang{,++}`  wrapper: clang -target i386-apple-macosx10.4 -isysroot 10.4u SDK, --ld-path=cctools ld64,
                           -include compat/include/tigerprelude.h, -I/-L toolchain/sysroot-i386/usr. Also tiger-ar/nm/ranlib/strip/otool/lipo/libtool.
- `toolchain/cctools/`    cctools-port (ld64-956.6) built for host arm64, target i386. Apple's Xcode ld refuses i386. Its `nm`
                           also reads old Tiger binaries (Xcode's nm rejects them: obsolete load command 23).
- `toolchain/sysroot-i386/usr`  install prefix for everything we build for the target: libc++/libc++abi/libunwind (static),
                           libtigercompat.a, third-party deps. Headers in include/, libs in lib/.
- `toolchain/src/llvm-project` sparse checkout, release/21.x. Patches in toolchain/patches/.
- `sdk/MacOSX10.4u.sdk`   headers + real dylib stubs for Tiger. `sdk/MacOSX10.5.sdk` for reference (newer declarations).
- `sysroot/`              exact mirror of the Tiger box's /usr/lib, /usr/include, /System/Library/{Frameworks,PrivateFrameworks}.
- `compat/`               libtigercompat.a: libc gaps (libcompat.c), ARC entry points on the legacy runtime (arc.m),
                           ObjC2 runtime API on ObjC1 (objc2compat.m), Blocks runtime. `make -C compat install`.
- `compat/dispatch/`      libtigerdispatch.a: libdispatch (GCD) + os_log/os_unfair_lock/os_signpost/sys/qos
                           polyfill on pthreads+mach+CFRunLoop. `make -C compat/dispatch install`.
                           API surface survey in compat/dispatch/SURVEY.md. Test: spike/dispatchtest.mm.
                           **64-bit:** `make -C compat/dispatch ARCH=x86_64 install` builds os.c only
                           (os_log/os_unfair_lock/os_signpost/qos) into sysroot-x86_64; link -ltigerdispatch.
                           This is the only producer of those symbols: compat's 64-bit archive deliberately
                           does not carry os.c (2c6d1db), because two archives with os.o is what sent people
                           hand-building libtigercompat.a. dispatch.c cannot go 64-bit at all: Tiger has no
                           x86_64 CoreFoundation and the main queue needs CFRunLoop (its CF headers do not
                           even parse at -target x86_64), so a 64-bit process gets the os_* surface and no
                           queues. The 64-bit install stages os/ and sys/ but NOT dispatch/, so a 64-bit
                           `#include <dispatch/dispatch.h>` fails at the include rather than at link time.
                           Test: spike/os64test.c.
- `deps/`                 third-party sources + build-c-deps.sh. `build/`, `logs/` scratch. `spike/` test programs.
- `WebKit/`               sparse blobless checkout of WebKit main (d2f52605, 2026-09-20). This is the fork we patch.

## Target box
- `ssh tiger` (alias in ~/.ssh/config, key auth). Copy files with `scp -O`. No Xcode/nm/otool on the box; use tiger-nm here.
- Core 2 Duo T7500, 6 GB RAM, 10.4.11 build 8S2167, kernel xnu-792.25.20. Clock is correct now.

## Compiler facts established
- Apple clang 21 (Xcode 27) emits i386 fine. Link with --ld-path to cctools ld; crt1.o comes from the SDK automatically via the driver.
- ObjC: `-fobjc-runtime=macosx-fragile-10.4` for MRR. For ARC: `-Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7`
  (the driver refuses -fobjc-arc below 10.6; the cc1 flag bypasses it; runtime 10.7 makes clang call objc_retain etc.
  directly, which libtigercompat implements). Fragile ABI: no auto-synthesized ivars, no ivars in class extensions /
  @implementation blocks (clang Sema errors) -> clang patch track.
- TLS: use -femulated-tls (Tiger has no __thread). No stack protector (-fno-stack-protector). No @rpath on Tiger's dyld
  (use @executable_path/@loader_path). Link deployment target must stay 10.4 so ld emits classic (non-LC_DYLD_INFO) binaries.
  Also never strip local symbols: see LINK RULE 2 below. Verified by stripping a test dylib, which turns every
  protocol ext lookup into an empty result (no crash, no data).
- Tiger libSystem lacks: posix_memalign, strnlen, memmem, getline, pthread_setname_np, pthread_threadid_np, clock_gettime,
  arc4random_buf, __cxa_thread_atexit, os_unfair_lock, dispatch_*, fstatat/openat, __stack_chk_guard, __bzero, backtrace,
  PTHREAD_RWLOCK_INITIALIZER, __eprintf (assert). Has: xlocale (newlocale/uselocale/strtod_l), mach_vm_*, kqueue, madvise, copyfile.
- Tiger libobjc: ObjC1 API only (no object_getClass/class_addMethod/method_exchangeImplementations/objc_allocateClassPair/
  objc_setAssociatedObject/objc_retain...). Has objc_msgSend{,_stret,_fpret}, objc_getClass, sel_registerName,
  class_getInstanceMethod, objc_addClass, objc_exception_throw, objc_sync_enter.
- Full export lists: logs/api/tiger-*.txt ; WebKit call-site usage counts: logs/api/used-*.txt ; diffs: logs/api/missing-*.txt.
- Tiger's private CoreText (243 exports) covers 54 of the 120 CT functions WebCore calls, incl. CTFontCreateWithGraphicsFont,
  CTFontGetGlyphsForCharacters, CTFontGetAdvancesForGlyphs, CTLine/CTRun/CTTypesetter, CTFontCopyTable.

## Learned from the deps track (2026-09-20)
- All of sqlite, libxml2, libxslt, LibreSSL 4.1, curl 8.14 (no zlib, no IPv6), ICU 76.1 (static, with data) are installed in
  toolchain/sysroot-i386/usr. deps/build-c-deps.sh reproduces it. HTTPS GET to apple.com from the Tiger box works (TLS 1.2 + SNI).
- The 10.4u SDK's zlib is 1.2.3 (no z_const/inflateReset2). Build a newer zlib into the sysroot if anything needs it.
- `__builtin_available` / `@available` make clang call `__isPlatformVersionAtLeast` from compiler-rt's os_version_check.c,
  which needs dispatch_once and CoreFoundation and targets 10.7+. Plan: implement `__isPlatformVersionAtLeast` and
  `__isOSVersionAtLeast` in libtigercompat (compare against 10.4.11 from Gestalt/sysctl) so @available works and is false for
  anything newer than 10.4.
- Autoconf cross-config gotcha: AC_CHECK_FUNC probes fail against the SDK's prototyped declarations; pass ac_cv_func_*=yes.
- Any appended -fstack-protector* flag beats the wrapper's -fno-stack-protector; disable hardening in configure.
- tigerprelude.h is wrapped in #ifndef __ASSEMBLER__ so -include works for .S files.
- ICU with tiger-clang++ needs `-stdlib=libc++ -I$sysroot/include/c++/v1` explicitly.

## ARC / ObjC2 / C++ runtime (verified on the box, spike/{arctest.mm,objc2test.m,exctest.mm,cxxtest.cpp})
- Exact flags. ARC ObjC++: `tiger-clang++ -Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7 -fobjc-exceptions -fexceptions
  -nostdinc++ -isystem toolchain/sysroot-i386/usr/include/c++/v1 -stdlib=libc++ -lc++ -lc++abi -lunwind -ltigercompat
  build/builtins-i386/libclang_rt.builtins-i386.a -framework Foundation`. **-ltigercompat must come before the builtins
  archive** (both define `___eprintf`; first one wins, second is never pulled).
- `thread_local` needs `-target i386-apple-macosx10.7 -femulated-tls` **on the compile step only** (clang rejects
  thread_local below 10.7 even with -femulated-tls; `-mmacosx-version-min=10.7` is not enough, the triple wins).
  Link at 10.4 as usual; output still has classic load commands, no LC_DYLD_INFO. At 10.7 clang emits Darwin's
  `_tlv_atexit` (not `__cxa_thread_atexit`) for thread_local destructors -> compat/tlv.c.
- Clang on the fragile ABI emits `objc_msgSendSuper`, **not** objc_msgSendSuper2, and never objc_alloc/objc_alloc_init/
  objc_opt_* (checked at -O0 and -O2 with literals, alloc/init/new, isKindOfClass). Nothing to add there.
- Blocks are real ObjC classes now (compat/blockclasses.m): NSBlock + __NS{Stack,Global,Malloc}Block__ memcpy'd over the
  `_NSConcrete*Block` placeholders, which moved out of BlocksRuntime/data.c into the same object file so the constructor
  actually gets linked. Foundation can retain/copy/release blocks.
- ObjC EH on the fragile ABI is setjmp/longjmp and does **not** interoperate with DWARF C++ EH: a C++ throw walks past
  `@finally`, and `@throw` skips C++ destructors. Everything else (C++ throw/catch across frames and across a dylib,
  @try/@catch/@finally on their own) works.
- Exception types crossing a dylib boundary need `__attribute__((visibility("default")))` under -fvisibility=hidden,
  otherwise the typeinfo is not coalesced and the catch is missed (each image carries its own static libc++abi).
- `_dyld_find_unwind_sections` in compat/libcompat.c works: throw in a dylib / catch in the exe and vice versa both pass.
- `@available` / `__builtin_available` works: clang emits both `___isOSVersionAtLeast` and `___isPlatformVersionAtLeast`
  (compiler-rt's os_version_check.c is not in our builtins archive), implemented in compat/availability.c by reading
  ProductVersion out of /System/Library/CoreServices/SystemVersion.plist. spike/availtest.m covers it.

## libc++ filesystem (2026-09-20)
- libc++ rebuilt with LIBCXX_ENABLE_FILESYSTEM=ON. openat/unlinkat/fdopendir/fcopyfile shims live in compat/libcompat.c
  (path-based emulation via F_GETPATH; not race-free). spike/fstest.cpp passes 19 checks on Tiger.
- Exception to the "don't edit sdk/" rule: sdk/MacOSX10.4u.sdk/usr/include/copyfile.h was added (copied from the 10.5 SDK;
  Tiger's libSystem exports copyfile, the 10.4u SDK just lacked the header). Pure additions of missing headers are OK; never
  modify an existing SDK header in place, use compat/sdk-overlay/.
- compat/cfcompat.c (wkcmake's) needs dispatch/notify declarations; make sure `make -C compat` builds clean before relying on it.
- Property/protocol introspection works better than expected. Clang emits ObjC2 property metadata even on the
  fragile ABI: the class struct it lays down is **12 words**, not the 10 in <objc/objc-class.h>, with
  { ivar_layout, ext } appended and ext pointing into an `__OBJC,__class_ext` section. It never sets a CLS_EXT bit,
  so compat detects it by requiring the class to live in an image that has a __class_ext section and the ext pointer
  to land inside it. `class_copyPropertyList` / `property_getName` / `property_getAttributes` /
  `property_copyAttributeList` all return real data; on a Tiger-built class they correctly return nothing.
  Protocol optional methods and properties are reachable too, via a second route. The old ABI chained that record
  through the protocol's first word, which Tiger overwrites with the Protocol class at load, but clang also emits it
  as the local data symbol `_OBJC_PROTOCOLEXT_<Name>`. compat finds the image whose `__OBJC,__protocol` section
  contains the protocol, walks that image's LC_SYMTAB through LINKEDIT (dlsym cannot see local symbols), and caches
  per protocol. Layout is clang's `struct _objc_protocol_extension` from CGObjCMac.cpp: { uint32 size,
  optional_instance_methods, optional_class_methods, instance_properties, extendedMethodTypes, class_properties },
  24 bytes on i386, with shorter records handled by checking `size`. This is what makes JSExport wrapper building
  work, since JSExport protocols are mostly @property declarations.
  Still empty by nature: `protocol_copyPropertyList2` with isRequiredProperty NO, since the old ABI keeps no separate
  optional-property list. Note that properties declared after `@optional` also declare optional accessors, so the
  optional instance method list legitimately contains those getters and setters.
- Under ARC a bare `Protocol *` gets retain/release, and Tiger's Protocol descends from Object, so libobjc logs a
  one-time "Object compatibility method has been executed" warning. Harmless; WTF holds protocols
  `__unsafe_unretained` so it does not hit it. Our runtime.h spells the Protocol** out-params
  `__unsafe_unretained`, without which ARC rejects the declaration outright.
- **Include-path gotcha:** tiger-clang/tiger-clang++ pass `-I toolchain/sysroot-i386/usr/include` themselves, and a
  plain -I outranks any `-isystem` a caller adds. So `<objc/runtime.h>` (and anything else the sysroot has) always
  resolves to the **staged** copy, never to compat/include, no matter what the caller puts on the command line.
  Changing a compat header only takes effect after `make -C compat install`.
- The ARC ownership diagnostic ("pointer to non-const type 'X *' with no explicit ownership") fires on **return types
  and struct fields, not on parameters**, so `void f(NSString **)` is accepted while `NSString **f(void)` is an error.
  `make -C compat install` now ends with a `check` target that syntax-only-compiles compat/archeadercheck.m against the
  staged headers with ARC on, which catches exactly this.

## Core Animation for Tiger (2026-09-20, atv track) — see atv/REPORT.md
- Apple TV Software 3.0.2 (build 8N6014, Darwin 8 / 10.4.7 base) ships an i386 QuartzCore.framework 1.6.0 with a full
  Core Animation (43 CA classes, CA-prefixed, not LayerKit). Extracted at atv/extracted/3.0.2/QuartzCore.framework.
- Loads on the real 10.4.11 box with zero unresolved symbols; spike/catest.m (17 checks) and spike/carendertest.m
  (CARenderer over a CGL pbuffer, GPU-rendered pixel readback) pass.
- Constraints: Tiger AppKit has no layer-backed NSViews and the window server has no CA protocol, so the only path is a
  CARenderer inside an NSOpenGLView, driven manually ([CATransaction flush] + addUpdateRect:), like the old Windows port's
  CACFLayerTreeHost. Rename the framework's install name to a private path before bundling (collides with Tiger's own
  QuartzCore; 207 Core Image class names duplicate). Missing API is post-10.6 only (contentsScale, rasterization, etc.).
- Headers: use the 10.5 SDK's QuartzCore headers (Core Animation 1.x) via the overlay.

## Rules of thumb (2026-09-20, from the user)
- Priority order for every gap: (1) use what the system already gives us, public or private, older name or not;
  (2) if we must write it, match Apple's implementation (open source for 10.5/10.6, or disassembly of Leopard/Snow
  Leopard binaries); (3) only if neither exists, write the best, most performant version we can. Never a lazy stub
  where (1) or (2) is available.
- Private-but-exported Tiger API is fair game. The export lists in logs/api/*.txt already include private symbols (nm -g),
  and nscompat's selector lists came from the binaries' method lists. Before writing a shim, check whether Tiger already
  has the thing under a private or older name (examples: CFRunLoopGetMain, CFStringCreateWithBytesNoCopy,
  _CTFontDescriptorCopyAvailableFontFamilyNames, CTFontDescriptorCopyWithAttributes). ABI stability is not a concern.
- Cross-check shims against prior art: Apple open source for 10.6 (objc4-437 objc-runtime-old.mm, libdispatch-84,
  libclosure, CF-550, Libc-583), macports-legacy-support (github.com/macports/macports-legacy-support), PLBlocks; and for
  CoreText/CoreGraphics semantics, disassembly of Leopard/Snow Leopard binaries (combo updaters have full binaries).
- Caution on logs/api/used-*.txt counts: they include declaration lines in WebKit's own SPI headers (PAL/pal/spi/...), so a
  count of 1 may be "declared, never called". Grep for real call sites before shimming.
- Leopard backports: dead end (logs/leopard-backport.md). Tiger's own private CoreText + compat/ctcompat.c is the path.

## Compiler switch (2026-09-20 20:00): tiger-clang now runs the patched clang 21.1.8 in toolchain/llvm-tiger
- Patch: toolchain/patches/clang-fragile-ivars.patch. The wrapper passes -Xclang -fobjc-fragile-extension-ivars, so ivars in
  class extensions / @implementation blocks and property auto-synthesis work on the fragile ABI (warning silenced). Caveat:
  a subclass in another TU can't see those ivars. Ivars in named categories are still an error.
- Plain `-fobjc-arc -fobjc-runtime=macosx-fragile-10.7` now works (no -Xclang bypass). Note the driver then enables
  -fobjc-arc-exceptions for ObjC++, so ARC ObjC++ links need -lc++abi -lunwind (tiger.cmake already links them).
- Wrapper also adds -cxx-isystem <sysroot>/include/c++/v1 and -Wno-unused-command-line-argument. Old wrapper kept as
  toolchain/bin/tiger-clang.appleclang. spike/hello.mm compiles byte-identically with either compiler.
- thread_local still needs `-target i386-apple-macosx10.7 -femulated-tls` on the compile step (link stays 10.4).
- Curl WK1 backend: ResourceHandleCurl.cpp & friends were only removed 2023-03-20 (f57e6ee12e74); last-good copies (2023-03-19,
  ee329e96) are in refs/webkit-history/curl-resourcehandle/ with a README. Restoring them is closer to current WebCore APIs than
  the plan's "write one new file against CurlRequestClient" estimate assumed. Do NOT use the June-2023 stubbed version kept there.

## CA host prototype (spike/CAHost, 2026-09-20 20:00) — Core Animation on screen on the box
- NSOpenGLView + CARenderer with the rebundled Apple TV QuartzCore (install name @executable_path/../Frameworks/QuartzCore.framework/...,
  headers from the 10.5 SDK copied in by rebundle.sh). CA render 0.6 ms/frame; the swap/vsync is the whole budget.
- Requirements: #undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER before importing CA headers at a 10.4 target;
  [CATransaction flush] every frame; addUpdateRect: every frame; CGLSetCurrentContext before each frame; resize = root layer
  bounds (in a disabled-actions transaction) + renderer bounds + glViewport/glOrtho. Geometry is y-up (set geometryFlipped or convert).
- -drawLayer:inContext: delegate path exists in this CA build; CGBitmapContext -> CGImage -> setContents: works for backing stores.
- GUI apps launched over ssh get a window server connection on 10.4.11; screencapture -x works from ssh.
- Xcode 2.5 is installed on the Tiger box (2026-09-20 20:10): /usr/bin/gdb (6.3, Apple gdb-696), nm, otool, gcc 4.0.1 work
  on-device. Use `ssh tiger gdb --batch -ex run -ex bt --args /tmp/foo` for crash triage; our binaries carry DWARF when built -g.

## Housekeeping (2026-09-20 20:15)
- The project root is now a git repo (compat/, toolchain scripts & patches, spike/, logs/*.md, NOTES.md, deps scripts, atv reports).
  Big/derived trees are ignored. Commit after each finished piece of work: `git add -A && git commit -m "..."` so overwritten
  files can be recovered (six overlay availability headers were lost once already).
- Overlay ownership: wkcmake owns compat/sdk-overlay/{usr/include,Foundation.framework,CoreFoundation,CGBase/CFError} and
  make-overlay.sh; nscompat owns AppKit.framework in the overlay; ctcompat CoreText.framework; cgcompat CoreGraphics.framework.
- LINK RULE: anything linking libtigercompat.a needs `-Wl,-ObjC` (its Foundation surface is categories, which a static archive
  only pulls in with -ObjC) and `-ltigerdispatch` (NSOperationQueue depends on it). Silent failure otherwise: "selector not recognized" at runtime.
- From the shim audit (logs/shim-audit.md): Tiger runs objc4-267.1; its objc_addClass repairs hand-built classes (null cache,
  clear CLS_* flags), which is why objc_allocateClassPair works. posix_memalign is now real (mmap + malloc zone; verified 16 B–1 MB
  on the box, spike/aligntest.c). Always rebuild from clean before trusting a compat test: stale archives hid two regressions.
- Trap seen twice on this box: a Tiger export whose name AND argument list match the modern API can still behave differently
  (return false for style 0, return a fixed rect, be an empty stub). Check by disassembly, then run it.
- Baseline from spike/TigerBrowser on Tiger's own 2007 WebKit: a 2,000,000-iteration JS loop takes ~5.3 s (no JIT). Compare our
  C-loop jsc against that. HTTPS fails through Tiger's CFNetwork/SSL as expected (no SNI/TLS 1.2); curl backend fixes it.
- GUI processes launched over ssh die when that ssh session closes (nohup doesn't detach on Tiger). Do launch + sleep +
  screencapture in ONE ssh invocation, or launch via `open` and let the app outlive the session.
- ABI screen for same-name functions (from the audit): for each Tiger export WebCore calls, compare the highest stack argument the
  prologue reads (tiger-otool -tV) with the modern prototype's i386 cdecl argument size; hand-check candidates. Found CTLineDraw's
  extra CFRange. Run it on any framework we call by name. **Now automated: `tools/abi-screen.py <framework>...`**, with
  `--control` re-running the CoreText positive control (it reproduces all four hand verdicts incl. CTLineDraw). Two traps it
  encodes: count the *access width*, not the displacement (a trailing double is one `movsd 0xc(%ebp)` and scores 4 bytes short),
  and strip exactly one leading underscore off Mach-O names (`___CFRangeMake` is not `CFRangeMake`). Modern arg sizes come from
  clang's own i386 lowering, not from reading headers. CoreFoundation/ATS/LaunchServices/HIServices/Security are **done and clean**:
  206 functions, 1583 call sites, zero mismatches -> logs/abi-screen-cf.md, spike/cfabitest.c (13 checks pass on the box).
  Modern WebKit calls **no** ATS* function at all. Stronger than the disassembly heuristic: 199 of those 206 are declared by
  BOTH the 10.4u SDK and the modern SDK, and the i386 prototypes are byte-identical, so only 7 rest on disassembly at all.
  (CoreGraphics/ImageIO were screened separately by cgcompat, fd4afe3.) Tiger's malloc zone ABI is version 3 (no memalign field), so
  malloc_zone_memalign cannot be offered; posix_memalign's mmap path matches Snow Leopard's Libc step for step.
- LINK RULE 2: never strip local symbols on this port (no `strip -x`, no `-Wl,-x`): protocol ext records
  (__OBJC_PROTOCOLEXT_*) are recovered by name from the symbol table for JSExport, since Tiger's runtime discards the pointer.
- dispatch_once is now lock-free per libdispatch (a global mutex deadlocked cross-thread nested onces).
- Leopard DP1 9A241 CoreText runs on Tiger with a patched binary + runtime CF-bridge-table bootstrap (refs/leopard-9a241/tools,
  logs/leopard-backport.md): real CGFloat ABI, 13 of the 66 missing functions, CTRunGetPositions. Decision: oracle for testing
  ctcompat, not a dependency (pre-release; two CoreTexts per process; objects must not cross). Its 11 TRANSITIONAL exports list
  exactly which functions changed shape in the double->CGFloat migration.
- CG ABI screen: 285 entry points, 1 genuine mismatch (a CGGState transform getter returning by value), adapted and tested.
- Tiger CoreGraphics behavior findings (runtime probes, spike/blendtest.c, CG-SURVEY.md): CGContextSetBlendMode silently ignores Copy, XOR,
  DestinationOver, PlusLighter and Clear (Multiply/Screen work; spike/cgprobe.c); CGShading discards alpha (cgcompat
  compensates for gradients); CGContextClipToMask is correct with gray masks but silently clips everything for stencil masks
  and RGBA images (WebCore clipToImageBuffer must convert to gray); shadows render ~26% lighter than modern CG. Canvas composite ops and CSS blend modes degrade to source-over on this port.
- jsc timing on the box (C loop, 2.2 GHz C2D): ~1.7 M loop iterations/s; fib(25) 135 ms. ~4x Tiger's 2007 JSC.
- Tiger CG: CGContextSetShouldSmoothFonts / SetAllowsFontSmoothing are no-ops in bitmap contexts; SetShouldAntialias is the only
  (context-wide) knob. No subpixel font smoothing on this port for bitmap contexts. Shims stay inert by decision.
- Tiger ImageIO (logs/imageio-probe.md): decodes PNG/JPEG/GIF(anim)/BMP/TIFF correctly; CGImageSourceGetStatusAtIndex always
  UnknownType (WebCore workaround must cover all frames); no partial decode until the whole file arrives; PNG-compressed ICO
  fails; CMYK JPEG near-black; sRGB-profile PNGs off by up to ~23/255.
- Tiger CG: private CGFontSetShouldAntialias/CGFontShouldAntialias (per-font flag) is the faithful target for
  CGContextSetShouldAntialiasFonts (kept no-op: WebCore only passes true; flag mutates a shared cached CGFont). Interpolation
  quality is binary on Tiger (None vs everything else = High); reading state back can't detect it.
- Image libs in the sysroot (deps/build-c-deps.sh): libpng 1.6.48, libjpeg-turbo 3.1 (no SIMD), libwebp 1.5 (+demux/mux/sharpyuv),
  all static with .pc files. For WebP / PNG-in-ICO / CMYK JPEG via WebKit's cross-platform decoders; ImageIO stays default.
- Tiger CoreText shaping (logs/ct-probe.md): reads AAT (morx) only, no OpenType GSUB/GPOS. System fonts of the era carry AAT and
  shape fine; OpenType web fonts get no ligatures / Arabic-Indic forms. Plan: HarfBuzz (building into the sysroot) via WebKit's
  ComplexTextControllerHarfBuzz for fonts without AAT tables, glyph drawing stays CG. Cap-height/x-height are quantized to
  half points on Tiger (adapters requested); every other metric matches modern to 6 decimals.

## Git rule (2026-09-20 21:10)
- Commit only your own paths: `git commit -o <files> -F msgfile` (`-o` commits exactly the named paths regardless of what else
  is staged; `git commit -- <paths>` also works). Never `git add -A`, never `git commit --amend` (amend commits the whole shared
  index). Check `git show --stat` after each commit. A wrong message is cheaper than a wrong file set; don't rewrite history. Never `git add -A` on this shared tree. NOTES.md is shared: append your
  own bullets/sections and include NOTES.md in your path-scoped commit; don't rewrite others' text.
- HarfBuzz 14.5 (meson, static, OT shaper only) is in the sysroot; deps/spike-tests/test_harfbuzz.c shaped Latin and Arabic on the box.
- Static ABI screening is complete (tools/abi-screen.py, logs/abi-screen-cf.md): 510 functions, every over-read already adapted.
  Re-run 22:00 against the FROZEN post-update binaries (md5-verified against the box; `--binaries=<dir>` overrides sysroot/ until
  the re-mirror lands): every count and every named function identical to the pre-update run, stub sweep included. No new hits.
- CG behavioral probing complete (logs/shim-audit.md, spike/): pattern tiling, transparency layers under CTMs, dash phase,
  clip-to-rects, masking colors all match modern. cgcompat's CGContextDrawTiledImage loop seamed at fractional origins;
  fixed in ce943a5 by bracketing the loop with antialiasing off, so adjacent tiles' shared edge snaps the same way for both
  (spike/cgtest.c asserts no interior pixel drops below full alpha, and the check was verified to fail with the fix removed).
  Shadows ~92% of modern ink; no alpha correction by decision (would worsen large blurs).
- CoreText cap-height/x-height adapters: midpoint of flat and round glyph heights reproduces modern CT to 0.03% (was 5.8%).
- HarfBuzz on Tiger proven with GSUB: DejaVu 'fi' -> one ligature glyph; Arabic joined forms differ from isolated (deps/HARFBUZZ.md).
- Shaping boundary refined (logs/ct-probe.md, commit bb1cf0d): Tiger's CoreText does OpenType liga/kern and AAT Arabic identically
  to modern; it lacks only OpenType *joining* (initial/medial/final selection by the shaper) and reordering, i.e. complex scripts in
  fonts without morx. 41/49 of Tiger's font files have morx/mort; the six Hiragino CJK faces are OpenType-only (costs vertical
  forms/ruby). HarfBuzz fallback scope: complex scripts in AAT-less fonts. Glyph rasterization geometry is identical to modern.

## Foundation behaviour differences from modern (2026-09-20, audit track)
- `-[NSString stringWithFormat:]` does **not** understand the C99 length modifiers `%zu`, `%zd`, `%jd`, `%td`:
  the specifier is emitted as literal text, so `%zu` with 123456 yields the two characters `zu`. Silent
  corruption, not an error. `%lld`/`%llu`/`%qi`/`%qu`/`%hd`/`%hhd` are all fine. No ObjC format string in
  WebKit uses `%z` or `%ll` today; don't introduce one. (logs/foundation-probe.md)
- `+[NSURL fileURLWithPath:]` produces `file://localhost/tmp/x` with `host` == `"localhost"`, where modern
  gives `file:///tmp/x` with a nil host. The two are not string-equal, so anything comparing file URLs by
  absolute string or inspecting `-host` sees a different shape. Also `+URLWithString:` returns nil for a
  string containing a raw space, where modern parses leniently.

## Linking libtigercompat (2026-09-20, audit track)
- **`-ObjC` is mandatory and its absence fails silently.** A category in a static archive is only pulled in when
  something references a symbol in the same object file, so without `-ObjC` every nscompat category is absent at
  runtime with no link error and no warning. Symptom is a selector-not-found for a method that *is* shimmed
  (found via `-[NSCFString stringByReplacingOccurrencesOfString:withString:]`).
- `-ObjC` force-loads the whole archive, so the link line then also needs `-ltigerdispatch -framework AppKit
  -framework ApplicationServices` (nscompat-appkit.m.o wants NSColor/CGColorCreate, nscompat-operation.m.o wants
  dispatch_get_{global,main}_queue). Working line: spike/run-fndbehaviour.sh.
- Differential probes beat single-platform ones: build one source for Tiger *and* the host, print identical
  KEY=value lines, diff. spike/{cgbehaviour.c,fndbehaviour.m} do this. It caught two of my own broken tests that
  a Tiger-only run reports as agreement: a transparency-layer case that measured nothing, and a y-axis mistake
  that made every point sample read an empty pixel on both platforms.
- **Evidence that a comparison is sound has to be independent of the thing being compared.** Two of us hit this
  from opposite directions in one day. Comparing font metrics by font *name* across machines: ascent and descent
  agreeing looked like proof the comparison was valid, but those come from `hhea` and say nothing about outlines,
  so a metrically-compatible but differently-outlined file passes that check silently (fix: a cross-machine font
  probe must ship its own font file). And reading a matching argument list off a disassembly says nothing about
  what the function does with them, which is the whole reason the behavioural probes exist alongside the ABI screen.
- WebKit CMake tip (wkcmake): WebKitMacros.cmake's `<framework>_UNIFIED_SOURCE_EXCLUDES` filters Sources*.txt by regex before
  unified bundles are generated (runs after PlatformCocoa.cmake), so excluding directories needs no edits to the .txt lists.
- jsc on the TigerBrowser script.html loop: ~2.24 s vs 5.3 s on Tiger's 2007 WebKit (2.4x, C loop, no JIT).
- Testing caveat: NSPasteboard is nil for processes launched over ssh on the box (pbs not in the session's bootstrap
  namespace); pasteboard tests need a console-session launch. Window server, windows, screencapture all work over ssh.
- Tiger AppKit coalesces multiple setNeedsDisplayInRect: into one drawRect: with the full view bounds (more repainting than modern).
- Cap-height/x-height: Tiger's NSFont quantizes to the same half-point grid as its CoreText (same ATS measurement); the 16pt
  Helvetica agreement was a lucky size. The midpoint heuristic stays (Courier, no OS/2 on either OS, agrees to 0.04%). Hazard:
  WebCore paths that read NSFont metrics on this port will disagree with the CT-derived ones; check per call site.
- CA host phase 2 (spike/CAHost, commit 3e38673): viewport(masksToBounds, geometryFlipped) > page > 48 manual 256px tile
  CALayers painted via -drawLayer:inContext: (flip the CTM yourself) + composited overlays. Scroll = viewport bounds.origin, no
  repaint. CA render 0.5-0.9 ms/frame; tile paint ~5 ms; 12 live tiles ~32 MB RSS. hitTest: works. CATiledLayer draws NOTHING
  under CARenderer (delegate runs, no output): use manual tiles. Layers with non-affine CATransform3D depth-sort behind opaque
  siblings: set zPosition. geometryFlipped cascades to the whole subtree; no -geometryFlipped getter (use valueForKey:).
- CommonCrypto on Tiger: CommonDigest only (MD2/4/5, SHA1/256/384/512; no SHA224), no CommonCryptor/CCHmac/PBKDF/CCRandom.
  WebCrypto symmetric algorithms excluded; digests stay; LibreSSL is the eventual answer.
- Overlay: NSPoint/NSSize/NSRect unified with the CG types (NSGEOMETRY_TYPES_SAME_AS_CGGEOMETRY_TYPES, as 10.5 did); CFBase.h
  gained CF_ENUM/CF_OPTIONS. WEBKIT_MAX_BUNDLE_SIZE is 16 under TIGER (non-modular 10.4 headers overflow clang's source locations).
- SDK protection (2026-09-20 21:45): sdk/MacOSX10.4u.sdk and MacOSX10.5.sdk are read-only (chmod -R a-w); writes through
  overlay symlinks now fail loudly instead of truncating SDK headers. `toolchain/verify-sdk.sh` checks the 10.4u SDK against
  sdk/MacOSX10.4u.sdk.sha256 (6516 files). Restore from the tarball on failure. Overlay files: delete the symlink before
  writing a real file at that path (make-overlay.sh warns about this).
- Video plan (logs/qtkit-plan.md): Tiger ships QuickTime/QTKit 7.2. The 2018 MediaPlayerPrivateQTKit's two rendering paths
  (QTMovieLayer, private QTVideoRendererWebKitOnly) are absent; -[QTMovie frameImageAtTime:withAttributes:error:] with
  QTMovieFrameImageTypeCGImageRef is present (QTKit gates on QTKIT_VERSION, not OS version) and is the path: CGImage per frame,
  drawn in the software path, later a layer's contents. Registration is now a MediaPlayerFactory subclass. Codecs: H.264
  Baseline/Main + AAC/MP3 in MOV/MP4 only (no WebM/AV1/Opus). Apple TV QuickTime is identical to Tiger's.
- Compositing design (logs/ca-hosting-design.md): PlatformCALayerTiger as a sibling of PlatformCALayerCocoa; TileController/
  TileGrid port as-is (they already use plain CALayers); derive zPosition in setTransform for non-affine transforms or 3D
  elements vanish; phase 1 = USE_CA on with AnimatedOpacityTrigger only, HAVE_IOSURFACE off; ~2400 new LOC through phase 2.
- Video path confirmed on the box (spike/qtkittest.m): -[QTMovie frameImageAtTime:withAttributes:error:] with
  QTMovieFrameImageTypeCGImageRef returns a real CGImageRef (CFTypeID == CGImageGetTypeID) for H.264 Baseline+AAC MP4/MOV;
  audio-only MP3/M4A play; works with or without NSApplication. QTKit's 10.5 availability annotations must be neutralized
  around the import (same #undef trick as the CA headers). Test media under spike/qtkit-media/.
- 2026-09-20 22:00: the user updated the Tiger box to QuickTime/QTKit 7.6.4 (QTKit build 1327.73). sysroot/ now mirrors the
  7.6.4 QTKit, QuickTime frameworks and /System/Library/QuickTime components (incl. AppleVAH264HW.component, AppleProResDecoder);
  the 7.2 copies are in sysroot-old/. Export lists: logs/api/tiger-QTKit-7.2.txt vs tiger-QTKit-7.6.4.txt (117 new/118 removed
  symbols, e.g. QTMovieFrameImageForce(No)VisualContexts, QTMovieNaturalSizeDidChangeNotification). Plan and spike being redone.
- 2026-09-20 21:50: the box received Security Update 2009-005 (Intel), QuickTime 7.6.4, ImageIO.pkg, RAWCamera, Safari 4.1.3
  (new system WebKit 533.x with JIT), Java release 8, iLife bits (receipts in /Library/Receipts). ATS/CoreGraphics/ImageIO/
  libxml/libxslt/OpenSSL/libz system binaries changed. sysroot/ is being re-mirrored (agent "remirror"; old copy will be
  sysroot-old-preupdates/, old export lists logs/api/preupdate/). All CT/CG/ImageIO/ctprobe suites are being re-run on the
  updated box. Our own deps are static, so only the shim measurements are affected.
  - **A framework's version string did not move when its binary did.** CoreText is still 1.1.3 /
    source 511200 with a different md5, so comparing versions after a re-mirror would conclude
    nothing had changed. Compare checksums, or compare export sets and disassembly with addresses
    stripped, which is what actually established that nothing behavioural moved (ctcompat: export
    sets identical across CT/ATS/CG, 29 load-bearing functions byte-identical; audit: every
    behavioural probe byte-identical pre and post).
  - **`sysroot/` is mutable and `refs/` is not.** Tiger facts come from the mirror, which an update
    can invalidate under you; the 10.5.8 and 9A241 reference facts are checked-in files nothing on
    the box can touch. The two get cited a paragraph apart in the surveys, so when quoting an
    address always name the file it came from.
- QTKit 7.6.4 (logs/qtkit-plan.md §8, spike/qtkittest.m): QTVideoRendererWebKitOnly (private) now exists, so the 2018
  backend's software paint path ([renderer drawInRect:]) ports ~unchanged; QTMovieLayer still absent (needs 10.5 AppKit).
  H.264 High profile and 720p decode on 7.6.4. frameImageAtTime:withAttributes:error: returns an AUTORELEASED CGImageRef:
  never CFRelease it (7.6.4 crashes at pool drain). Cost: ~48 ms/frame at 320x240, ~1.4 s/frame at 720p via frameImageAtTime
  (use the renderer path for playback). QTMovieOpenForPlaybackAttribute=YES still allows frameImageAtTime on 7.6.4.
- Gate rule (wkcmake): shim when Tiger can truthfully answer, gate when the concept doesn't exist (NS_ACTIVITY, HDR, NSAppearance,
  touch bar...). Whoever implements a symbol owns its declaration; WebKit's PAL SPI header steps aside under TIGER.
- Upstream bugs found so far: WTF AvailableMemory 32-bit overflow on >4 GB RAM; JSRemoteInspector.cpp reaches RemoteInspector
  unguarded with ENABLE_REMOTE_INSPECTOR off; NativeImageCG single-pixel read on an uninitialized buffer.

## Rule that keeps paying: check what Tiger already has before writing a shim

Four cases now, from three tracks, where the 10.4 system could answer a question
we were about to answer ourselves:

- `CGEventSourceButtonState` for `+[NSEvent pressedMouseButtons]`. Quartz Event
  Services shipped in 10.4, so that shim is a real query rather than a stub.
- `CFRunLoopGetMain`, exported by Tiger's CoreFoundation but **not declared in
  the 10.4u SDK header**. It cannot implement `+[NSRunLoop mainRunLoop]`, since
  NSRunLoop and CFRunLoop are not toll-free bridged, but it does let a test
  check that the captured object really is the main run loop.
- The per-font antialias flag (ctcompat track).
- The whole `uuid_*` family for NSUUID, which an early survey of mine wrongly
  called missing after a truncated grep.

Before trusting an export list, check it filters on **defined** symbols. A list
built without that filter contains everything the binary imports as well, so a
"present-but-undeclared" check against one reports capabilities the framework
does not have. The cgcompat track hit this: `logs/api/tiger-ImageIO.txt` listed
699 names where ImageIO defines 269, the other 430 being CoreFoundation and libc
imports. Verified clean for the lists used here: `tiger-CF.txt` contains none of
`objc_msgSend`, `malloc` or `pthread_mutex_lock`, all of which CoreFoundation
imports. The four symbols above are `T` in the binaries and are exercised by
passing tests on the box, which is the stronger check of the two.

The corollary that bit twice: **the export list and the SDK header disagree
often.** `logs/api/tiger-*.txt` is what the binary exports; the SDK is what 2005
chose to declare. Check the export list, not just the header, before concluding
something is absent. And read the whole grep: `grep -i uuid ... | head` is what
produced the wrong NSUUID conclusion, because the list is sorted and the `u`
entries came after the visible lines.

## Matching a multi-part selector

To ask whether `foo:bar:baz:` is used anywhere, reconstruct it from the keyword
parts. Never grep the joined-up string: a real call site interleaves the
arguments and never contains that text. This dismissed three live call sites in
one triage pass before it was noticed. See the triage table at the end of
`compat/NSCOMPAT-SURVEY.md`.
- BOX STATE FROZEN 2026-09-20 22:00 EDT: 10.4.11 8S2167 fully updated (111 receipts; last: Java release 9, iPhoto 7.1.5 at
  21:58). WebKit.framework 4533.19.4 (Safari 4.1.3), QTKit/QuickTime 7.6.4, Security Update 2009-005, Xcode 2.5. No further
  Apple updates exist for Tiger. Any measurement dated before 21:57 EDT was against the pre-update system.
- Post-update re-verification (22:00 state): CoreGraphics 1.258.77->1.258.85, ImageIO 1.5.6->1.5.9, CoreText/ATS patched with the
  SAME version strings (md5 differs): exports identical everywhere, the 29 load-bearing CoreText functions byte-identical in
  disassembly, all CT/CG/ImageIO suites and probes reproduce exactly. Version strings do not detect Apple patches; compare bytes.
- Reboots clear the box's /tmp: redeploy test binaries and the 9A241 oracle rig after any reboot ("missing" != "differs").
- JS reality check: Safari 4.1.3's WebKit (533.19.4, i386 JIT) runs TigerBrowser's 2M-iteration loop in ~59 ms; our C-loop jsc
  takes 2.24 s (~38x slower). The 2007 non-JIT WebKit took 5.3 s. HTTPS still fails through the system stack after the update.
- QTVideoRendererWebKitOnly on 7.6.4 (spike/qtrenderertest.m): works; 320x240 decodes in real time (15/15 fps, drawInRect ~35 ms);
  720p software decode manages ~2 fps (drawInRect 220-390 ms). Its notification constant is exported but undeclared in the SDK:
  dlsym it (WebKit's SOFT_LINK does the same). For HD: QuickTime's OpenGL visual-context path (QTOpenGLTextureContextCreate /
  setVisualContext:, exported in 7.6.4) into the CARenderer host, or downscale.
- JIT verdict (logs/jit-i386-plan.md): upstream removed ARMv7 JIT (857bd433, 2026-08-01) and ALL 32-bit JSValues (29ceb3c0,
  2026-08-02) six weeks before our checkout; x86-32 JIT was gone since 2021 (bug 229331). Our i386 jsc runs the C loop with
  64-bit JSValues. Resurrecting a 32-bit JIT = 17-31k LOC rebuilding the value representation across LLInt/Baseline/DFG against
  an upstream deleting it. Decision: no JIT; JS is interpreter-speed on this port (2.24 s vs Safari 4.1.3's 59 ms on the test
  loop). Only percent-level CLoop/compiler tuning remains (try -O3/LTO for JSC).
- WebCore M2 first full pass: 97/557 failed; CommonCryptoSPI types + HAVE_TASK_IDENTITY_TOKEN off cleared 177 errors; media
  pipeline excluded; second pass running.
- WebKitLegacy plan (logs/webkitlegacy-plan.md): ~430 LOC edits + 40 build config; 5 files excluded; do NOT exclude Plugins/
  (inert already) or the inspector client files (breaks link). Must-fix: WebDelegateImplementationCaching.mm casts objc_msgSend
  to a float-returning fn pointer -> objc_msgSend_fpret on i386 (silent garbage otherwise); curl's CA bundle path is compiled as
  a host path -> ship cacert.pem in Resources and set it at runtime (CurlSSLHandle::setCACertPath, PlayStation pattern).
  Checked: plain .m files with @implementation ivars and auto-synthesized properties compile and run on the box with the
  patched clang (spike/fragileivars_c.m), so the plan's two ivar items (WebPanelAuthenticationHandler.m,
  WebJavaScriptTextInputPanel.m, WebFeature.m) need no edits.
- JS PERFORMANCE (reopened 22:20 at the user's request; it's critical to them). Three options under evaluation:
  (1) keep 2026 tree + interpreter (2.24 s test loop); (2) pin to the last tree with JSVALUE32_64 + x86-32 JIT (~mid-2021,
  bug 229331) and revive the JIT (agent jit2021, logs/jit-pin-2021-plan.md); (3) a 64-bit content process (WebKit2-shaped:
  32-bit Cocoa UI process + x86_64 content process with today's fully-maintained x86_64 JIT) — spikes: jsc PORT=JSCOnly
  x86_64 on Tiger's 64-bit libSystem (agent jsc64, worktree WebKit-jsc64, logs/jsc64-spike.md) and Leopard 10.5.0's x86_64
  CF/CG/CoreText loaded privately in a 64-bit process (agent leopard, logs/leopard-x86_64-spike.md). Decision is the user's.

## DIRECTION SET BY THE USER (2026-09-20 22:35)
- Make as much as possible 64-bit; maximize performance on the Core 2 Duo, including CPU-specific optimization
  (-march=core2 -O3, LTO, SIMD in decoders). Target sites: YouTube (needs MSE + H.264 software decode in-process),
  React apps (JIT), graphically heavy sites like The Verge (image formats, HTTP/2, compositing).
- Architecture: WebKit2-shaped split. 32-bit Cocoa UI process (thin: window, events, IME, pasteboard, CA compositing host
  with the Apple TV QuartzCore) + 64-bit content process (WTF/JSC with the x86_64 JIT incl. FTL, WebCore, curl networking)
  on Tiger's x86_64 libSystem. Rendering backend inside the content process: decided by the spikes (Leopard x86_64 CG/CT
  loaded privately vs cairo/freetype/harfbuzz). WebKitLegacy is no longer the target; the 32-bit WebCore work continues only as
  far as it transfers (generic Tiger gates, compat layer for the UI side).
- 2021 pin (logs/jit-pin-2021-plan.md): x86-32 JIT alive on GTK/WPE Linux CI until deletion 2021-08-20 (bug 229331); pin point
  82044153d434; cheaper port (OptionsMac.cmake + curl WK1 backend still in-tree) but freezes platform/security at 2021 and
  Mac x86-32 JIT correctness was never bot-tested. Status: fallback only, given the user's 64-bit/latest direction.
- Post-update ABI screen: 510 functions, no new hits (dd2d487). WebCore 32-bit compile: 51 failures left, named list.
- qtkit-plan §9: QTVisualContext C API lives in QuickTime.framework (present on 7.6.4); video as a GL quad in the CARenderer
  frame (no IOSurface, so no zero-copy CALayer.contents). Relevant only to a 32-bit media path now.
- USER DECISION (22:50): the 2021 pin is OFF THE TABLE, not even as a fallback. The port runs the 2026 WebKit tree, period.
  JS performance comes from the 64-bit content process with today's x86_64 JIT; if the 64-bit path failed, the answer would
  be the 2026 tree on the interpreter, never an older browser.
- TOOLCHAIN BUG (23:00): cctools ld64-956.6 crashes linking x86_64 at -macosx_version_min 10.4 when inputs carry EH personality
  references (libcrypto.a; any C++ with exceptions): "Assertion failed: (targetAtom != NULL), ld.hpp:914" in the x86_64 classic
  stub pass. Agent ld64fix is fixing it (patch to toolchain/patches/) and building a stopgap that links on the box with Xcode
  2.5's /usr/bin/ld64 (toolchain/bin/tiger-ld64-onbox.sh). Plain C with -fno-asynchronous-unwind-tables -fno-unwind-tables
  avoids the trigger. i386 links are unaffected.
- WebCore 32-bit compile parked (23:20, commit e788401 in WebKit/): 478/480 targets, 49 TUs / 240 errors in 8 clusters (absent
  frameworks to exclude; CFNetwork cookie SPI moot under curl; modern CG/ImageIO SPI; AppKit 10.10+ API; Security SecTrust
  vintage (CSSM adapter possible); wheel-phase gating fallout; method_copyReturnType & co. missing in Tiger's runtime (objcrt);
  crypto via LibreSSL). The JSString.h subscript ambiguity is a fragile-ABI artifact, not 32-bit. No link attempted.
- Audio bridge proven (spike/audiobridge, 9dc8fe2): 64-bit producer -> shm_open/mmap SPSC ring -> 32-bit CoreAudio consumer
  (Component Manager API on Tiger; no CoreAudio/AudioUnit/AudioToolbox x86_64 slices exist). 0 underruns, ~12 ms steady latency,
  <4% CPU. RULE for any 32/64-bit shared struct: 4-byte fields only (i386 ABI 4-byte-aligns 8-byte fields; x86_64 8-byte-aligns
  them); split 64-bit values into two uint32_t; verify sizeof/offsetof from both compilers (spike/audiobridge/ringlayouttest.c).

## x86_64 linking fixed (2026-09-20 22:25, ctcompat/ld64 track) — 64-bit links work, no on-box linker needed
- **Root cause of the x86_64 link crash** (`Assertion failed: (targetAtom != NULL) ... ld.hpp, line 914` in
  `stubs::x86_64::classic::StubHelperAtom`): in `ld64/src/ld/passes/stubs/stubs.cpp`, both x86_64 *classic* stub
  call sites passed `stubToGlobalWeakDef` as the constructor's **third** argument, which is `forLazyDylib`
  (see `stub_x86_64_classic.hpp`; the i386 classic call site right above passes `forLazyDylib` correctly).
  So any target that is a global weak def selected `internal()->lazyBindingHelper`, which is only ever set for
  `-lazy_library` and is otherwise NULL, and the `ld::Fixup` constructor asserted. Nothing to do with CIE
  personalities: frames 1 and 2 of that backtrace are misattributed cold-section symbols.
  Deployment targets < 10.6 take the classic (non-`dyld_stub_binder`) path, so this hit **every** x86_64 link at 10.4
  whose input pulled in a weak def. libcrypto.a does, which is why it looked libcrypto-specific.
- Fix: `toolchain/patches/cctools-ld64-x86_64-classic-stubs.patch` (2 lines + a comment), applied and installed in
  `toolchain/cctools/bin/i386-apple-darwin8-ld`. i386 is untouched (spike/{exctest.mm,hello.mm,fstest.cpp} still pass).
- **Use `toolchain/bin/tiger-clang64{,++}` as-is for all 64-bit work.** No on-box `ld64` fallback wrapper was written;
  the cross linker is correct now. (If one is ever needed, Xcode 2.5's `/usr/bin/ld64` is on the box.)
- Second bug found while verifying: `_dyld_find_unwind_sections` in compat/libcompat.c used `struct section` /
  `getsectbynamefromheader`, i.e. the **32-bit** Mach-O accessors, so in a 64-bit image it read garbage section
  addresses and every C++ throw hit `libc++abi: terminating due to uncaught exception`. Now switched on `__LP64__`
  to `section_64` / `getsectbynamefromheader_64`. **C++ exceptions now work in 64-bit binaries on the box**
  (throw across a function, catch by type, and `catch(int)`); `toolchain/sysroot-x86_64/usr/lib/libtigercompat.a`
  was rebuilt (it holds availability.o, libcompat.o, tlv.o; there is still no script for it, build it with
  tiger-clang64 and the compat Makefile's CFLAGS).
- Link line that works for 64-bit C++: `tiger-clang64++ -nostdinc++ -isystem toolchain/sysroot-x86_64/usr/include/c++/v1
  -stdlib=libc++ -lc++ -lc++abi -lunwind -ltigercompat` (libtigercompat supplies `_dyld_find_unwind_sections` and
  `posix_memalign`, both of which libc++abi/libunwind need and Tiger lacks).
- ld64 x86_64 FIXED (2d12f9c): stubs.cpp passed stubToGlobalWeakDef as forLazyDylib at the x86_64 classic call sites, so any
  weak-def target used the NULL lazy-binding helper. Patch: toolchain/patches/cctools-ld64-x86_64-classic-stubs.patch. Also
  compat's _dyld_find_unwind_sections now uses section_64 accessors under __LP64__ (64-bit C++ exceptions work on the box).
  tiger-clang64{,++} link everything; no on-box ld64 stopgap needed. libtigercompat x86_64 archive: built by hand (availability.o,
  libcompat.o, tlv.o); the compat Makefile is i386-only (TODO: ARCH switch).
- compat/Makefile now takes ARCH: `make -C compat install` is unchanged (full i386 archive, obj/, sysroot-i386, ARC header check),
  `make -C compat ARCH=x86_64 install` builds only the arch-neutral C gaps (availability.c, libcompat.c, tlv.c, BlocksRuntime)
  with tiger-clang64 into obj-x86_64/ and compat/libtigercompat-x86_64.a, installed as toolchain/sysroot-x86_64/usr/lib/libtigercompat.a.
  The CF/CG/CT/NS shims stay i386-only: they are about Tiger frameworks no 64-bit process can load, and Tiger libobjc is i386-only,
  which is also why the ARC header check is skipped for ARCH=x86_64. Both verified from clean.
- 64-BIT ON TIGER CONFIRMED (logs/leopard-x86_64-spike.md, e4b6982): x86_64 processes run on 10.4.11 (32-bit kernel, x86_64
  libSystem 3230 exports + x86_64 dyld). All 11 JIT prerequisites pass: RWX mmap, W^X flips, executing generated code, mach_vm
  family, 16 MB thread stacks, 64 GB VA reserved. Tiger has NO x86_64 CF/CG/Cocoa/libobjc, so Leopard 10.5.0's x86_64 copies
  load privately with small shims (CoreText 0 missing, ColorSync 1, ATS 3, libobjc 3, CG 9, CF 12; CoreServices' re-export load
  commands must be demoted for dyld-46). CoreFoundation and the ObjC runtime work; CoreGraphics faults in CGBitmapContextCreate
  (being isolated with gdb); CoreText/ImageIO untested pending a context. Flat namespace is a spike technique; per-binary import
  repointing is the shipping form.

## Reference linker: toolchain/apple-ld64-97/ld (Rosetta) — 2026-09-20 22:30
- Apple's own **ld64-97.17**, the linker Xcode 3.2.6 shipped, extracted from /Users/shg/Downloads/xcode_3.2.6_and_ios_sdk_4.3.dmg
  (`Packages/DeveloperToolsCLI.pkg` -> `xar -xf ... Payload` -> `gzip -dc Payload | cpio -id ./usr/bin/ld`). Universal i386/x86_64,
  **no ppc**, so run it as `arch -x86_64 toolchain/apple-ld64-97/ld ...` under Rosetta 2. Gitignored (not redistributable);
  re-extract with the recipe above. `arch -x86_64 toolchain/apple-ld64-97/ld -v` prints `PROJECT:ld64-97.17`.
- Use it as an oracle when a link looks wrong, not as the build linker: it takes classic options only
  (`-macosx_version_min 10.4`, not `-platform_version`), knows nothing of LTO or `-mllvm`, and **re-roots `-L` paths under
  `-syslibroot`**, so an absolute `-L` outside the SDK is silently dropped and `-lfoo` can resolve to the SDK's dylib instead
  of our static archive. Pass archives by full path when comparing. It reads LLVM 21's x86_64 objects without complaint.
- **Validation of our patched cctools ld64 against it** (x86_64, `-macosx_version_min 10.4`, same objects, three programs:
  the libcrypto/HMAC repro, a C++ throw/catch test and a static-libc++ std::sort/std::string test). All six binaries run
  **identically on the box**. Findings:
  - Both take the **classic** path: `dyld_stub_binding_helper`, no LC_DYLD_INFO, no `dyld_stub_binder`. This is the direct
    confirmation that the classic-stub fix restores Apple's own behaviour rather than working around it.
  - **Symbol tables are identical**, except that ld64-97 drops the local `GCC_except_table*` labels and we keep them. Keeping
    them is what we want anyway (LINK RULE 2).
  - Section **names** differ, same roles: ours `__stubs` / `__got`, ld64-97 `__symbol_stub1` / `__nl_symbol_ptr`. Stub helper
    size is byte-identical (0x498 for the libcrypto case); ours places `__stub_helper` right after the stubs, ld64-97 puts it
    after `__const`. No `__IMPORT` segment in either (that is an i386-only classic thing).
  - We emit three load commands ld64-97 does not: LC_VERSION_MIN_MACOSX, LC_FUNCTION_STARTS, LC_DATA_IN_CODE. Tiger's 64-bit
    dyld ignores them (all three binaries run), so they are harmless, but they are the only load-command difference.
- `sdk/MacOSX10.6.sdk` also came out of that dmg (`Packages/MacOSX10.6.pkg`), read-only like the other two, for reference
  headers only. Never build against it: it declares 10.6 API that Tiger does not have.
- SPLIT-PROCESS PLAN (logs/split-process-plan.md, d970345): UNIX-domain-socket IPC (WebKit's unix transport; Mach path needs
  libdispatch port sources), DrawingAreaCoordinatedGraphics non-accelerated path (tiles in shared memory -> UI process CA host),
  clone the PlayStation port (cairo + curl, no GLib, no ObjC) for the content process; no x86_64 libdispatch needed
  (RunLoopGeneric/WorkQueueGeneric). Cross-ABI IPC: 3 fixes + a guard; raw shared structs use the 4-byte rule, IPC::Encoder
  uses _Alignof so the wire format agrees. Branch (a) (Leopard x86_64 CF/CG/CT) not to be gated on (chains into libobjc/libauto).
  COSTS: CT/CG shim tracks become UI-process-only assets; controls look Adwaita unless ~2000 LOC of Aqua painters are written.
  Milestone N0 = jsc64 with JIT running on the box; everything else waits on it.
- USER DECISION (23:40): controls "absolutely need to look like Aqua". Approach: the 32-bit UI process renders real Aqua controls
  (NSCell/HITheme, every type/state/size) into an atlas + 9-slice manifest; the 64-bit content process's RenderTheme paints from
  it (pixel-exact Tiger Aqua). On-demand rendering over IPC for states the atlas lacks is the extension. Scrollbars: Aqua metrics.
- **The exact trigger for that ld64 crash, measured against a rebuilt unpatched linker (22:35).** It is NOT exception
  handling. A plain C file with one `__attribute__((weak))` function, compiled with `-fno-asynchronous-unwind-tables
  -fno-unwind-tables`, still crashed the unpatched linker; a plain C hello with no weak symbol linked fine. The rule is:
  **any x86_64 link below 10.6 whose stubs include a global weak definition.** That is every C++ program, since inline and
  template functions, `operator new` and out-of-line `std::string` members are all weak defs, and it is also plain C that
  touches a weak symbol (libcrypto's `__explicit_bzero_hook` is what made the HMAC repro fail). Plain C without weak
  symbols is the only thing that ever worked, which is why the leopard track's six C programs and two C dylibs all linked.
  Do not use "no exceptions" or "no unwind tables" as a safety rule; it is not one.
- On-box linking, if ever needed (from the leopard track): the box's /usr/bin/ld64 is ld64-62.1 (Apr 2007). It parses
  `-install_name` as `-i` and dies, and it supplies no startup objects, so it needs the older spellings and explicit crt:
  `ld64 -arch x86_64 /usr/lib/crt1.o foo.o -lSystem -macosx_version_min 10.4 -o foo`, and for a dylib
  `-dylib /usr/lib/dylib1.o ... -dylib_install_name <path> -dylib_compatibility_version/-dylib_current_version`.
  Not needed for the build: the cross linker is correct now.
- Reference linker: toolchain/apple-ld64-97/ld (Apple ld64-97.17 from Xcode 3.2.6, run via `arch -x86_64`; classic options; pass
  archives by full path; add crt1.o/dylib1.o yourself). Our patched cctools ld64 matches its behavior (classic dyld_stub_binding
  _helper path, no LC_DYLD_INFO). sdk/MacOSX10.6.sdk added (read-only, reference headers).
- gdb 6.3 on the box rejects cctools-ld64 output (LC_VERSION_MIN_MACOSX 0x24, LC_FUNCTION_STARTS 0x26, LC_DATA_IN_CODE 0x29).
  For debuggable binaries link with the reference ld64-97 (emits none of them) or try -no_function_starts -no_data_in_code_info.
- UI-side video via QuickTime's OpenGL visual context (spike/CAHost/CAVideo.m): real-time to 1024x576; 720p 7-8 of 15 fps
  (decode-bound); QTKit's setVisualContext: drags in Core Image, so use the C Movie API (NewMovieFromProperties +
  SetMovieVisualContext + MoviesTask). BLOCKER FIXED: the Apple TV QuartzCore duplicates 207 Core Image class names and poisons
  +[CIFilter filterWithName:] in any process that loads both; spike/CAHost/decollide.py renames its CI* strings to ZI* (rebundle.sh
  runs it). Any UI process loading the private QuartzCore needs this.
- USER DECISION (23:45): not just Aqua looks, Aqua BEHAVIOR: menus, press states, keyboard interaction, "literally everything".
  Design: a native widget layer in the 32-bit UI process: real NSControls (NSButton/checkbox/radio, NSPopUpButton for <select>,
  NSSlider, NSProgressIndicator, NSScroller for scrollbars, file/color buttons) positioned over the page from layout geometry sent
  by the content process, moved synchronously with UI-process-driven scrolling, clipped per overflow container/iframe; native
  events, change notifications back over IPC; focus/tab interop; accessibility native. Text inputs stay WebCore-drawn/edited with
  Cocoa key bindings via the UI process's NSTextInput interpretation. Atlas painting remains the fallback for CSS-styled controls
  and controls under transforms/opacity. Precedent: early WebKit KWQ widgets; WebKit2's native popup menus.
- WEBKIT2 SPLIT SURVEY (logs/webkit2-split-survey.md, ded71b4): PLATFORM(COCOA) must be OFF on both sides of WebKit2 (the
  serialization generator bakes USE(CF)/PLATFORM(COCOA) conditions into the wire format; 115 Cocoa CoreIPC files). AppKit lives
  only in the app shell + view class via the C API. Template: PlayStation port (socket IPC, no GLib/CF/sandbox, generic RunLoop;
  unix transport already Darwin-aware; unix SharedMemory needs no edits; stub the eventfd-based IPC semaphore). UI process by hand
  on the C API (~3-4.2k LOC) not WKWebView (~27k + RemoteLayerTree). Backing-store DrawingArea is alive (Windows/PlayStation);
  one missing file: a CG backing store (~150-200 LOC) for the UI side. Two build dirs + one shared feature-flag fragment + a step
  diffing the generated IPC trees. NetworkProcess stays separate (curl sources exist; 81-line entry point). Content-process side
  ~1.4-1.6k LOC. BUILD RULE: both architectures must be compiled with the same clang, so that sizeof and alignof agree for every
  non-scalar wire type. The reason once given here was backwards and is corrected below: our clang reports alignof(long long)
  and alignof(double) as 4 on i386 (the correct ABI alignment) against 8 on x86_64, so 64-bit scalars were NOT safe by
  default; wireAlignmentOf in the cross-ABI patch is what makes them agree.
  Consequence for rendering branch (a): WebCore with USE(CG)/USE(CORE_TEXT) but PLATFORM(COCOA) off,
  the old Apple-Windows-port shape.
- ld64 x86_64 crash trigger CORRECTED: global weak definitions (every C++ program; libcrypto's __explicit_bzero_hook), not EH.
  Fixed; "no unwind tables" is not a safety test.
- x86_64 deps (deps/build-deps-x86_64.sh, 84c9037): zlib, brotli, nghttp2, LibreSSL, sqlite, libxml2/xslt, libpng, jpeg-turbo
  (SIMD), libwebp, dav1d, libavif, freetype, expat, fontconfig, pixman, cairo, ICU 76 (no renaming), HarfBuzz (freetype); all
  -O3 -march=core2, verified on the box; curl pending (linker now fixed).
- RULE: on this Apple Silicon host Rosetta 2 runs x86_64 conftests, so configure/meson/CMake think they are NOT cross-compiling
  and run-time probes report macOS 27's behavior (ICU silently built an empty data lib). Force cross_compiling=yes /
  needs_exe_wrapper=true / CMAKE_CROSSCOMPILING=ON for every x86_64 build; the i386 builds never hit this (Rosetta can't run i386).
- fontconfig: generated-file steps must not see the wrapper's -include tigerprelude.h (override $(CPP) for gperf generation).
- WIDGET LAYER CORRECTION (user, 00:05): live on-screen NSViews can't be drawn over by page content (z-order train wreck).
  Design: real AppKit controls hosted in an OFFSCREEN NSWindow in the UI process, captured to bitmaps on state change
  (cacheDisplayInRect:) and composited as CALayers in the page's layer tree (proper z-order/clipping/transforms); events
  translated and forwarded; AppKit modal tracking loops replaced by explicit highlight/click/value updates; NSMenu popups stay
  real windows. Prototype: CAHost phase 4 (revised).
- ARCHITECTURE CANDIDATE (d), now primary pending survey (00:15): "32-bit rendering, 64-bit everything else" = WebKit's
  GPU-process shape: 64-bit WebProcess (JIT, DOM, layout, decoding, HarfBuzz shaping incl. AAT, display-list recording) +
  32-bit render process replaying display lists with Tiger's real CG/CoreText via the verified compat layer, hosting the CA
  compositor (+ remoted WebGL on Tiger OpenGL) + 32-bit UI (AppKit, offscreen-rendered Aqua controls); NetworkProcess 64-bit.
  Reuses the whole 32-bit investment; Apple-exact text/graphics; JIT kept. Open question: is RemoteRenderingBackend/display-list
  remoting usable with PLATFORM(COCOA) off + USE(CG) on (logs/render-process-survey.md)? Fallback (b) cairo-in-64-bit;
  (a) Leopard frameworks opportunistic. Font handle: HarfBuzz metrics/shaping in 64-bit must match CT rasterization in 32-bit.
- 64-bit media (2026-09-20 22:30, media64 track, logs/media64-plan.md): **only libSystem, libz and libstdc++ are
  x86_64 on the box** — CoreAudio, AudioToolbox, CoreFoundation, QuickTime and QTKit are all i386/ppc only. So the
  64-bit content process gets software decode and nothing else: no CoreMedia, no audio device, no CG.
  FFmpeg 8.0 (decoders only, static, -O3 -march=core2, SSSE3 asm via nasm, pthread frame threading, libdav1d) builds
  for x86_64-apple-macosx10.4 and is installed in toolchain/sysroot-x86_64/usr — deps/build-ffmpeg64.sh. Three Tiger
  fixes in that script: -Dstatic_assert=_Static_assert (Tiger's assert.h has none); FFmpeg's `xmm_reg` renamed to
  ff_xmm_reg (Tiger's <mach/i386/thread_status.h> defines struct xmm_reg); and the installed libav*/libsw* headers are
  deleted before each build, because tiger-clang64's own -I$sysroot/include outranks FFmpeg's -I and shadows the
  source tree (same include-path trap as compat/include).
  Decode on the 2.2 GHz C2D, best of 3, 1/2 threads (spike/decodebench.c, spike/run-decodebench.sh,
  logs/decodebench-tiger.txt): H.264 High 480p 177/255 fps, 720p 73/112, 1080p 29/50; VP9 480p 112/154, 720p 47/76;
  AV1 480p (dav1d) 119/194. Audio 1 thread: AAC 261x realtime, MP3 127x, Opus 122x. yuv420p->BGRA with swscale
  1.37/3.08/6.99 ms per 480p/720p/1080p frame. **720p30 H.264 costs 12 ms of a 33 ms budget; 1080p30 costs 27 ms.**
  QTKit 7.6.4's software path did ~2 fps at 720p, so this is ~50x faster and is the argument for the split.
  Backend decision: purpose-written MediaPlayerPrivateFFmpeg (~5,300 LOC content-side + ~700 UI-side), not GStreamer
  (32k LOC of GTK/WPE-coupled glue on top of cross-building GLib + six gst modules against a libc with no CF).
  MSE is cheap because SourceBufferPrivate.cpp's coded-frame processing is port-independent: we only owe
  appendInternal, parsed with libavformat over a custom AVIO on a growing buffer. Audio must leave the process —
  clone RemoteAudioDestinationProxy's shape (AudioDestinationResampler + shared ring buffer) into the 32-bit UI
  process. Two decoder threads is the right number; a third buys nothing on two cores.

## Rosetta cross-compile trap (2026-09-20, deps agent)
Building x86_64 on this Apple Silicon Mac: Rosetta 2 transparently *executes* x86_64
binaries, so any configure-time check that compiles-and-runs a conftest (autoconf's
AC_RUN_IFELSE, CMake's check_*_source_runs/try_run, meson's compiler.run()) measures
*this Mac's* real behavior instead of Tiger's, and can silently bake in wrong answers.
Found via ICU: its old bundled autoconf re-derives `cross_compiling` by compiling and
running a trivial conftest regardless of `--host`, so on x86_64 it always concluded
cross_compiling=no -- which silently drops the real ~30MB icudata from the build in favor
of a 680-byte data-less stub (data is only built when `$tools=true or cross_compiling=yes`).
Audited every other x86_64 lib's config.log / meson-log / CMakeCache for the same class of
bug (grepped for actual AC_RUN_IFELSE / try_run / execution traces): none of the other
autoconf-based builds use runtime probes at all (modern autoconf just trusts `--host` and
never re-derives it by execution -- ICU's is unusually old), meson was already protected by
`needs_exe_wrapper = true` in every cross file (meson skips run-checks without a configured
exe_wrapper rather than silently running them), and CMake's CMAKE_CROSSCOMPILING correctly
comes out ON given CMAKE_SYSTEM_PROCESSOR x86_64 differs from the host's arm64 (matching
CMAKE_SYSTEM_NAME=Darwin alone would NOT have been enough, since the host is also Darwin).
Rule going forward for any x86_64 build script: `export cross_compiling=yes` before
autoconf configures, `needs_exe_wrapper = true` in meson cross files, `CMAKE_CROSSCOMPILING
ON` set explicitly in CMake toolchain files -- all three now standard in
deps/build-deps-x86_64.sh. If a configure script's *own* runtime probe (not just the
generic cross_compiling boilerplate) ignores all of the above, as ICU's did, patch that copy
of the source directly (see deps/src/icu-x86_64, "TIGER64: patched") rather than fighting it.
- IPC/shared memory proven (spike/ipc32x64, 6c45186/f88e046): fork+exec (no posix_spawn on Tiger); bootstrap_register works;
  Mach round trip 11 us; 64 KB inline 241 MB/s; shared frame memcpy ~900 MB/s; double-buffered pipeline ~176 fps; GL upload of
  a 1440x900 BGRA frame from the mapping ~8 ms (no copy; client storage is SLOWER for changing textures); exception ports give
  crash isolation without task_for_pid. Prefer mach_make_memory_entry_64 over shm_open (crash-safe lifetime). Static-assert
  struct layouts from both compilers. A CGL pbuffer over ssh is GPU-accelerated.
- CONTROLS DECISION (survey a375b4c): no live NSViews, no offscreen-view capture. WebKit already serializes ControlPart/
  ControlStyle and remotes control drawing (GPU process); route it to the 32-bit process and draw with real NSCells/HITheme
  (~275 LOC + 250 for scrollbars): same pixels as Safari, WebCore behaviors, real NSMenu popups. compat/aquacontrols.m (nscompat)
  is the drawing implementation. cahost phase 4 (offscreen views) cancelled.
- MEDIA DECISION (plan 7b4bbb3): ffmpeg-direct MediaPlayerPrivate + MSE (clone platform/mock/mediasource), NOT GStreamer;
  frames via paint() over the existing shared-memory path; codec policy refuses VP9/AV1/Opus so YouTube serves H.264; target
  360p smooth / 480p likely. NetworkProcess/curl is live upstream (WK1 ResourceHandleCurl restoration is dead work).
- TEXT INPUT (survey addendum): write a lean Tiger NSTextInput view (~1000 LOC) on the C API; 5 query messages need synchronous
  variants; port the IME staging logic (Korean/Vietnamese); clamp NSNotFound (32-bit) vs 64-bit replies.
- ACCESSIBILITY: remote AX is closed on Tiger (10.7 private class + ObjC in the content process); v1 must DECLARE absence
  (web view reports a group role with no children, ~15 LOC). Tiger shipped VoiceOver, so this is a known regression.
- BUILD: OptionsCocoa.cmake's TIGER block (lines ~165-199) is architecture-blind (forces C_LOOP, hardcodes the i386 sysroot);
  must be split per process before either 64-bit build can be configured.
- DECODE NUMBERS on the box (spike/decodebench.c, logs/decodebench-tiger.txt, ffmpeg x86_64 -march=core2, 2 threads):
  H.264 High 480p 255 fps / 720p 112 fps (8.9 ms) / 1080p 50 fps (20 ms); VP9 480p 154 / 720p 76 fps; AV1 480p 194 fps (dav1d);
  AAC 261x, MP3 127x, Opus 122x realtime; swscale yuv420p->BGRA 1.4/3.1/7.0 ms at 480p/720p/1080p. => 720p30 H.264 is ~1/3 of
  the machine; 1080p30 is all of it. ~50x QTKit. Recommendation: MediaPlayerPrivateFFmpeg (~5.3k content-side + 700 UI-side
  LOC), MSE via libavformat custom AVIO over the port-independent SourceBufferPrivate; audio via the RemoteAudioDestination shape
  over the proven shm ring. No CoreAudio/QuickTime/CF in x86_64 (only libSystem, libz, libstdc++ have x86_64 slices).
- Text input plan (logs/textinput-plan.md): NativeWebKeyboardEvent's Cocoa constructor already carries KeypressCommands; PageClient
  needs only interpretKeyEvent; a Tiger NSTextInput view is ~450-840 LOC; open design point: NSTextInput's synchronous queries
  vs cross-process EditorState (local cache, one-round-trip staleness).
- Rosetta trap audit: only ICU's old autoconf was affected; scripts hardened anyway (cross_compiling=yes, CMAKE_CROSSCOMPILING ON).
- **64-bit C++ exceptions: canonical link line and the trap that hid them (22:45).** Working line, also in `spike/run64.sh`:
  `tiger-clang64++ -nostdinc++ -isystem toolchain/sysroot-x86_64/usr/include/c++/v1 -stdlib=libc++ -lc++ -lc++abi
  -lunwind -ltigercompat`. `spike/cxx64exc.cpp` (run it with `spike/run64.sh spike/cxx64exc.cpp spike/throwlib.cpp`)
  covers 8 cases on the box and all pass: std::runtime_error by reference, catch by value, an 8-frame unwind, a
  destructor during unwinding, a custom class thrown in a second translation unit and caught here, the reverse
  direction caught by base class, a static-lib exception matching its std:: base, and rethrow preserving the type.
- **The trap: `toolchain/sysroot-x86_64/usr/lib/libtigercompat.a` had two producers and the wrong one won.** A copy
  built without the `__LP64__` fix to `_dyld_find_unwind_sections` was installed over ours, and the symptom is not a
  link error but `libc++abi: terminating due to uncaught exception` at the first throw, because the shim silently
  returns false and libunwind then finds no FDE. It also looks like a source bug rather than a stale file, which is
  what made it survive a report and a round of debugging. **Build it only with `make -C compat ARCH=x86_64 install`.**
  To check a suspect archive in one step, call `_dyld_find_unwind_sections` on `&main` from a 64-bit C program and
  print the result: 1 with non-null section pointers is good, 0 is the stale archive.
- RULE: staged artifacts in toolchain/sysroot-*/usr have ONE producer. libtigercompat.a comes only from
  `make -C compat install` (i386) / `make -C compat ARCH=x86_64 install`; never hand-build and copy it. A stale archive
  broke every 64-bit throw for an hour (two producers). Check: a 64-bit C program calling _dyld_find_unwind_sections(&main,..)
  must return 1. Canonical 64-bit C++ link line is in spike/run64.sh (spike/cxx64exc.cpp: 8 exception cases pass).
- ARTIFACT OWNERSHIP MAP (single producer each): libtigercompat.a (compat/Makefile, both arches); libtigerdispatch.a
  (compat/dispatch/Makefile, i386 only, not needed 64-bit); libc++/libc++abi/libunwind i386 (build/runtimes-i386 via the cmake in
  logs/runtimes-configure.log) and x86_64 (jsc64's build dir, same recipe; record its path in logs/jsc64-spike.md); compiler-rt
  builtins i386 (build/builtins-i386, by-hand script in NOTES) and x86_64 (jsc64); all third-party deps i386
  (deps/build-c-deps.sh) and x86_64 (deps/build-deps-x86_64.sh); ffmpeg x86_64 (deps/build-ffmpeg64.sh); sdk overlay
  (compat/sdk-overlay/make-overlay.sh + per-framework owners); QuartzCore private bundle (spike/CAHost/rebundle.sh + decollide.py).
  If you need a rebuild, run the owner's script; never copy artifacts by hand.
- Follow-up on that archive (22:50): the overwriting copy was **md5 a11c3ccf, installed 22:31, carrying an extra `os.o`
  built from `compat/dispatch/os.c`** (os_log / os_signpost). There is no 64-bit `libtigerdispatch.a`, so whoever needs
  os_log in a 64-bit binary has been folding that one object into libtigercompat.a by hand, and that hand-rolled archive
  is built from the pre-fix sources. **The correct x86_64 archive is md5 dabd0d25**, four members
  (availability.c.o, libcompat.c.o, tlv.c.o, runtime.c.o), identical to `compat/libtigercompat-x86_64.a`.
  Open question for the dispatch owner, since os.c needs compat/dispatch's own include layout and is not compat's file:
  either build a 64-bit libtigerdispatch.a, or add os.c to the compat Makefile's x86_64 list so the Makefile archive is a
  superset and nobody has a reason to overwrite it. Until that is settled the overwrite can recur, and it is silent.
- `deps/spike-tests/test_exceptions64.cpp` (the deps track's minimal case) passes with the current archive, verified
  verbatim with their own command line, as does `spike/cxx64exc.cpp`. Neither ever needed a link-order or visibility change.
- UI shell: spike/TigerBrowser (76c6b53) = CARenderer-hosted page view + toolbar/find bar/menus + framework check; the app
  bundles the decollided private QuartzCore. Tiger's system QuartzCore ALSO loads transitively via AppKit in any Cocoa app,
  so decollide.py is mandatory for every UI-side process. An NSOpenGLView's surface composites over sibling views: dock the
  window's NSScroller beside the page view, never over it. The box's stderr-to-file is fully buffered (use setvbuf or fflush).
- FONT HANDLE PROVEN (logs/hb-vs-ct.md, cf33772): HarfBuzz (64-bit layout) vs Tiger CoreText (32-bit raster) on identical font
  bytes: run width within 0.031 pt, per glyph 0.008 pt, metrics 0.008 pt incl. line gap, 69 comparisons, 5 fonts (DejaVu OT,
  Helvetica AAT+kern, Lucida Grande AAT, Geeza Pro morx Arabic, Hiragino OT CJK). HarfBuzz's ot shaper handles morx/kerx.
  Apple-format kern is distributed differently per glyph (CT on the leading glyph, HB split) but run widths agree; harmless since
  HB owns positions. Tiger can't join OpenType-only Arabic; HB can. Compare only glyphs the font covers (CTLine falls back).
- **Resolved (22:55): `make -C compat ARCH=x86_64 install` now produces the superset archive, so there is no reason to
  hand-roll one.** It builds `compat/dispatch/os.c` (os_log / os_signpost / os_unfair_lock) with that directory's own
  include tree and flags (`-std=gnu99 -fblocks -I dispatch/include`) and stages `dispatch/include` into the x86_64
  sysroot, so `<os/log.h>` and `<os/lock.h>` resolve there. **os_log in a 64-bit binary now comes from the sysroot's
  libtigercompat.a**; no extra library and no hand-built archive. Verified on the box: os_log_create plus an
  os_unfair_lock round trip run, and all 8 cases of spike/cxx64exc.cpp still pass.
  - New x86_64 archive: **md5 481e247f**, five members: availability.c.o, libcompat.c.o, tlv.c.o, **os.c.o**, runtime.c.o.
  - **i386 is unchanged**: os.c stays the property of libtigerdispatch.a there, and the i386 libtigercompat.a still has
    no os.o. The duplication is deliberate and commented in compat/Makefile; if a 64-bit libtigerdispatch.a is ever
    built, drop os.c from the Makefile's x86_64 list in the same commit.
- ARCHITECTURE DECIDED (00:50): (d) merged variant per logs/render-process-survey.md (73fbec3). TWO processes + network:
  * 32-bit "UI+render" process: AppKit shell/view, Core Animation compositor (private QuartzCore), display-list replay with
    Tiger CG/CoreText via compat, NSCell/HITheme control drawing (ControlPart remoting), text input/IME, audio output.
  * 64-bit web process: JSC (x86_64 JIT incl. FTL), DOM/layout, image + video decoding (ffmpeg), HarfBuzz shaping + font
    fallback (over the 32-bit-generated font manifest), display-list recording; no Apple frameworks (libSystem only).
  * 64-bit network process: curl/LibreSSL/HTTP/2.
  Precedent: WinCairo remotes 2D image-buffer drawing to its GPU process with PLATFORM(COCOA) off; ~110 messages, 2 gated.
  Must-do before first pixel: force neutral encodings for the 3 CG-flag-dependent wire types (color space, ShareableBitmap
  config, font attributes). Reuse Windows' coordinated-graphics layer delta (~1600 LOC) with a ~300-LOC scene applier building
  real CA layers. Metrics authoritative from the 32-bit side (CT heuristics). Estimate 4.9-7.2k LOC + UI process 3-4k.
  Branch (a) Leopard x86_64 CG/CT is DEAD for rendering (they block on 32-bit-only font/window-server Mach services); the
  64-bit JIT process itself is fine. Branch (b) cairo remains the fallback only.
- Aqua atlas done (spike/aquaatlas, 366 images, 18 controls, HIThemeDrawButton for window-inactive states): fallback artwork and
  the reference for compat/aquacontrols.m. Tool gotchas: CGBitmapContextGetData is NULL on Tiger unless you supply the buffer;
  a bare executable / first launch of a new bundle can't become active (inactive artwork); HIThemeDrawTrack draws whole scrollbars.
- IPC cross-ABI patch (toolchain/patches/webkit-ipc-cross-abi.patch, objcrt, branch tiger-ipc-abi): FOURTH offender found:
  Encoder/Decoder pad the wire by alignof(T), which is 4 for 64-bit scalars on i386 Darwin and 8 on x86_64, so every field after
  the first uint64/double shifts. Fixed with a wireAlignmentOf (8-byte scalars aligned to 8 on both sides) plus the requires
  clause banning long/unsigned long/size_t/long double fields, UnixMessage framing fixed-width,
  ScrollSnapOffsetsInfo/PlatformXR fields fixed. Both builds must run to catch every offender (ptrdiff_t is int on i386, so
  it only trips the x86_64 build). Verified by compiling wtf/ArgumentCoder.h for both targets, positive and negative;
  the include roots are compat/sdk-overlay/usr/include then compat/include, and the overlay alone is sufficient.
  UnixMessage.h cannot be syntax-checked standalone (Encoder.h pulls the generated MessageNames.h).
- 64-bit os_log/os_unfair_lock/os_signpost: REVERSED (commits 2c6d1db + 833495c crossed and briefly left no 64-bit impl).
  Final: x86_64 libtigerdispatch.a (os.c ONLY, no dispatch_* since Tiger has no x86_64 CF for the main queue) owns os_*;
  x86_64 libtigercompat.a is 4 members (availability/libcompat/tlv/runtime) and stages no dispatch headers, so a 64-bit
  #include <dispatch/dispatch.h> fails at the include, not at link. 64-bit link rule: -ltigercompat -ltigerdispatch.
  Lesson: a home change is ONE commit touching both makefiles, never two independent ones.
- Do NOT identify archives by md5: ar stores member mtimes, so cmp-identical objects give different checksums. Every md5
  quoted earlier was a snapshot. Verify behaviourally (spike/run64.sh cxx64exc, nm for expected symbols).
- Leopard x86_64 CG hang root-caused (leopard, 2729346): CGBitmapContextCreate -> CGFontDefaultAllowsFontSmoothing ->
  pthread_once -> CGSGetDisplayIsLCD -> mach_msg to Tiger's 32-bit WindowServer, which never replies. Bootstrap-lookup
  interception is not reached (port obtained via direct MIG). Branch stays closed.
- Aqua controls (nscompat): compat/aquacontrols.m + TigerCompat/AquaControls.h; TigerControlStyle state bits match
  WebCore::ControlStyle::State positions; button family blits via HIThemeDrawButton (NSCell ignores key-window state on
  10.4, HITheme takes it as an argument); text/search/progress have no inactive look on 10.4. 540 atlas PNGs + metrics.json.
- Plan restructured (wcplan 116a691): branch (d) primary; UI+render merged 32-bit; DrawControlPart remoting already exists
  (RemoteGraphicsContext.messages.in:124), control cost ~525 LOC + aquacontrols.m; CAHost phase 4 cancelled; top risk is
  now serializer asymmetry (silent wire corruption).
- **Superseded within the hour (23:05): os.c is NOT in the compat archive; link `-ltigerdispatch` instead.** The dispatch
  track built a 64-bit libtigerdispatch.a (`make -C compat/dispatch ARCH=x86_64 install`), which is the better answer and
  removes the reason anyone hand-rolled a compat archive, so the os.c addition above was reverted. The x86_64
  libtigercompat.a is back to its four members (availability.c.o, libcompat.c.o, tlv.c.o, runtime.c.o) and compat no
  longer stages dispatch's headers: its 64-bit install deliberately omits `dispatch/` so a 64-bit TU including
  `<dispatch/dispatch.h>` fails at the include rather than at link time, and compat must not put it back.
  64-bit os_log/os_unfair_lock/os_signpost: `-ltigerdispatch`. dispatch_* does not exist in 64-bit and cannot, since
  Tiger has no x86_64 CoreFoundation and the main queue needs CFRunLoop.
- **Retraction: do not identify these archives by md5.** `ar` stores each member's mtime, so two archives built from
  byte-identical objects have different checksums; I verified that directly (same four member objects, `cmp`-identical,
  two different archive md5s). I had told the deps track to check for md5 dabd0d25, which was wrong advice. The real
  check is behavioural: run `spike/run64.sh spike/cxx64exc.cpp spike/throwlib.cpp`, or call
  `_dyld_find_unwind_sections` on `&main` from a 64-bit C program, where 1 with non-null section pointers is good and
  0 is a compat archive built from pre-fix sources.

- **Final (23:15), superseding both entries above: `compat/libtigercompat-x86_64.a` is the single 64-bit home of
  `compat/dispatch/os.c`.** The dispatch track retired its x86_64 target (its Makefile now says so and refuses to grow
  one back), so `make -C compat ARCH=x86_64 install` builds os.c with that directory's own flags and include tree.
  **64-bit `os_log` / `os_retain` / `os_release` / `os_unfair_lock` / `os_signpost` / `sys/qos` need only
  `-ltigercompat`**, verified on the box including the object lifecycle that the dispatch owner moved out of
  dispatch.c into os.c (it was not self-contained before; an os.c-only link used to fail on `os_release`).
  Members now: availability.c.o, libcompat.c.o, tlv.c.o, cfcompat.c.o, **os.c.o**, runtime.c.o.
  - i386 is unchanged: os.c stays in libtigerdispatch.a there, and the i386 libtigercompat.a has no os.o. Never let
    both archives carry it; if a 64-bit libtigerdispatch is ever revived, drop os.c from the compat Makefile in the
    same commit.
  - compat stages only `dispatch/include/os` and `dispatch/include/sys` into the 64-bit sysroot, never `dispatch/`.
    That is deliberate, and the dispatch track's design: a 64-bit TU including `<dispatch/dispatch.h>` must fail at the
    include, not compile and then die on undefined `dispatch_*` at link time. `dispatch_*` cannot exist in 64-bit,
    since the main queue needs CFRunLoop and Tiger has no x86_64 CoreFoundation. A stale `dispatch/` left in
    sysroot-x86_64 from the hand-rolled era was removed; nothing regenerates it. Verified: the include now fails.
- **[WRONG, see the correction further down; do not act on this entry.]**
  `compat/include/sdk-fill/Availability.h` was **restored** (23:15) from the staged copy in
  `toolchain/sysroot-i386/usr/include/sdk-fill/`, which still had it intact from 19:56. It had been created but never
  committed, so it existed only as the derived copy and vanished from the source tree; it is committed now. It is also
  staged into the **x86_64** sysroot for the first time. This is the second time this class of loss has happened here
  (see the Housekeeping note about six overlay availability headers). **The rule below is still right, the example
  is not**: this particular header was never lost, it had simply moved to the overlay. **A header that only exists
  staged is a header you have already lost**, because `git status` cannot show a file that was never added, so commit
  new files in sdk-fill and the overlay the same day they are written. But before "restoring" one from a sysroot,
  check whether the canonical copy moved: a staged file is evidence that something once built, not that the source
  tree is missing anything.
- MSE demux proven with libavformat (2026-09-20, media64 track, logs/mse-demux.md, spike/msebench.c): **no fMP4 box
  parser needed**, but the obvious design fails. One long-lived AVFormatContext per SourceBuffer over a growing
  append buffer (read callback returning EAGAIN when starved) decodes the first segment and then nothing, for mov
  AND matroska: `mov_switch_root` zeroes `next_root_atom` and resets `found_mdat` *before* parsing the next root
  atom (mov.c:10889), so the first pump at a fragment boundary with the next moof not yet appended destroys the
  demuxer permanently (mov.c:11119). Clearing pb->error/pb->eof_reached is necessary but not sufficient; the damage
  is in the demuxer, not avio. mov also can't open on an init segment alone (it arms next_root_atom only after
  seeing both moov and mdat, mov.c:9549); matroska won't even open without a Cluster.
  What works: a throwaway AVFormatContext per append over `init segment || complete buffered fragments` as a FINITE
  stream, with one long-lived AVCodecContext per track. Decodes play, seek across a discontinuity, and remove, for
  fMP4 H.264+AAC and WebM VP9+Opus; 0.32 ms per open, 5-7 ms per 2 s append (~0.35% of one core), and remove() is
  free because no demuxer state pins old bytes. Timestamps need no fixup: each fragment's baseMediaDecodeTime gives
  absolute PTS whichever segment is handed over.
  **Trap: a truncated mdat parses into garbage packets rather than failing** (4 partial appends per segment without
  scanning: 1 packet, 0 frames). So completeness can't be tested by trial parse; we owe ~120 LOC of top-level box /
  EBML scanning, and the ISO-BMFF cut must be at the last complete **mdat**, not the last complete box (a moof
  without its mdat yields nothing and gets consumed: 0 packets at every split). With the scanner, append
  granularity stops mattering (600/600 frames at 1, 2, 4, 16 and 64 appends per segment).
- **Actually final (23:25), and this one is verified by symbol ownership rather than by a passing test:
  64-bit `os_*` lives in `libtigerdispatch.a`, not in libtigercompat.** compat/dispatch restored its x86_64 target
  (833495c) and the lead accepted that as the end state, so the os.c fold-in recorded above was reverted. Link
  **`-ltigercompat -ltigerdispatch`** for os_log / os_retain / os_release / os_unfair_lock / os_signpost / sys/qos.
  Checked on the box: dispatch's `spike/os64test.c` passes (os_unfair_lock across 4 threads, os_log, os_signpost),
  a lifecycle program (os_log_create + os_retain + os_release) runs, and the same program fails to link without
  `-ltigerdispatch`. x86_64 libtigercompat.a members: availability.c.o, libcompat.c.o, tlv.c.o, cfcompat.c.o, runtime.c.o.
  - **The check that actually settles this is `nm` on both archives, not a program that runs.** For twenty minutes both
    archives carried os.o, and separately there was a window where neither did; in both windows a test could pass or
    fail for the wrong reason. `tiger-nm -g` on each archive for os_log_create, os_unfair_lock_lock and os_release
    gives 0 from libtigercompat and 3 from libtigerdispatch, which is the invariant. Assert ownership, not liveness.
  - Three agents changed this in one hour by independent commits. If it ever moves back to compat it has to be **one
    commit** that adds os.c, stages `dispatch/include/{os,sys}` and retires the 64-bit libtigerdispatch together.
    Never `dispatch/` itself: a 64-bit TU including `<dispatch/dispatch.h>` must fail at the include, because
    `dispatch_*` cannot exist in 64-bit (the main queue needs CFRunLoop; Tiger has no x86_64 CoreFoundation).
- **Correction to my Availability.h entry above: there was nothing to restore, and what I restored was a superseded
  draft.** The canonical file is `compat/sdk-overlay/usr/include/Availability.h`, which is present and is the better
  one: its version constants come from the real AvailabilityVersions.h out of the Xcode 27 SDK, where the sdk-fill
  draft hardcoded them. I had recreated the draft at compat/include/sdk-fill/Availability.h from a staged copy and
  committed it; that is now removed, along with the copy I had newly staged into sysroot-x86_64. The long-standing
  i386 staged copy is left alone. **Two headers with the same name and different content is worse than one missing
  header**, and "it exists in a sysroot" was not evidence that it belonged in the source tree.
- Live NSView hosting over the CA surface (cahost ec5b1f6): works (10.4 ms/frame scrolling 40 controls, 0.7 at rest) and is
  the FALLBACK path only; the chosen path paints controls into page pixels (DrawControlPart + aquacontrols.m). Rules that
  survive either way: NSOpenGLCPSurfaceOrder=-1 + isOpaque NO + NSRectFillUsingOperation(NSCompositeCopy) to punch the hole;
  controls must be SUBVIEWS of the GL view; invalidate old+new rects on setFrame:; Tab traversal is gated on the global
  AppleKeyboardUIMode (host must walk focus itself, which matches WebCore FocusController); popup menus are their own
  windows and need nothing.
- Text input (browsershell bf7a40d): command vocabulary on 10.4 matches WebHTMLView.mm selector names exactly; Ctrl-A/E
  map to ...OfParagraph:, not ...OfLine:. Synthetic NSEvents for arrows need the 0xF7xx PUA glyph in `characters` or
  interpretKeyEvents: emits insertText:"". Dead keys/IME need the real TSM pipeline (CGEventPost), deferred.
- Box state changes by agents (2026-09-21 ~02:55): screensaver idle timer disabled (defaults -currentHost write
  com.apple.screensaver idleTime 0); a stale root-owned crash-report dialog from an earlier CAVideo run was on screen.

## x86_64 curl smoke test on the box (2026-09-20, deps agent)
Ran the x86_64 curl build (deps/build-deps-x86_64.sh: LibreSSL 4.1.0 + zlib 1.3.1 + brotli
1.1.0 + nghttp2 1.65.0, HTTP/2 enabled) against real sites on the Tiger box, using the CA
bundle we ship (deps/src/cacert.pem, scp'd to the box) rather than any system roots.
deps/spike-tests/test_curl_smoke.c: TLS version parsed out of curl's own verbose trace line
("SSL connection using ..."), HTTP version and Content-Encoding via curl_easy_getinfo/
header callback, TTFB via CURLINFO_STARTTRANSFER_TIME. i386 curl was never built with
HTTP/2/brotli (that work predates this task), so there's no i386 delta to report -- this is
new coverage.

| site | TLS (auto) | HTTP | encoding | TTFB | total |
|---|---|---|---|---|---|
| youtube.com | TLSv1.3 / TLS_CHACHA20_POLY1305_SHA256 | HTTP/2 | gzip | 0.169s | 0.285s |
| theverge.com | TLSv1.3 / TLS_CHACHA20_POLY1305_SHA256 | HTTP/2 | br | 0.074s | 0.138s |
| react.dev | TLSv1.3 / TLS_CHACHA20_POLY1305_SHA256 | HTTP/2 | br | 0.156s | 0.175s |

Forced against youtube.com: TLS 1.2 (ECDHE-ECDSA-CHACHA20-POLY1305) succeeds, HTTP/2,
gzip, TTFB 0.154s; TLS 1.3 forced explicitly also succeeds (same as auto). SNI is implicit
in every hostname-based HTTPS connect above (no separate opt-out was set); all three sites
require it for cert selection and all handshakes succeeded, so it's working. No
-ltigerdispatch needed anywhere, only -ltigercompat, same as the rest of the x86_64 curl
build. Link line and test binary: deps/spike-tests/test_curl_smoke.c.
- Addendum to the git rule: **never `git rm` on this tree.** It stages the deletion into the shared index, where the
  next agent's path-scoped commit can sweep it up: my removal of the stray sdk-fill/Availability.h landed inside
  wkcmake's dc61a3c, not in mine, and my own `git commit -o` then failed with "pathspec did not match" because the
  path was already gone. Plain `rm` the file and name it in your own `git commit -o` instead.
- INCIDENT 2026-09-21 ~02:57 (wkcmake): top-level build/ deleted by a cleanup loop (zsh no-word-split on a colon list ->
  empty path). Lost: WebCore fifth-pass tree, LLVM build tree. Kept: toolchain/llvm-tiger install, logs/wc-build.log,
  all sources/patches. Rule: removal scripts validate every path and dry-run print first; nothing new under build/ until
  the fixed script is reviewed.
- Per-process CMake (wkcmake, WebKit b1fc713d / dc61a3c): one shared feature-flag list for all configs; check target
  compares flags by name then hashes generated serializers. Findings: VIDEO/MSE/GPU_PROCESS/WEBASSEMBLY appear in
  serialization conditions so they are all-or-nothing across processes; 55 names are PlatformEnable/PlatformHave
  macros, not CMake options, including USE(CG)/CoreText, so per-side divergence needs the Tiger port header + a
  non-Cocoa options file (next piece). Decisions: WK2 off behind a switch until then; WebKitLegacy off; Render config
  folded into UI.
- MSE demux (media64 27d5bfb/f41223b, logs/mse-demux.md): one long-lived AVFormatContext over a growing buffer FAILS
  (mov demuxer destroys its continuation state at a fragment boundary); WORKS: throwaway format context per append over
  init+complete fragments, persistent decoders per track, 0.1-0.3 ms/open, ~0.35% of a core. Truncated mdat parses to
  garbage, so a ~120-line box/EBML completeness scanner is required; cut at the last complete mdat, not last box.
- x86_64 curl (deps 23d12ca): youtube/theverge/react.dev all TLS 1.3 + HTTP/2 + br/gzip via shipped cacert.pem,
  TTFB 74-169 ms; links with -ltigercompat only.
- Font manifest (ctcompat 9312d6c, logs/tiger-fonts.json, 70 KB): 176 faces, 174 resolvable to (path, face index);
  the 2 unresolvable are Type 1 multiple-master (no sfnt; HarfBuzz cannot use them either). Handle-resolved fonts match
  name-resolved fonts to 0.00000 on glyph IDs/advances/ascent/descent across OpenType/AAT/AAT-Arabic/CJK. Traps: table
  offsets are blob-relative in .dfont but file-absolute in .ttc; 76/176 faces live only in the resource fork; activating
  a font invalidates a live ATSFontIterator; ATS synthesises PostScript names from the FOND for old suitcases, so index
  by ATS activation order, never by name match.
- Archive ownership invariant is checked with nm on BOTH archives (exactly one defines os_log_create), never by a
  program that links: within one hour there was a window where both carried os.o and one where neither did, and a
  running test passed in both.
- build/builtins-i386 (compiler-rt builtins) went with the build/ deletion; i386 C++ spikes cannot link until deps
  restores it (assigned).
