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
