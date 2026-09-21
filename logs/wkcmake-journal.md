# WebKit CMake for Tiger — build journal

Build dir: `build/tiger-jsc`. Reconfigure with:

```
cmake -S WebKit -B build/tiger-jsc -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/tiger.cmake -DTIGER=ON \
  -DCMAKE_OSX_SYSROOT=$PWD/sdk/MacOSX10.4u.sdk \
  -DPORT=Cocoa -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_WEBCORE=OFF -DENABLE_TOOLS=OFF -DDEVELOPER_MODE=OFF
```

`-DTIGER=ON` must be on the command line as well as in the toolchain file:
`Source/cmake/WebKitXcodeSDK.cmake` runs from the root `CMakeLists.txt` *before*
`project()`, so the toolchain file has not been read yet at that point.

Every edit inside `WebKit/` is marked `# TIGER:` or `// TIGER:`.

## Session 1

### Configure-time fixes

| What failed | Fix |
|---|---|
| `WEBKIT_RESOLVE_SDK` runs `xcrun --sdk <path>` then reads `SDKSettings.json`; the 10.4u SDK has only `SDKSettings.plist` | `WebKitXcodeSDK.cmake`: a `if (TIGER)` branch that hardcodes `WEBKIT_SDK_NAME=macosx`, `WEBKIT_SDK_VERSION=10.4`, `USE_APPLE_INTERNAL_SDK=OFF`, `CMAKE_OSX_ARCHITECTURES=i386` |
| `WEBKIT_RESOLVE_TOOL` would pin host clang/ld/swiftc from xcrun over the cross toolchain | same file: under TIGER resolve only the host tools `gperf` and `mig`, then `return()` |
| `find_package(ICU 70.1 REQUIRED)` resolves Tiger's ICU 3.x `libicucore` | `FindICU.cmake`: under TIGER point `ICU_{UC,I18N,DATA}_LIBRARY` at `sysroot-i386/usr/lib/libicu{uc,i18n,data}.a` without `find_library`, so configure works before the deps build lands. Headers still come from `Source/WTF/icu/unicode`, which WebKit copies into `<build>/ICU/Headers` — so the deps ICU has to be ABI-compatible with those bundled headers |
| `find_library(SWIFTCORE_LIBRARY ... REQUIRED)` + `enable_language(Swift)` | `OptionsCocoa.cmake`: `SWIFT_REQUIRED OFF` and the `link_libraries(swiftCore)` skipped under TIGER |
| availability VFS overlay references `WebKitLibraries/AvailabilityOverlay`, absent from the sparse checkout, and the 10.4 SDK has no `os/availability.h` | overlay block gated `AND NOT TIGER` |
| `WEBKIT_ADD_SDK_IMPORTED_LIBRARY(... .tbd)` — the 10.4u SDK ships real dylib stubs, not tbds | under TIGER a replacement function prefers `sysroot-i386/usr/lib/lib*.a` (our newer sqlite/libxml2/libxslt) and falls back to the SDK's dylib |
| `add_subdirectory(ThirdParty/unifdef)` missing | `git sparse-checkout add Source/ThirdParty/unifdef` |
| `add_subdirectory(ThirdParty/ANGLE)` missing | `USE_ANGLE_EGL OFF` under TIGER (the original `set(USE_ANGLE_EGL ON)` sits *after* the TIGER block, so the override had to move into that statement) |
| `find_library(COREGRAPHICS_LIBRARY CoreGraphics)` / `CoreText` NOTFOUND — 10.4 has CG inside ApplicationServices and CoreText only as a private framework | `JavaScriptCore/PlatformCocoa.cmake`: under TIGER link only Security; JSC is a static lib here so nothing is linked at that point anyway |

### ARC interception

WebKit turns ARC on per target with a literal `-fobjc-arc`, in exactly two
places under the ports we build: `Source/WTF/wtf/PlatformCocoa.cmake` (the whole
WTF target) and `Source/JavaScriptCore/shell/PlatformCocoa.cmake` (testapi's
`.mm` files). Both now say `${WEBKIT_OBJC_ARC_OPTIONS}`, set in
`OptionsCocoa.cmake` to `-fobjc-arc` normally and to
`-Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7` under TIGER. The
toolchain file puts `-fobjc-runtime=macosx-fragile-10.4` in
`CMAKE_OBJC{,XX}_FLAGS_INIT` for MRR translation units; the ARC form appends
later on the command line and wins.

`-fobjc-weak` is dropped under TIGER (needs the non-fragile runtime).

### The TIGER option surface

`TIGER` is a plain cache BOOL. Everything it switches:

- `WebKitXcodeSDK.cmake` — SDK description instead of xcrun probing; host-only tool resolution.
- `FindICU.cmake` — static ICU from `sysroot-i386`.
- `OptionsCocoa.cmake` — the feature list below, `SWIFT_REQUIRED OFF`,
  `USE_LIBWEBRTC FALSE`, `USE_ANGLE_EGL OFF`, `ENABLE_WEBKIT OFF`,
  `ENABLE_WEBKIT_LEGACY` follows `ENABLE_WEBCORE`, `ENABLE_WEBINSPECTORUI OFF`,
  all library types STATIC, no `-fobjc-weak`, no `-not_for_dyld_shared_cache`,
  no `-dead_strip_dylibs`, cctools `libtool` for archives, `WEBKIT_OBJC_ARC_OPTIONS`.
- `JavaScriptCore/PlatformCocoa.cmake` — no CoreGraphics/CoreText.

Features forced OFF (all via `WEBKIT_OPTION_DEFAULT_PORT_VALUE(... PRIVATE OFF)`):
JIT/DFG/FTL/WebAssembly(+BBQ/OMG)/SamplingProfiler, GPU_PROCESS, WEBGPU(+SWIFT),
WEBGL, WEB_RTC, MEDIA_STREAM, MEDIA_RECORDER, VIDEO, WEB_AUDIO, WEB_CODECS,
MEDIA_SOURCE, MEDIA_SESSION(+COORDINATOR/PLAYLIST), MEDIA_CONTROLS_CONTEXT_MENUS,
ENCRYPTED_MEDIA, LEGACY_ENCRYPTED_MEDIA, VIDEO_PRESENTATION_MODE,
PICTURE_IN_PICTURE_API, WIRELESS_PLAYBACK_TARGET, AVF_CAPTIONS, DATACUE_VALUE,
AV1, SPEECH_SYNTHESIS, GAMEPAD, WEBXR(+LAYERS), MODEL_ELEMENT, APPLE_PAY,
PAYMENT_REQUEST, PDFKIT_PLUGIN, ASYNC_SCROLLING, ACCESSIBILITY_ISOLATED_TREE,
WEB_AUTHN, WRITING_TOOLS, WK_WEB_EXTENSIONS, OFFSCREEN_CANVAS(+IN_WORKERS),
SERVICE_CONTROLS, TELEPHONE_NUMBER_DETECTION, RESOURCE_USAGE, MEMORY_SAMPLER,
CONTENT_FILTERING, INSPECTOR_TELEMETRY, INSPECTOR_ALTERNATE_DISPATCHERS,
SANDBOX_EXTENSIONS, SHAREABLE_RESOURCE, STREAMING_IPC_IN_LOG_FORWARDING,
VARIATION_FONTS, BACK_FORWARD_LIST_SWIFT, IPC_TESTING_SWIFT, MINIBROWSER,
API_TESTS. `ENABLE_C_LOOP` forced ON.

Note: `WebKitFeatures.cmake` already defaults `ENABLE_JIT=OFF`,
`ENABLE_C_LOOP=ON`, `ENABLE_WEBASSEMBLY=OFF`, `USE_SYSTEM_MALLOC=ON` for
`WTF_CPU_X86`, so the JIT half of that list is belt and braces.

### Compile-time fixes

The toolchain file adds three include roots that everything below depends on:

- `compat/include/sdk-fill` — headers the 10.4u SDK is missing, under their real
  names. Some are new files; some *shadow* an SDK header, pull the real one in
  with `#include_next`, and add what was introduced later. `-isystem` puts this
  dir ahead of the sysroot, which is what makes `#include_next` land on the SDK's
  copy.
- `compat/include` — so `<TigerCompat/*.h>` resolves. These are our own shims,
  named as such, not impersonating SDK headers.
- `compat/dispatch/include` — the libdispatch/os_log polyfill, read in place
  until it is staged into `sysroot-i386`.

Plus `-cxx-isystem sysroot-i386/usr/include/c++/v1` (our libc++, not the SDK's
GCC 4.0 headers) and `-D_LIBCPP_DISABLE_AVAILABILITY` (libc++ otherwise marks
`<filesystem>`, `to_chars` and aligned new unavailable below macOS 10.15, based
on the deployment target; ours is a static libc++ built for this target and has
them), and `-F` for `ApplicationServices.framework/Frameworks` and
`Carbon.framework/Frameworks`, because on Tiger CoreGraphics, CoreText, ATS and
ImageIO are subframeworks and `-F` does not descend into a framework.

One CMake trap worth knowing: `WebKitCompilerFlags.cmake` mirrors its curated
C/CXX flags into plain `CMAKE_OBJC_FLAGS` / `CMAKE_OBJCXX_FLAGS`, and it runs
*before* `OptionsCocoa.cmake` calls `enable_language(OBJC OBJCXX)`. The plain
variables it creates then shadow the cache entries that `_INIT` populates, so
`CMAKE_OBJCXX_FLAGS_INIT` never reaches an ObjC command line. The toolchain file
seeds the plain variables too.

### Compile fixes, by category

**SDK headers that do not exist on Tiger** (new files in `compat/include/sdk-fill`):

| Header | Since | What it provides |
|---|---|---|
| `Availability.h` | 10.5 | `__MAC_*` constants, no-op availability attribute macros, and `__MAC_OS_X_VERSION_MIN_REQUIRED 1040` |
| `os/availability.h` | 10.10 | the `API_*` attribute spellings, all no-ops |
| `mach/vm_page_size.h` | 10.9 | `vm_kernel_page_size` and friends, aliased to Tiger's `vm_page_*` in `<mach/mach_init.h>` |
| `execinfo.h` | 10.5 | `backtrace`, `backtrace_symbols`, `backtrace_symbols_fd` |

Pinning `__MAC_OS_X_VERSION_MIN_REQUIRED` at 1040 is the single highest-leverage
thing in the whole port. Every `#if __MAC_OS_X_VERSION_MIN_REQUIRED >= <modern>`
in `PlatformHave.h` / `PlatformEnable.h` evaluates false, so the modern-SDK code
paths compile out without a single edit. Only the gates keyed purely on
`PLATFORM(MAC)` or `OS(DARWIN)` — which assume any Mac is at least 10.5 — need
attention, and those are collected in one `#if PLATFORM(TIGER)` block at the end
of `PlatformHave.h`.

**SDK headers missing newer declarations** (shadow + `#include_next`):

| Header | What was added |
|---|---|
| `mach/vm_statistics.h` | the `VM_MEMORY_*` allocation tags past 61 (Tiger stops at `DYLD_MALLOC`), with their XNU values |
| `mach/vm_purgable.h` | `VM_PURGABLE_DENY` |
| `mach/task_info.h` | `TASK_VM_INFO`, `struct task_vm_info`, `TASK_VM_INFO_REV*_COUNT` |
| `notify.h` | `notify_register_dispatch` (10.6) |
| `malloc/malloc.h` | `malloc_zone_memalign` (10.6), `malloc_zone_pressure_relief` (10.7) |
| `fcntl.h` | `O_CLOEXEC` (10.5) |
| `uuid/uuid.h` | `uuid_string_t` (10.5) |

`TASK_VM_INFO` compiles but cannot work: Tiger's kernel does not implement that
flavor, so `task_info` returns `KERN_INVALID_ARGUMENT` and the callers already
treat that as "footprint unavailable". Same shape for
`notify_register_dispatch`, stubbed to report failure, so the notifications
WebKit uses it for (memory pressure, time zone changes) never fire.

**Pure libc gaps** added to `compat/include/tigerprelude.h`, which the compiler
wrappers force-include:

- `aligned_alloc` (C11), as a static inline over `posix_memalign`.
- `mach_approximate_time`, `mach_continuous_time`,
  `mach_continuous_approximate_time` (all 10.12), as static inlines over
  `mach_absolute_time`. The approximate ones are exact here; the continuous ones
  do not count time asleep, which is what a Tiger program would have had anyway.
- `#include <sys/qos.h>` under `__has_include`, so `qos_class_t` is visible.
  Tiger's `<pthread.h>` predates it.

**New implementations**, `compat/cfcompat.c` (C) and `compat/nscompat.m` (ObjC),
both picked up by `compat/Makefile`'s `*.c` / `*.m` globs with no Makefile edit:

- `backtrace` / `backtrace_symbols` / `backtrace_symbols_fd` — an i386
  frame-pointer walk plus `dladdr` symbolication. Diagnostics only.
- `notify_register_dispatch`, `malloc_zone_memalign`,
  `malloc_zone_pressure_relief` — see above.
- `CFAutorelease` (10.9) — one line, `[(id)object autorelease]`, which is what
  Apple's implementation is. Declared in `compat/include/TigerCompat/CFCompat.h`.
- `kCFTimeZoneSystemTimeZoneDidChangeNotification` (10.5) — a `CFStringRef`
  constant with the real name. Nothing on Tiger posts it.

**`HAVE_*` gates turned off** in the `#if PLATFORM(TIGER)` block at the end of
`Source/WTF/wtf/PlatformHave.h`:

`HAVE_MACH_EXCEPTIONS` (needs `mach/mach_exc.defs`, 10.5+; Tiger has only
`exc.defs`, so the mig step in `WTF/wtf/PlatformCocoa.cmake` is skipped too),
`HAVE_TIMINGSAFE_BCMP` (10.12; WTF has its own `constantTimeMemcmp`),
`HAVE_STAT_BIRTHTIME` (10.5), `HAVE_MADV_FREE_REUSE` (10.5),
`HAVE_QOS_CLASSES` (`pthread_attr_set_qos_class_np`, 10.10).
`ENABLE_COCOA_WEBM_PLAYER` is zeroed in the matching block at the end of
`PlatformEnableCocoa.h`, since it is on purely because the platform is Cocoa and
it `#error`s without `ENABLE_MEDIA_SOURCE`.

**Per-site `PLATFORM(TIGER)` / `BPLATFORM(TIGER)` guards in WebKit sources.**
`WTF_PLATFORM_TIGER=1` is a global compile definition from the TIGER option, so
`PLATFORM(TIGER)` works with no change to `Platform.h`. bmalloc and libpas do not
include `wtf/Platform.h`, so `BPLATFORM_TIGER` and `PAS_PLATFORM_TIGER` are
mirrored from it in `BPlatform.h` and `pas_platform.h`.

- `bmalloc/VMAllocate.h` — `MADV_FREE_REUSABLE` (10.5) becomes `MADV_FREE`.
- `bmalloc/SystemHeap.cpp` — `shouldUseDefaultMallocZone()` returns true (our
  `memalign` hands out default-zone pointers), `malloc_zone_memalign` becomes
  `aligned_alloc`, `malloc_zone_pressure_relief` becomes nothing.
- `bmalloc/CryptoRandom.cpp` — `CCRandomGenerateBytes` is 10.10, so take the
  `/dev/urandom` path the non-Darwin ports use.
- `libpas/pas_lock.h` — `PAS_USE_ULOCK_FLAGS_API` off.
  `os_unfair_lock_lock_with_flags` is far newer than the polyfill, which stops at
  `os_unfair_lock_lock_with_options`; falls through to plain `os_unfair_lock_lock`.
- `WTF/PlatformRegisters.h` — include `<sys/ucontext.h>` (`struct mcontext` is
  only forward-declared otherwise) and read `uc_mcontext->ss`, which the 10.5 SDK
  renamed to `__ss`.
- `WTF/spi/cocoa/OSLogSPI.h` — skip the `os_log_with_args` redeclaration; the
  polyfill already declares it and the two differ in exception specification.
- `WTF/cf/CFTypeTraits.h` — no `CFError` trait (no `CFErrorRef` on Tiger).
- `WTF/cf/TypeCastsCF.h` — no `CTFontDescriptor` trait, no `<CoreText/...>`.
- `WTF/RetainPtr.h`, `WTF/RetainRef.h` — include `<TigerCompat/CFCompat.h>` for
  `CFAutorelease`.
- `WTF/posix/OSAllocatorPOSIX.cpp` — no `MAP_JIT` (10.7); include
  `<mach/mach_init.h>` for `mach_task_self`.
- `WTF/cocoa/ResourceUsageCocoa.cpp` — include `<unistd.h>` for `_SC_PAGESIZE`.


### Second pass, after the dependencies landed

ICU 76.1, the libdispatch/os_log polyfill and a libc++ rebuilt with
`LIBCXX_ENABLE_FILESYSTEM=ON` all arrived mid-session. What that changed:

- `std::filesystem` works, so `FileSystem.cpp`, `posix/FileSystemPOSIX.cpp` and
  `mac/FileSystemMac.mm` compile.
- ICU works, so `unicode/icu/CollatorICU.cpp`, `text/TextBreakIterator.cpp`,
  `text/StringView.cpp` and the rest of the text layer compile. The static ICU is
  built `--disable-renaming`, so `U_DISABLE_RENAMING=1` is now a global compile
  definition under TIGER — without it the bundled ICU 74 headers would append
  `_74` to every call and nothing in a 76 library would answer.
- The polyfill's `<dispatch/dispatch.h>` needed an `extern "C++"` fix (a member
  template cannot have C linkage, and WebKit includes it from inside
  `WTF_EXTERN_C_BEGIN`). The dispatch agent fixed it.

More headers added to `compat/include/sdk-fill`:

| Header | Since | What it provides |
|---|---|---|
| `mach-o/getsect.h` | 10.6 | `getsegmentdata`, as an inline load-command walk. Tiger has not even `getsegbynamefromheader` to build it from |
| `dlfcn.h` | 10.5 | `RTLD_MAIN_ONLY` |
| `objc/message.h` | 10.5 | header relocation only: the messaging declarations moved out of `<objc/objc-runtime.h>`, and the modern one also pulls in `<objc/runtime.h>` |
| `mach/vm_statistics.h` | — | also `VM_FLAGS_PERMANENT` |

More inlines in `tigerprelude.h`: `pthread_attr_set_qos_class_np`,
`pthread_set_qos_class_self_np`, `pthread_get_qos_class_np` (all 10.10). They
accept the request and do nothing; Tiger has no per-thread QOS. Note
`HAVE_QOS_CLASSES` stays **on**, because `qos_class_t` and
`dispatch_qos_class_t` both exist via the polyfill and `WorkQueueCocoa` needs
`Thread::dispatchQOSClass`; only the pthread setters were missing.

More in `<TigerCompat/CFCompat.h>` + `nscompat.m`: `CFLocaleCopyPreferredLanguages`
(10.5, reads the `AppleLanguages` user default, which is where the real one
reads from), `CFStringCreateWithBytesNoCopy` (10.5, copies and then honours the
`contentsDeallocator` contract), `kCFLocaleCollatorIdentifier` (10.5, an alias
for Tiger's `kCFLocaleCollationIdentifier`).

More `HAVE_*` off: `HAVE_IOSURFACE` (IOSurface.framework is 10.6).

More per-site guards: `TextBreakIterator.h` and
`TextBreakIteratorInternalICUCocoa.cpp` take the ICU backing for every mode
(`TextBreakIteratorCF` needs `CFStringTokenizer` and
`CFStringGetRangeOfCharacterClusterAtIndex`, both 10.5); `RandomDevice.{h,cpp}`
take the `/dev/urandom` path via a file-local `WTF_RANDOM_DEVICE_OS_DARWIN`;
`Entitlements.mm` answers no to every query (Tiger has no code signing, so no
process carries entitlements); `SecuritySPI.h`, `XPCSPI.h`, `IOSurfaceSPI.h`,
`TollFreeBridging.h` and `CFStringSPI.h` drop what the 10.4 SDK cannot back;
`FileSystemMac.mm` skips `kMDItemDownloadedDate`; `ResourceUsageCocoa.cpp` has
no `pages_reusable` accounting.

### A trap: two copies of a shadow header

`compat/Makefile`'s `install` copies `include/.` into
`sysroot-i386/usr/include`, and the compiler wrappers pass that directory with
`-I`, which comes *before* every `-isystem`. Staging `sdk-fill/` flat into it
therefore put a second copy of each shadow header ahead of the first — and since
they share include guards, the `#include_next` chain stopped at the second copy
and the real SDK header was never reached. `malloc/malloc.h` failed with
"unknown type name 'malloc_zone_t'".

`sdk-fill` is therefore reachable **only** through an explicit `-isystem`, which
`toolchain/tiger.cmake` and `compat/Makefile` both pass. It is deliberately not
staged into the sysroot. If another agent wants these headers, add the
`-isystem` rather than copying them.


### The Foundation compatibility layer

`compat/include/TigerCompat/FoundationCompat.h` + `compat/foundationcompat.m`.
The toolchain file force-includes the header into every Objective-C translation
unit (`-include` in `CMAKE_OBJC{,XX}_FLAGS`), so WebKit sources need no include
of their own for any of it. What it supplies:

- `NSInteger` / `NSUInteger` (10.5).
- `NSUUID` (10.8), over `uuid_generate` / `uuid_parse` / `uuid_unparse`.
- `NSFileCoordinator` (10.7) and `NSFileCoordinatorReadingOptions`. It exists to
  arbitrate file access between processes and with iCloud, neither of which
  Tiger has, so the coordinated block runs against the URL it was given.
- `NSFileManagerDelegate` (10.5), declaration only; Tiger uses informal delegate
  categories.
- `NSURLIsExcludedFromBackupKey` (10.7) and `-[NSURL setResourceValue:forKey:error:]`
  (10.6), which reports success without doing anything. Tiger has no Time Machine.
- Categories: `-[NSArray firstObject]` (10.6),
  `-[NSArray objectAtIndexedSubscript:]` and the mutable and dictionary
  subscripting methods (10.6/10.8),
  `-[NSDictionary enumerateKeysAndObjectsUsingBlock:]` (10.6),
  `-[NSData initWithBytesNoCopy:length:deallocator:]` (10.6),
  `-[NSFileManager contentsOfDirectoryAtPath:error:]` (10.5),
  `+[NSURL fileURLWithPath:isDirectory:]` (10.5),
  `+[NSLocale localeWithLocaleIdentifier:]` (10.6) and the `languageCode` /
  `scriptCode` / `countryCode` accessors (10.12).
- `NSFileWriteFileExistsError` (10.5).

Two details worth remembering.

**`YES` has to be redefined.** Tiger's `<objc/objc.h>` defines `YES` as
`(BOOL)1`. Clang expands `@YES` to `@` followed by the macro, which then parses
as the boxed expression `@(BOOL)1` and fails — so *every* Objective-C literal
`@YES` / `@NO` in WebKit is a syntax error against this SDK. The 10.8+ SDK
defines `YES` as `__objc_yes` for exactly this reason, and FoundationCompat.h
does the same before Foundation is parsed.

**The `NSLocale` accessors must be `@property`, not methods.** WebKit reaches
them with dot syntax on an `id`, and dot syntax only resolves through a declared
property. `+localeWithLocaleIdentifier:` likewise returns `instancetype`, not
`id`, or the `RetainPtr` it feeds deduces to `id` and the dot syntax fails
again.

**What the layer cannot fix: lightweight generics.** `NSArray<NSString *> *`
does not parse, because the 10.4 `NSArray` is not declared with a `__covariant`
type parameter and a category cannot add one. In WTF this hit only two files, so
they spell it `NSArray` under `PLATFORM(TIGER)`
(`spi/cocoa/NSLocaleSPI.h`, `cocoa/LanguageCocoa.mm`). WebCore will hit it in
far more places, and at that point the right fix is to add the type parameters
to `NSArray`, `NSDictionary`, `NSSet` and `NSOrderedSet` in our local copy of
the SDK's Foundation headers — about six edits that unblock every site at once.
That needs a decision, since it means patching `sdk/`.

### Another shared-tree trap

`compat/Makefile` compiles with `-isystem include`, but the compiler wrappers
pass `-I$(PREFIX)/include`, and `-I` is searched before `-isystem`. A source
file edited in `compat/` was therefore compiled against the *previously
installed* copy of its own header. The Makefile now has an `install-headers`
target that every object depends on, so the staged headers are refreshed before
anything is compiled.


### JavaScriptCore

After WTF, JSC needed six more things.

**Missing SDK headers**, added to `compat/include/sdk-fill`:

| Header | Since | Notes |
|---|---|---|
| `libkern/OSCacheControl.h` | 10.5 | `sys_icache_invalidate` / `sys_dcache_flush`, no-ops. On i386 the caches are coherent in hardware, which is what Apple's i386 implementation does too, and nothing here JITs |

**`memset_pattern4/8/16`** (10.5) as inlines in `tigerprelude.h`.

**`MachineContext.h` had no 32-bit x86.** Six `#error Unknown Architecture`
under `OS(DARWIN)`, because WebKit dropped 32-bit x86 years ago. Added a
`PLATFORM(TIGER)` branch to each, reading `esp` / `ebp` / `eip` / `ebx` from
`i386_thread_state_t`. The two `argumentPointer` accessors have no honest
answer — i386 passes arguments on the stack — so they return `eax` and `edx`;
only the WebAssembly fault handler reads them and `ENABLE_WEBASSEMBLY` is off.
Also, `mcontext`'s members were renamed to `__es`/`__ss`/`__fs` in the 10.5 SDK,
so eight `machineContext->__ss` sites go through a `TIGER_MCONTEXT_SS` macro.

**The Objective-C JavaScriptCore API is off.** `JSC_OBJC_API_ENABLED` is 0 under
TIGER, in both `API/JSBase.h` and `runtime/VM.h` (the definition is duplicated
there). `Source/JavaScriptCore/SourcesTiger.txt` replaces `SourcesCocoa.txt` and
keeps only `JSStringRefCF.cpp` and the two crash-reporting shims. Two reasons:
`JSContext`, `JSValue`, `JSVirtualMachine`, `JSManagedValue`, `JSWrapperMap` and
`ObjCCallbackFunction` all declare instance variables inside `@implementation`,
which the fragile runtime forbids, and `JSVirtualMachine` needs `NSMapTable` as
a class. The Foundation compat layer has since grown an `NSMapTable`, so the
remaining work to restore the API is moving each class's ivars into its
`@interface`. None of it is needed for the C API, the `jsc` shell, or
WebKitLegacy. `ENABLE_REMOTE_INSPECTOR` is off as well, which makes the three
remote-inspector Cocoa files compile to nothing (their contents are entirely
inside `#if ENABLE(REMOTE_INSPECTOR)`), and `API/JSRemoteInspector.cpp` is out
of the source list because it references `RemoteInspector` unconditionally.

**Link libraries.** `-stdlib=libc++` only adds libc++ itself, so every binary
now links, in dependency order, `libc++abi.a`, `libunwind.a`,
`libtigerdispatch.a`, `libtigercompat.a`, `-lobjc`, Foundation and
CoreFoundation. Set with `link_libraries()` in the TIGER branch of
`OptionsCocoa.cmake`.

**libSystem entry points that had to be implemented**, in `compat/cfcompat.c`:

- `dyld_image_header_containing_address` (10.6), over `dladdr`: `dli_fbase` is
  the Mach-O header of the containing image.
- `_dyld_get_image_uuid` (10.6), by walking load commands for `LC_UUID`, which
  predates Tiger, so this one is real rather than a stub.
- `_dyld_get_shared_cache_uuid`, `dyld_shared_cache_file_path`,
  `_dyld_get_dlopen_image_header` — Tiger has no shared cache and no handle-to-
  header mapping, so these report nothing.
- `dyld_get_program_sdk_version` (10.10), returning 10.4.0 packed.
- `cache_simulate_memory_warning_event` — no unified cache layer to notify.

`abort_with_reason` (10.11) is not implemented: `wtf/spi/darwin/ReasonSPI.h`
already has a `CRASH()` fallback for non-Cocoa platforms, so TIGER takes that
branch.

### Working alongside the other compat tracks

Two files in `compat/` changed hands mid-session. `nscompat.m` was rewritten and
greatly expanded by the ObjC-runtime track, with a new
`<TigerCompat/NSCompat.h>` covering NSInteger, NSUUID, NSMapTable, fast
enumeration, subscripting and blocks-based enumeration. The Foundation work
started here was folded into that, and
`<TigerCompat/FoundationCompat.h>` is now just the *prelude*: the annotation
macros and the `YES`/`NO` spelling that have to be defined before Foundation is
parsed, plus an `#import` of NSCompat.h and the three things it does not cover
(`NSFileManagerDelegate`, `NSFileCoordinator`, and `NSLocale`'s accessors
redeclared as `@property`, since WebKit reaches them with dot syntax on an
`id`). `compat/foundationcompat.m` holds only `NSFileCoordinator` and the
CoreFoundation entry points declared in `<TigerCompat/CFCompat.h>`.

Worth knowing about the `YES` fix: Tiger's `<objc/objc.h>` defines `YES` as
`(BOOL)1`, and clang expands `@YES` to `@` followed by the macro, which parses
as the boxed expression `@(BOOL)1` and fails. Every `@YES` and `@NO` in WebKit
is a syntax error against this SDK until `YES` is `__objc_yes`, which is what
the 10.8+ SDK does and what the prelude does now.


## Session 2

`jsc` runs on the Tiger box. `/tmp/jsc -e 'print(1+1)'` prints 2 and exits 0.

Three things stood between the binary that linked at the end of session 1 and
one that works, and none of them was where the symptoms pointed.

### 1. The SDK overlay

`compat/sdk-overlay/`, put ahead of the 10.4u SDK by `toolchain/tiger.cmake`
(`-isystem` for `usr/include`, `-F` for the frameworks). The 10.4u SDK itself is
never edited. `compat/sdk-overlay/README.md` records every file and why.

A framework binds by name: once clang resolves `Foundation` to the overlay,
every `<Foundation/*.h>` must be found there — it does **not** fall through to
the next `-F`. Verified, not assumed. So `make-overlay.sh` builds each overlaid
framework's `Headers` as symlinks into the SDK and never touches a real file:
a symlink is untouched, a real file is ours.

What is overlaid:

- **Availability.** `AvailabilityVersions.h` copied verbatim from the Xcode 27
  SDK (self-contained, gives every `__MAC_xx` its true value), plus our own
  `Availability.h` and `os/availability.h` that pin
  `__MAC_OS_X_VERSION_MIN_REQUIRED` at 1040 and make the availability
  *attributes* no-ops. The attributes have to be no-ops: marking a declaration
  introduced in macOS 13 at a 10.4 deployment target makes every call to it a
  hard error. Upstream WebKit has the same problem against any non-internal SDK
  and solves it the same way, with `WebKitLibraries/AvailabilityOverlay`.
- **Foundation generics.** `NSArray.h`, `NSDictionary.h`, `NSSet.h`,
  `NSEnumerator.h` gain lightweight generics. Only the `@interface` lines change:
  the type parameter is declared on the class and repeated on each category,
  which is what clang requires. Method signatures keep their `id` types, since
  `id` converts both ways — the parameter's job is to let the specialization
  parse and be checked at the use site. The two WTF workarounds from session 1
  are reverted.
- **`NSMapTable.h`.** The 10.4 header typedefs the opaque C struct to the same
  name as the class Foundation gained in 10.5. The typedef is renamed to
  `NSMapTableCStruct`, the C functions are rewritten to it by a macro that is
  `#undef`'d at the end of the header, and `TIGER_NSMAPTABLE_TYPEDEF_RENAMED`
  tells `<TigerCompat/NSCompat.h>` it may declare the class.
- **`NSNetServices.h`.** `id * _reserved;` is "pointer to non-const type 'id'
  with no explicit ownership" under ARC, a hard error, and the Cocoa port
  compiles WebKit with ARC. It is the only `id *` ivar in the 10.4 Foundation.
- **`CFError.h`** (new, opaque `CFErrorRef` only) and `CoreFoundation.h` (the
  SDK's plus that include), **`CGBase.h`** (the SDK's plus `CGFloat`). Both were
  being defined locally by the CT and CG compat headers; the overlay is their
  proper home.
- **`AssertMacros.h`.** See below.

Not overlaid, deliberately: CoreText, because the 10.5 prototypes are ABI-wrong
against Tiger's binary (`compat/CT-SURVEY.md`); Foundation *runtime* gaps, which
are declarations plus implementations and belong to `<TigerCompat/NSCompat.h>`.

### 2. Carbon's namespace, and how it reaches JavaScriptCore

Once the CoreGraphics hook headers landed in the overlay, `<CoreGraphics/*.h>`
began pulling `<TigerCompat/CGCompat.h>` → `<ApplicationServices/...>` →
CoreServices → CarbonCore into WTF and JavaScriptCore, and CarbonCore is full of
unprefixed names that collide with ordinary C++:

- `check`, `verify`, `require` and 29 more macros from `AssertMacros.h`.
  JavaScriptCore's `bytecode/Fits.h` has `static bool check(T)`. Fixed properly:
  the overlay's `AssertMacros.h` includes the real one and then honours Apple's
  later `__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES` opt-out, which
  `tiger.cmake` passes as 0.
- `Marker`, a CarbonCore typedef, against JavaScriptCore's own `Marker` in
  `API/JSMarkingConstraintPrivate.cpp`. A macro opt-out cannot fix a typedef.

Nothing below WebCore needs a CoreGraphics type, so CG is now gated out of WTF
entirely: the CG traits in `wtf/cf/CFTypeTraits.h` and the `CGRect`/`CGSize`/
`CGPoint` stream operators in `wtf/text/TextStream.h` and `TextStreamCocoa.mm`.
That is a stopgap for the WebCore phase, where those hooks are the whole point;
the cgcompat agent has been asked to narrow `<TigerCompat/CGCompat.h>` so it
does not reach the ApplicationServices umbrella.

### 3. Why jsc crashed, and why it then ran out of memory

**The collector's dangling references were misaligned MarkedBlocks.**
`MarkedBlock::blockFor()` finds a block's footer by masking a cell pointer with
`~(blockSize - 1)`, and `blockSize` is 16 KB. Every MarkedBlock comes from
`fastAlignedMalloc` → `bmalloc::api::memalign` → `aligned_alloc`, and on Tiger
that was the inline in `tigerprelude.h` over `libcompat.c`'s `posix_memalign`,
whose emulation tops out at `valloc`'s page alignment. Measured on the box:
`posix_memalign(16384, 16384)` returns 4 KB-aligned addresses, eight times out
of eight. So the collector was reading the footer of whatever sat below.

`compat/cfcompat.c` now has a real `aligned_alloc`. Tiger has no aligned
allocator to borrow, and Apple's lives inside the malloc zone, which Tiger's
does not expose, so the block is over-allocated and an aligned interior pointer
returned with the base stored just below it. That makes the result not
`free()`-able, so the pair is closed by `tiger_aligned_free()`, which bmalloc's
`free()` calls under `BPLATFORM(TIGER)` — the one seam every WebKit aligned
allocation and free passes through. `malloc_size()` returning 0 for an interior
pointer is what tells the two kinds of pointer apart. A stress test on the box
(five alignments from 16 bytes to 64 KB, 200 rounds each, then 2000 rounds
interleaved with plain malloc) shows no nulls, no misalignment, and no interior
pointer ever mistaken for a block start.

**Then arrays failed at about a thousand elements** with `RangeError: Out of
memory`, while plain objects, strings and typed arrays were fine. The shape of
that is the large-object path: a butterfly crosses `MarkedSpace::largeCutoff` at
around 8 KB, which for `JSVALUE32_64` is a thousand elements.

Instrumenting `CompleteSubspace::tryAllocateSlow` gave it away in one line:

```
TIGERDBG tryAllocateSlow: heap cap 549810 > multiple 2 * ramSize 0
```

`WTF::ramSize()` was **0**, so every allocation past the small-object path was
refused. This is a genuine 32-bit overflow in WebKit, not a Tiger gap. The box
has 6 GB; `host_info` reports `max_mem` as 6442450944 (`max_mem` is 64-bit even
in the 10.4 headers); `memorySizeAccordingToKernel()` sees that it exceeds
`size_t` and clamps to `SIZE_MAX`; and `computeAvailableMemory()` then does

```c
return ((sizeAccordingToKernel + multiple - 1) / multiple) * multiple;
```

with `multiple` = 128 MB, which overflows 32-bit `size_t` and lands on zero.
`AvailableMemory.cpp` now rounds *down* when rounding up cannot be represented.

Worth reporting upstream: any 32-bit WebKit build on a machine with more RAM
than `size_t` can address hits this.

### Smaller fixes

- `libkern/OSCacheControl.h`, `memset_pattern4/8/16` and `sys_icache_invalidate`
  are all **exported by Tiger's libSystem** and were only missing declarations.
  The inline reimplementations written in session 1 were shadowing the real
  ones and are gone. Checked the same way for every other shim:
  `notify_get_state`, `mkstemps`, `uuid_generate`/`uuid_parse`/`uuid_unparse`
  and `malloc_size` all exist; `backtrace`, `getsegmentdata`,
  `notify_register_dispatch`, `malloc_zone_memalign`,
  `malloc_zone_pressure_relief`, the `dyld_*` introspection calls,
  `abort_with_reason`, `mkostemp`/`mkostemps`, `posix_memalign`,
  `aligned_alloc`, `timingsafe_bcmp` and the pthread QOS setters do not.
- `CFStringCreateWithBytesNoCopy` and `CFStringGetRangeOfCharacterClusterAtIndex`
  are in Tiger's CoreFoundation binary but not its headers, so
  `<TigerCompat/CFCompat.h>` declares the real functions rather than shimming
  them. `CFLocaleCopyPreferredLanguages` is genuinely absent and is implemented
  in `cfcompat.c` the way CF-550 does it, over the `AppleLanguages` preference.
- `compat/foundationcompat.m` is deleted. It duplicated `NSUUID` and four
  categories with the nscompat agent's `nscompat.m`, which made
  `libtigercompat.a` unlinkable. `<TigerCompat/FoundationCompat.h>` survives as
  the force-include: the `NS_*` annotation macros, the `YES`/`NO` redefinition,
  `NSFileManagerDelegate`, `NSLocale`'s accessors as `@property`, and an import
  of `<TigerCompat/NSCompat.h>`.
- `NSFileCoordinator` is gone with it. Its only use in WTF is
  `createTemporaryZipArchive`, and on Tiger there is nothing to coordinate with,
  so that call site runs the accessor block directly.
- ICU is built `--disable-renaming`, so `U_DISABLE_RENAMING=1` is a global
  compile definition under TIGER. Without it the bundled ICU 74 headers append
  `_74` to every call and nothing in the 76 library answers.

**Why `YES` has to be redefined.** Tiger's `<objc/objc.h>` defines `YES` as
`(BOOL)1`. Clang expands `@YES` to `@` followed by the macro, which parses as
the boxed expression `@(BOOL)1` and fails — so every `@YES` and `@NO` in WebKit
is a syntax error against this SDK until `YES` is `__objc_yes`, which is what
the 10.8+ SDK does and what the prelude now does.

### Two traps for anyone else working in compat/

**`sdk-fill` must only ever be reached through an explicit `-isystem`.**
`compat/Makefile`'s `install` copies `include/.` into the sysroot, and the
compiler wrappers pass that directory with `-I`, which comes before every
`-isystem`. Staging `sdk-fill/` flat into it put a second copy of each shadow
header ahead of the first, and since they share include guards the
`#include_next` chain stopped at the second copy and the real SDK header was
never reached. `malloc/malloc.h` failed with "unknown type name
'malloc_zone_t'".

**Editing a compat header does nothing until `make -C compat install` runs**,
for the same reason: the wrapper's `-I` of the staged sysroot outranks the
source tree. The Makefile now has an `install-headers` target that every object
depends on, so a source file is never compiled against the previously installed
copy of its own header.


## Session 2, part 2: after the platform caught up

Four things landed from other tracks and changed the answers above.

**`posix_memalign` is real now.** The audit agent rewrote `compat/libcompat.c`'s
version: malloc at or below 16 bytes of alignment, valloc up to a page, and
above that an mmap'd region registered as a malloc zone so plain `free()` still
finds it — the same mechanism Apple's libmalloc uses. That makes the
over-allocate-and-offset `aligned_alloc` written earlier in this session
unnecessary, and worse than the platform's, because it needed its own free. It
is gone: `aligned_alloc` is back to an inline over `posix_memalign` in
`tigerprelude.h`, `tiger_aligned_free` is deleted, and bmalloc's `free()` is
back to plain `::free`.

One consequence had to be handled. `SystemHeap::free` called
`malloc_zone_free(m_zone, ...)` with `m_zone` forced to the default zone on
Tiger, which would not free a block that came from the new aligned zone. Under
`BPLATFORM(TIGER)` it now calls plain `free()`, which consults every registered
zone. Nothing routes large aligned requests through `SystemHeap` today, but the
mismatch was latent.

**The CoreGraphics hooks were narrowed**, so the CarbonCore namespace no longer
reaches JavaScriptCore through them. `<TigerCompat/CGCompat.h>` was including
the CoreGraphics umbrella, which reaches CGEvent and CGPSConverter and through
them all of CoreServices; the piece that was not obvious is that
`<ImageIO/CGImageSource.h>` includes the umbrella too. It names only the ten
sub-headers it needs now, and an include trace shows zero CoreServices hits.
The three gates added earlier are reverted: the CG traits in
`wtf/cf/CFTypeTraits.h` and the `CGRect`/`CGSize`/`CGPoint` stream operators in
`wtf/text/TextStream.h` and `TextStreamCocoa.mm` are back.

The `AssertMacros.h` overlay stays. It is still needed by anything that includes
the CoreGraphics umbrella, which pulls CoreServices by the SDK's own design.

**`libtigercompat.a` must be linked with `-Wl,-ObjC`.** Nearly all of its
Foundation and AppKit surface is categories, and a static archive member is only
pulled in when it defines a referenced symbol — a category defines none. Without
`-ObjC` the member never joins the link and every category method is missing at
runtime, from a build that said nothing. `-ObjC` also drags in
`NSOperationQueue`, so `libtigerdispatch` and the Foundation, AppKit and
ApplicationServices frameworks all become required, even for a binary that never
mentions a queue, a window or a colour. All of that is in the TIGER
`link_libraries()` block.

**`-Wno-deprecated-anon-enum-enum-conversion`** is now a global compile option.
Tiger spells the CGBitmapInfo constants as separate anonymous enums and WebCore
combines them; C++20 deprecated a bitwise operation between different
enumeration types, so at C++23 with `-Werror` every such site fails. Too many
call sites to patch.

### The overlay's availability headers were overwritten, and restored

Another track replaced `Availability.h`, `os/availability.h` and four more with
verbatim Xcode 27 copies. Those have the *real* availability attributes, which
is exactly what must not happen here: at a 10.4 deployment target every
`API_AVAILABLE(macos(13.0))` declaration WebKit makes becomes a
`-Wunguarded-availability-new` diagnostic on use, fatal under `-Werror` and
unreadable without it.

Restored, and the reasoning is now written into the header itself so it does not
happen again. The overlay keeps `AvailabilityVersions.h` verbatim (self-contained,
real constants) and our own no-op-attribute `Availability.h` and
`os/availability.h`. `AvailabilityMacros.h`, `AvailabilityInternal.h` and
`AvailabilityInternalLegacy.h` are deliberately **not** overlaid: the 10.4u SDK's
own `AvailabilityMacros.h` is what its headers were written against, and it
defines every `AVAILABLE_MAC_OS_X_VERSION_10_x_AND_LATER` they use.

Verified with a translation unit that declares
`void modernThing(void) API_AVAILABLE(macos(13.0));` and calls it: clean at
`-Wall -Werror`, with `__MAC_OS_X_VERSION_MIN_REQUIRED` 1040 and `__MAC_10_15`
and `__MAC_26_0` at their true values.

## Where it stands

Everything reproduces from a clean configure and build.

| Target | State |
|---|---|
| `bmalloc` | builds, `libbmalloc.a` |
| `WTF` | builds, all objects, `libWTF.a` |
| `JavaScriptCore` | builds, `libJavaScriptCore.a` |
| `jsc` | links, 51 MB, i386, **runs on Mac OS X 10.4.11** |

Committed in the WebKit checkout as `8aa8957e`, 614 added lines across 48 files,
every hunk marked `TIGER`.

### What runs on the box

`/tmp/jsc -e 'print(1+1)'` prints 2, exit 0.

| Area | Result |
|---|---|
| JSON | round trip exact; 5000 objects to 132 KB and back |
| RegExp | capture groups, global match, function replace, **named groups** |
| Closures | 1000 independent counters, 10000 calls |
| Date | `toISOString`, `toUTCString` correct |
| `Intl.DateTimeFormat` | `Sep 20, 2026` |
| `Intl.NumberFormat` (de-DE) | `1.234.567,891` |
| `Intl.Collator` (de) | sorts `a, ä, z` |
| Unicode | NFC/NFD normalization; Turkish `istanbul` uppercases to `İSTANBUL` |
| ES6+ | classes, arrow functions, template literals, `Map` |
| GC | 40 rounds of 3000 short-lived objects, 20000-entry Map, exit 0 |

So ICU is fully live, including collation and locale-sensitive casing, not just
the basic string paths.

### Timing, C loop interpreter, 2.2 GHz Core 2 Duo

| Benchmark | Time |
|---|---|
| 3,000,000-iteration arithmetic loop | 1753 ms |
| `fib(25)` | 135 ms |
| Building a 400 KB string, 50000 concatenations | 67 ms |

About 1.7 million loop iterations per second. That is the C loop with no JIT, so
it is the floor rather than a ceiling, and it is entirely usable for a browser's
scripting.

### Still off

- The Objective-C JavaScriptCore API (`JSC_OBJC_API_ENABLED` 0,
  `SourcesTiger.txt`) and `ENABLE_REMOTE_INSPECTOR`. The patched clang now
  permits instance variables in `@implementation` on the fragile ABI, so the
  reason for the first is gone; revisit at WebKitLegacy, which needs
  `-[WebFrame javaScriptContext]`.
- `WEBKIT_OBJC_ARC_OPTIONS` still uses the `-Xclang -fobjc-arc` form. The
  patched clang accepts the plain driver flag now, which would also turn on
  `-fobjc-arc-exceptions` for ObjC++. Not changed mid-session; worth doing when
  there is a reason to rebuild everything anyway.
- `jsc` prints `_NSAutoreleaseNoPool` warnings at startup. Cosmetic, but those
  objects leak.

## Round 3 consolidation

The stopgap aligned allocator is gone and the platform's is proven. With
`libcompat.c`'s real `posix_memalign` (malloc at or below 16 bytes, valloc to a
page, above that an mmap'd region registered as a malloc zone so plain `free()`
finds it), `aligned_alloc` is back to a plain inline over it in
`tigerprelude.h`, `tiger_aligned_free` is deleted, and bmalloc's `free()` is
back to `::free`. Note `libcompat.c` does **not** itself provide `aligned_alloc`;
the inline in the prelude is still where it comes from.

One related fix stays: `SystemHeap::free` called
`malloc_zone_free(m_zone, ...)` with `m_zone` forced to the default zone on
Tiger, which would not free a block from the new aligned zone. It calls plain
`free()` under `BPLATFORM(TIGER)`, which consults every registered zone.

Proven from a clean build, all on the box, all exit 0: the smoke test, the
allocation-shape test, the GC stress run, and the round-2 script set.

CoreGraphics is un-gated in WTF and JavaScriptCore again, now that the CG hooks
no longer reach the ApplicationServices umbrella. The `AssertMacros.h` overlay
stays, because anything that includes the CoreGraphics *umbrella* still pulls
CoreServices by the SDK's own design.

### Performance against the 2007 baseline

The JS timing loop from `spike/TigerBrowser/testpages/script.html`, verbatim,
under our `jsc` on the box:

| Run | Time |
|---|---|
| 1 | 2230 ms |
| 2 | 2236 ms |
| 3 | 2259 ms |

Against the 5.3 s baseline of Tiger's own 2007 JIT-less JavaScriptCore, that is
**about 2.4x faster** — with the C loop interpreter and no JIT at all.

### Upstream-worthy bugs found

Two changes in this port are not Tiger-specific and are bugs in current WebKit:

1. **`AvailableMemory.cpp` returns a RAM size of zero on a 32-bit build of a
   machine with more RAM than `size_t` can hold.** `memorySizeAccordingToKernel()`
   clamps to `SIZE_MAX`, then `computeAvailableMemory()` rounds up to a 128 MB
   multiple, which overflows. A zero `ramSize()` makes `CompleteSubspace` refuse
   every large allocation; it presents as `RangeError: Out of memory` from
   `Array.prototype.push` at about a thousand elements. Reproducible on any
   32-bit host with more than 4 GB.

2. **`API/JSRemoteInspector.cpp` does not build with `ENABLE(REMOTE_INSPECTOR)`
   off on a Cocoa platform.** Every other `RemoteInspector` use in the file is
   guarded; `defaultStateForRemoteInspectionEnabledByDefault()` guards its own
   only with `PLATFORM(COCOA)`.

Neither needs Tiger to reproduce.

### The Objective-C JavaScriptCore API: ready, waiting on two shims

The patched clang accepts `@implementation` instance variables and property
auto-synthesis on the fragile runtime, so the original reason for disabling the
API is gone. Turning it on now leaves exactly two gaps, both outside the WebKit
tree and both asked for:

- `objc_storeWeak` and `objc_loadWeak` are implemented in `compat/arc.m` but
  declared nowhere, and `wtf/WeakObjCPtr.h` calls them from a non-ARC
  translation unit.
- The `NSMapTable` C functions (`NSMapGet`, `NSMapInsert`, `NSMapRemove`,
  `NSFreeMapTable`, the enumerator pair) take Tiger's C struct, but
  `JSManagedValue.mm` and `JSWrapperMap.mm` pass the class, which is what modern
  Foundation supports.

`SourcesTiger.txt` says so at the top and is a delete-me file: remove it and
restore `SourcesCocoa.txt` in `PlatformCocoa.cmake` when both land.
`JSRemoteInspector.cpp`'s guard fix is kept either way, since it is needed
whenever the remote inspector is off.

Not stripping local symbols matters now: objcrt's JSExport support recovers
protocol ext records through clang's `__OBJC_PROTOCOLEXT_<Name>` local symbols.
Nothing in `tiger.cmake` strips, and the only strip-adjacent link flag is
`-dead_strip`, which removes unreferenced atoms rather than symbol-table
entries. Worth re-checking on the first JSExport test.

## Round 3, part 2: WebCore

M1 is done — WebCore configures with the plan's feature switches — and the M2
compile pass is running. What follows is what it took, because most of it was
not in the plan.

### The feature switches (plan §1)

`USE_CA` and `USE_CORE_IMAGE` are off, done at their definition sites in
`wtf/PlatformUse.h` rather than in a trailing block, because those two decide
which source files compile at all. The IOSurface and Core Animation `HAVE_`
satellites, plus `HAVE_CORE_TEXT_SBIX_IMAGE_SIZE_FUNCTIONS`,
`HAVE_CTFONTMANAGER_CREATEMEMORYSAFEFONTDESCRIPTORFROMDATA` and
`HAVE_LOCKDOWN_MODE_PDF_ADDITIONS`, are off in the `PLATFORM(TIGER)` block of
`PlatformHave.h`. `ENABLE_DATA_DETECTION`, `ENABLE_NOTIFICATION_EVENT` and
`ENABLE_DECLARATIVE_WEB_PUSH` are zeroed in `PlatformEnableCocoa.h` — none has a
CMake option, and the last two exist only because `PlatformEnable.h` `#error`s
when a feature outlives what it depends on. `ATTACHMENT_ELEMENT`,
`FULLSCREEN_API`, `NOTIFICATIONS`, `GEOLOCATION` and `DEVICE_ORIENTATION` joined
the CMake off-list.

### Excluding sources without touching the Sources lists

`WebKitMacros.cmake` already has a `<framework>_UNIFIED_SOURCE_EXCLUDES`
mechanism: a list of regexes that filters the `Sources*.txt` files before the
unified bundles are generated. `WEBKIT_COMPUTE_SOURCES` runs after
`PlatformCocoa.cmake` is included, so setting it there is in time.

That means excluding a whole directory needs no edit to any `.txt` file, which
keeps the diff against upstream much smaller than the plan assumed. Used for:

- All of `platform/graphics/ca` and the IOSurface backends — about 10,900 lines.
  `platform/graphics/tiger/GraphicsLayerTiger.cpp`, 60 lines, supplies the one
  `GraphicsLayer::create` definition `GraphicsLayerCA.cpp` owned. It asserts if
  it is ever reached, which it cannot be: with `USE(CA)` off,
  `RenderLayerCompositor` never sets `m_hasAcceleratedCompositing`, so
  `canBeComposited()` is false and no `GraphicsLayer` is constructed.
- 30-odd PAL soft-link files for frameworks that do not exist in the 10.4 SDK.
  Soft-linking still needs the framework's *headers* at compile time — only the
  load is deferred — so ARKit, AVFoundation, AVKit, Contacts, CoreML, PassKit,
  Vision, ScreenTime, WritingTools and the rest all have to go.

### Four configure-time problems the plan did not mention

1. **Eleven `find_library` results are NOTFOUND on Tiger**, and a NOTFOUND
   reaching a link list is a hard generate-time error. Cleared in one loop
   rather than by editing each entry out of `WebCore_LIBRARIES`. CFNetwork is
   the exception: Tiger has it, inside `CoreServices.framework/Frameworks`,
   where `-F` does not descend, so it is found explicitly.
2. **The OpenGL block** at `WebCore/CMakeLists.txt:2268` is not gated on
   `ENABLE_WEBGL`, and with `USE_ANGLE_EGL` and `USE_LIBEPOXY` both off it falls
   through to an `OpenGL::GLES` target no find module defines here.
3. **WebMParser** is built whenever `USE_LIBWEBRTC` is off, and
   `Source/ThirdParty/libwebrtc` is not in the sparse checkout, so the glob
   finds nothing and `add_library` fails.
4. **The `platform-feature-defines.txt` custom command** preprocesses
   `wtf/Platform.h` and does not inherit `add_compile_options`, so it could not
   find `<Availability.h>`. It already had a hook for exactly this
   (`WEBKIT_GENERATED_STUBS_INCLUDE_DIR`); the Tiger include and framework roots
   go in beside it, from `WEBKIT_TIGER_PLATFORM_ARGS`.

### Two structural fixes in the overlay, both high-leverage

**`NSPoint`, `NSSize` and `NSRect` are now the CoreGraphics types.** The 10.4 SDK
declares them as their own structs; Apple unified them in 10.5 behind
`NS_BUILD_32_LIKE_64`. WebCore assumes the unified world and does
`typedef CGPoint NSPoint` itself in six headers, which against this SDK is
"typedef redefinition with different types" — in `FloatPoint.h`, `FloatRect.h`,
`FloatSize.h`, `IntPoint.h`, `IntRect.h`, `IntSize.h` and `DoublePoint.h`.

This is safe rather than merely convenient. On i386 `struct _NSPoint` is
`{float x; float y;}` and `struct CGPoint` is `{float x; float y;}`, byte for
byte, so every AppKit entry point taking an `NSRect` by value receives exactly
the same four floats. The only observable difference is the Objective-C type
encoding, `{CGPoint=ff}` rather than `{_NSPoint=ff}`, which matters only to code
that compares encoding strings at runtime.

**`CFBase.h` gained the `CF_*` macros.** `CF_ENUM` and `CF_OPTIONS` are the
load-bearing pair: without them `typedef CF_ENUM(CFIndex, Name) { ... }` parses
as a function definition declared typedef, which is how PAL's SPI headers fail.
The rest — bridging, ownership, nullability, `CF_SWIFT_NAME` — are annotations.
Same shape, and same reasoning, as the `NS_*` macros in `FoundationCompat.h`.

### Smaller WebCore and PAL fixes

- `wtf/cocoa/SoftLinking.h` expands to `dispatch_once` but never included
  `<dispatch/dispatch.h>`; on a modern SDK something else in the include graph
  always had. Arguably an upstream latent bug.
- `WebCorePrefix.h`'s `PLATFORM(MAC)` block is a precompiled-header warm-up
  list, not a dependency list. `AudioSession.h` is 10.7, the IOKit HID family
  10.5, simd 10.11. Only what Tiger has is kept.
- `IOKitSPIMac.h`, the CG window-capture soft links, `CGDataProviderDirectAccessRangesCallbacks`,
  `CGDisplayMode`, `CGEventCopyIOHIDEvent` — all gated.
- The three `CoreGraphicsSPI.h` enum blocks cgcompat listed, gated. Their values
  were checked against `<TigerCompat/CGCompat.h>`.
- `-Wno-deprecated-anon-enum-enum-conversion` is global: Tiger spells the
  CGBitmapInfo constants as separate anonymous enums and WebCore combines them,
  which C++20 deprecated.

### CommonCrypto on Tiger — the answer

Measured against `sdk/MacOSX10.4u.sdk/usr/lib/libSystem.dylib` rather than the
export list, because that list is not exhaustive:

| Family | On Tiger? |
|---|---|
| CommonDigest — `CC_MD2`, `CC_MD4`, `CC_MD5`, `CC_SHA1`, `CC_SHA256`, `CC_SHA384`, `CC_SHA512` | **yes**, and the SDK ships `CommonCrypto/CommonDigest.h`. Note: no `CC_SHA224` |
| CommonCryptor — `CCCrypt`, `CCCryptorCreate` | no |
| CommonHMAC — `CCHmac` | no |
| `CCKeyDerivationPBKDF`, `CCRandomGenerateBytes` | no |

So WebCrypto's symmetric algorithms cannot be built on CommonCrypto. The PAL
crypto algorithm files are excluded; `CryptoDigestCommonCrypto.cpp` stays,
because digests are exactly what Tiger has. LibreSSL is already in the sysroot
and is the eventual answer for the rest.

### PAL compiles and archives

`libPAL.a` links. Getting there took, beyond the exclusions above:

| Problem | Fix |
|---|---|
| `wtf/cocoa/SoftLinking.h` expands to `dispatch_once` but never included `<dispatch/dispatch.h>` | added it; on a modern SDK something else in the include graph always had |
| `WebCorePrefix.h`'s `PLATFORM(MAC)` block warms the precompiled header with `AudioSession.h` (10.7), the IOKit HID family (10.5) and simd (10.11) | a `PLATFORM(TIGER)` branch keeping only what Tiger has |
| The three `CoreGraphicsSPI.h` enum blocks cgcompat listed | gated; their values were checked against `<TigerCompat/CGCompat.h>` |
| `CGDataProviderDirectAccessRangesCallbacks` (10.5), `CGDisplayMode` (10.6), `CGEventCopyIOHIDEvent`, the CGWindowList capture soft link, `CG_LOCAL` | gated, and `HAVE_CG_CONTEXT_SET_OWNER_IDENTITY` and `HAVE_LOCKDOWN_MODE_PDF_ADDITIONS` turned off |
| `wtf/spi/cocoa/SecuritySPI.h` redeclares `SecTrustRef` as `struct __SecTrust *`; Tiger's `<Security/SecTrust.h>` already has it as `OpaqueSecTrustRef` | include Tiger's header instead of redeclaring |
| `TransformationMatrix.h` includes `<simd/simd.h>` (10.11) under bare `PLATFORM(COCOA)` | gated, along with the three `simd_float*` conversions, which only the CA and WebXR paths use |
| `GainMap.h` includes `<ImageIO/CGImageMetadata.h>` (10.8) under bare `PLATFORM(COCOA)` | forward-declare `CGImageMetadataRef`; it is only held as a `RetainPtr`, which needs no definition |
| `NSNotificationName` (10.10) | added to the overlay's `NSObjCRuntime.h`, with the five sibling `NSString` aliases from the same release |

One nesting mistake worth recording, because it is easy to repeat: adding
`#if !PLATFORM(TIGER)` immediately inside an existing `#if HAVE(...)` block and
closing it before the block's own `#endif` silently steals that `#endif`. The
error surfaces hundreds of lines later as "unterminated conditional directive".
Turning the `HAVE_` off is the better move anyway.

### The WebCore compile passes

Five passes. The failure count is translation units, not errors.

| Pass | Failing units | What cleared |
|---|---|---|
| 1 | 97 of 557 | — |
| 2 | 60 | `CCCryptorStatus`/`CCStatus`, `HAVE_TASK_IDENTITY_TOKEN`, the media exclusions |
| 3 | 60 | `CCAlgorithm`/`CCOperation`, the `nw_*` types, `CFN_EXPORT` |
| 4 | 51 | the exclusions applied to `WebCore_SOURCES` as well as the `.txt` lists |
| 5 | in flight | `CF_FORMAT_FUNCTION`, the NSURLSession SPI blocks |

Two of these were worth more than they looked.

**The exclusions were only half-applied.** `WebCore_UNIFIED_SOURCE_EXCLUDES`
filters the `Sources*.txt` lists, but `WebCore_SOURCES` is a separate list that
`PlatformCocoa.cmake` and `CMakeLists.txt` append to directly and nothing
filters. The Core Animation and media files were still being compiled from
there. Both now come from one pattern list, in the TIGER block of
`WebCore/PlatformCocoa.cmake`.

**`CF_FORMAT_FUNCTION` presented as two unrelated problems.** WebCore declares
`formatLocalizedString` with it. An undefined macro there reads as "expected
function body after function declarator" in `LocalizedStrings.h`, and then
every caller fails separately with "no member named formatLocalizedString" in
`CodecUtilities.cpp`. One missing macro, two symptoms, neither pointing at the
cause. That is the third time a missing `CF_*` macro has done this, so the whole
set WebCore names is in the overlay's `CFBase.h` now rather than just the one.

**A pattern that keeps recurring: narrowing an include removes types as well as
functions.** Gating `<CommonCrypto/CommonCrypto.h>` down to `CommonDigest.h`
took `CCCryptorStatus`, `CCStatus`, `CCAlgorithm` and `CCOperation` with it, and
those are named in declarations throughout the SPI header even where nothing
calls them. Same with `Network/Network.h` and the `nw_*` types. The fix in both
cases is to declare the types and let the functions stay absent, so the failure
lands at link time where it belongs rather than at parse time in every file that
includes the header.

**And one to watch for when adding a gate:** putting `#if !PLATFORM(TIGER)`
immediately inside an existing `#if HAVE(...)` and closing it before that
block's own `#endif` silently steals the `#endif`. It surfaces hundreds of lines
later as "unterminated conditional directive". Turning the `HAVE_` off is
usually the better move anyway.

### Where the compile stands

PAL compiles apart from one file, `system/mac/PopupMenu.mm`, which is the
`<select>` popup and has to come back. It needs five AppKit shims that nscompat
is adding: `NSControlSizeMini` (the 10.10 rename of `NSMiniControlSize`),
`NSUserInterfaceLayoutDirection` and its two values (10.6),
`-[NSMenu userInterfaceLayoutDirection]` (10.11) and
`-[NSWindow convertRectToScreen:]` (10.7). It is excluded with a comment naming
all five and saying to delete the exclusion once they land.

WebCore proper has not been reached yet — each round so far has been consumed by
PAL and by the shared headers underneath it. That is the expected shape: the
plan puts M2 at 15-20 days and says the unified-source batching means one bad
file blocks twenty.

### Waiting on other tracks

| Item | Owner | Blocks |
|---|---|---|
| `objc_storeWeak` / `objc_loadWeak` declarations | objcrt | the JSC Objective-C API |
| `NSMapTable` C functions taking the class | nscompat | the JSC Objective-C API |
| `NSNotificationName` | nscompat | `PAL/spi/mac/NSWindowSPI.h` |
| The five PopupMenu shims | nscompat | `PAL/system/mac/PopupMenu.mm` |

## Deferred items, recorded so they are not rediscovered

Two from the WebKitLegacy plan, neither reachable yet but both easy to lose.

**The curl CA bundle path has to be set at runtime.** curl was built with
`--with-ca-bundle` pointing at `toolchain/sysroot-i386/usr/etc/ssl/cacert.pem`,
a path on *this* machine that does not exist on the Tiger box, so every TLS
verification would fail at runtime with a certificate error that looks like a
protocol problem. Ship `cacert.pem` in the app's Resources and call
`CurlSSLHandle::setCACertPath`, defaulting to the bundle's
`Resources/cacert.pem` and overridable by an environment variable
(`WEBKIT_CURL_CA_BUNDLE`) so the `jsc` and test tools can point at a copy.
`platform/network/playstation/CurlSSLHandlePlayStation.cpp` is the model, about
thirty lines.

**`WebDelegateImplementationCaching.mm` must use `objc_msgSend_fpret` on i386.**
It casts `objc_msgSend` to a float-returning function pointer. On x86-64 that is
harmless because floats come back in xmm0 either way; on i386 a float return
comes off the x87 stack and the wrong entry point yields silent garbage rather
than a crash. This is the same class of failure as the blend modes and
`CGContextClipToMask`: correct-looking code, no diagnostic, wrong values.

**And one still open from this round.** `GraphicsContextCG::clipToImageBuffer`
passes an RGBA image to `CGContextClipToMask`, and Tiger's ClipToMask clips
everything away for any mask that is not a DeviceGray non-alpha image — measured,
`spike/clipmasktest.c`. It needs a grayscale conversion at that call site; the
FIXME already there is pointing at exactly this. The symptom is blank regions
where CSS masking, clip paths or a masked canvas composite should be, with no
crash and no error, so it is worth checking first if that appears.

## Fifth WebCore compile pass — final (round 3 close-out)

`ninja -C build/tiger-wc WebCore -k 100000` reached **478/480, 49 failing
translation units, 240 errors**. Progression across passes:

| pass | failing TUs |
|---|---|
| 1 | 97 |
| 2 | 60 |
| 3 | 60 |
| 4 | 51 |
| 5 | 49 |

Fixed since pass 4: the NSURLSession class extensions in `CFNetworkSPI.h`
(three `#if defined(__OBJC__)` gates), `CF_FORMAT_FUNCTION` in the overlay's
`CFBase.h` (`LocalizedStrings.h` + `CodecUtilities.cpp`), and the exclusion
lists now applied to `WebCore_SOURCES` as well as the unified-source excludes.

Per team-lead's direction change (64-bit content process), this pass is **not
pushed to link**. Residual clusters, in the order they should be attacked on
whichever 64-bit rendering branch wins:

1. **Frameworks that simply do not exist in the 10.4 SDK** (~60 errors, 12 TUs)
   — Vision (`BarcodeDetectorImplementation.mm`, 19), Accelerate vImage
   (`PixelBufferConversion.cpp`, 17), CoreMedia/VideoToolbox (`TrackInfo.cpp`,
   `CMUtilities.h`, `WebCoreDecompressionSession.h`, `MediaSampleConverter.cpp`,
   `VP9UtilitiesCocoa.mm`, `VideoToolboxSoftLink.h`), MediaAccessibility,
   WebGPU (`WebGPUPtr.h`). All are more source exclusions, no shim work.

2. **CFNetwork SPI + NSHTTPCookie vintage** (41 errors) —
   `CFHTTPCookieStorageAcceptPolicy*`, `NSHTTPCookieStringPolicy`,
   `NSHTTPCookieAcceptPolicy` used as a nullable type. Moot once networking is
   curl; exclude `CookieCocoa.mm` and gate the cookie half of `CFNetworkSPI.h`.

3. **Modern CoreGraphics/ImageIO private API** (~30 errors) —
   `NativeImageCG.cpp` wants `CGImageBlockRef`, `CGImageBlockSetRef`,
   `CGImageProviderCallbacksVersion1/2`, CVPixelBuffer lock flags;
   `CGWindowUtilities`, `ShareableBitmapCG`, `ImageUtilitiesCG`, `UTIRegistry`,
   `ImageIOSPI.h`, `GraphicsChecksMac.cpp`. cgcompat territory.

4. **AppKit 10.10+ API** (~35 errors) — `NSVisualEffectView`
   (`WebCoreFullScreenPlaceholderView.mm`, 14), `NSScrollView` content insets
   (`ScrollViewMac.mm`, 10), `NSAppearance` (`NSAppearanceSPI.h`,
   `LocalDefaultSystemAppearance.mm`), `ValidationBubbleMac.mm`,
   `AppKitControlSystemImage.mm`, `CursorMac`, `IconMac`, `ColorMac`,
   `NSSharingServicePickerSPI.h`. Mostly excludable; the rest is nscompat.

5. **Security framework vintage** (12 errors) — `CertificateInfoCFNet.cpp`
   wants `SecTrustCopyCertificateChain`, `SecCertificateCopyValues`,
   `kSecOIDX509V1ValidityNotBefore/After`. Tiger has the CSSM spellings
   (`CSSMOID_X509V1ValidityNotBefore`), so a real adapter is possible.

6. **Fallout from my own gating** (~15 errors) — `ScrollAnimatorMac.mm` (9)
   lost `PlatformWheelEventPhase::Began/Ended/Cancelled/MayBegin` and
   `PlatformWheelEvent::isGestureStart/isEndOfMomentumScroll` when the wheel
   phase HAVE was turned off. Also `GraphicsLayer.cpp`, `HIDElement.h`,
   `objc_utility.mm`, `TypeCastsCF.h`. These are mine to re-balance.

7. **Genuine toolchain/runtime items** (12 errors):
   - `JSValue.mm` needs `method_copyReturnType`, `method_copyArgumentType`,
     `method_getReturnType` — ObjC2 introspection Tiger's runtime lacks.
     **Routed to objcrt.** Blocks the JSC ObjC API in ObjC++ TUs.
   - `JSString.h:1168` `stringImpl[0]` is ambiguous **because of the fragile
     runtime**, not 32-bit: `NSString` is a complete struct under the fragile
     ABI, so the built-in `operator[](NSString*, int)` becomes a candidate
     against `StringImpl::operator[](unsigned)` via
     `StringImpl::operator NSString*()`. One-character fix (`[0U]`), and it
     will *not* reproduce on a non-fragile 64-bit branch.
   - `CornerShapeUtilities.cpp`: "thread-local storage is not supported for the
     current target" — the known `-target i386-apple-macosx10.4` limitation.

8. **Crypto** (5 errors) — `SerializedCryptoKeyWrapCocoa.mm`,
   `CryptoUtilitiesCocoa.h`, `CryptoAlgorithmAESCBCCocoa.cpp`,
   `PushCryptoCocoa.cpp`. LibreSSL replacements exist; not attempted.

Stopping here per instruction. No WebKitLegacy work, no curl `ResourceHandle`
restoration, no link attempt.

### Addendum: ctcompat's AAT case constants verified

ctcompat landed the four case-feature constants (their 4a109c6) in the
overlay's `CoreText.framework/Headers/SFNTLayoutTypes.h`. Rebuilding
`UnifiedSource-platform-25.cpp` confirms the six errors are gone. Their note
is worth keeping: this is a build fix only, not a rendering change — all six
WebCore uses funnel into `CTFontCopyGlyphCoverageForFeature`, which is a NULL
stub, so `supportsSmallCaps` answers no and WebCore synthesises scaled
capitals. Of eight Tiger system faces only Hoefler Text and Didot declare a
case feature at all, and both use the legacy type 3.

The rebuild surfaced the next layer in the same unit, and two of my own gates
turned out to be stale:

- `wtf/cf/TypeCastsCF.h` was skipping both `<CoreText/CTFontDescriptor.h>` and
  `WTF_DECLARE_CF_TYPE_TRAIT(CTFontDescriptor)` on the grounds that Tiger's
  CoreText is header-less. It is not any more, and Tiger does export
  `CTFontDescriptorGetTypeID`. Un-gated (WebKit 1982436d).
- `kCFNumberCGFloatType` is 10.5+. Added to the overlay's `CFBase.h` as a macro
  expanding to `kCFNumberFloat32Type`: CGFloat is float on i386, and Tiger's
  CFNumber would not recognise the 10.5 enumerator anyway. A macro rather than
  an enumerator so it does not depend on CFNumber.h parse order (d439f59).

That unit is now down to four errors, all ctcompat's and all routed:
`kThirdWidthTextSelector`, `kQuarterWidthTextSelector`, and
`kCTFontBaselineAdjustAttribute` twice.

Note for the next full pass: `TypeCastsCF.h` is a widely included WTF header,
so touching it costs a large rebuild.

## Round 4, first tree change: per-process CMake split

WebKit b1fc713d. Two new files plus edits to `OptionsCocoa.cmake`.

`Source/cmake/OptionsTigerProcesses.cmake` introduces `TIGER_PROCESS`, one of
UI, RENDER, WEB or NETWORK, and validates it against the toolchain: asking for a
64-bit process with the i386 toolchain now fails at configure rather than after
four hundred compile errors.

The structure is deliberate. **Every feature flag is set in one shared list,
identically for all four configurations.** The serializer generator copies the
conditions from the `.serialization.in` and `.messages.in` files straight into
the generated C++, and 219 distinct `ENABLE_`/`USE_` names appear in them, so two
processes that disagree about any one of them compile different serializer sets
out of the same source and diverge on the wire. The per-process sections set only
the JIT switches and the target selection, none of which appear in a condition.

Three findings from doing it:

1. **Video cannot be a per-process choice.** `ENABLE_VIDEO`, `ENABLE_MEDIA_SOURCE`
   and their relatives all appear in serialization conditions. Turning video on
   later for the ffmpeg backend means turning it on for all four processes, not
   just the web process. Same for `ENABLE_WEBASSEMBLY`, which is why it stays off
   even in the JIT configuration for now. `ENABLE_GPU_PROCESS` is the most-used
   condition of all and flips on for everyone at once when the render process
   lands.
2. **55 of the names are not CMake options at all**, they are compile-time macros
   in the `PlatformEnable*.h` headers. They cannot be set from the build system,
   so making them agree is the port header's job, not CMake's. The configure
   prints the list. Setting one would have been a hard error, so the fragment
   skips and reports rather than keeping a hand-pruned second copy of the list.
3. **`USE_CG` and `USE_CORE_TEXT` are among them.** The two flags that most need
   to diverge between the render process and the web process are not CMake
   variables under the Cocoa port. That is one more reason the non-Cocoa
   `OptionsTiger64.cmake` is required rather than optional.

`Source/cmake/TigerCheckIPC.cmake` records every IPC-relevant flag and its final
value into `tiger-ipc-features.txt` at configure time, deriving the names from the
tree with one grep rather than keeping a list in sync by hand. It adds a
`tiger-check-ipc` target that compares this tree against `TIGER_IPC_REFERENCE`,
first at the flag level, which names the cause, then by hashing the generated IPC
sources, which catches the symptom. It treats `USE_CG`, `USE_CORE_TEXT` and the
three types the render-process survey identified as known-divergent and reports
them instead of failing.

Verified both ways. All four configurations agree with each other. A throwaway
tree configured with `-DENABLE_VIDEO=ON` is correctly rejected with
"UNEXPECTED: ENABLE_VIDEO: A has OFF, B has ON".

`ENABLE_WEBKIT` stays off, behind a new `TIGER_WEBKIT2` switch. Turning it on
under `PORT=Cocoa` fails immediately in `PlatformCocoa.cmake`, which needs Swift,
and that build is exactly the one the survey ruled out: it compiles the 115
CoreIPC serializers that make a Cocoa process disagree with a non-Cocoa one.
WebKitLegacy is now off in all four configurations; the WK1 work is retired.

### Configure results

| Process | Toolchain | Arch | Result |
|---|---|---|---|
| UI | tiger.cmake | i386 | configures |
| RENDER | tiger.cmake | i386 | configures |
| WEB | tiger64.cmake | x86_64 | configures |
| NETWORK | tiger64.cmake | x86_64 | configures |

All four build WTF, JSC and WebCore for their architecture and feature set. No
builds were run. The x86_64 pair configures under `PORT=Cocoa` but will not
compile, since Tiger has no 64-bit Foundation; they need `OptionsTiger64.cmake`,
which is the next piece.

Reconciled with jsc64's `toolchain/tiger64.cmake` rather than writing a second
one. Theirs defines `WTF_PLATFORM_TIGER64` and deliberately not
`WTF_PLATFORM_TIGER`, keeps the dispatch polyfill off the path, and points at
`sysroot-x86_64`. Left untouched.

### Incident: I deleted build/

A loop over colon-separated specs used `set -- $cfg`, which does not word-split
in zsh, so the positional parameters were empty and `rm -rf build/$D` became
`rm -rf build/`. It removed the `tiger-wc` WebCore tree from the fifth compile
pass and the `llvm-host` LLVM build tree.

What survived: `toolchain/llvm-tiger`, the installed patched clang, is intact and
reports the right version and target. Everything lost is a build tree, so the
cost is rebuild time, not information; `logs/wc-build.log` still has the fifth
pass and its error list.

The lesson for this repo: never interpolate a shell variable into an `rm -rf`
path without first checking it is non-empty. The corrected loop validates all
three fields and skips the iteration if any is empty.

## Round 4 update: three configurations, and the in-process rendering seam

The render process is gone, per the architecture decision. `TIGER_PROCESS` is now
UI, WEB or NETWORK. The UI configuration absorbs what RENDER was: CoreGraphics and
CoreText WebCore graphics, the replay side, the CoreAnimation host and the Aqua
control drawing, alongside the WebKit2 UI process.

Video and media source are now **on in all three configurations**. They appear in
serialization conditions, so the ffmpeg backend is not a web-process-only choice;
the decision belongs to the whole port and is recorded in the shared list rather
than a per-process section. The neutral graphics encoding switch now defaults on.

All three configure. The check target says the three trees agree.

| Process | Toolchain | Arch | Result |
|---|---|---|---|
| UI | tiger.cmake | i386 | configures |
| WEB | tiger64.cmake | x86_64 | configures |
| NETWORK | tiger64.cmake | x86_64 | configures |

### The in-process rendering seam

Team-lead asked what running the GPU-process-side remoting inside the UI process
requires. I traced it. The answer is better than expected on structure and worse
than expected on two specific risks.

**The seam is seven lines.** `UIProcess/WebProcessPool.cpp:581-587`,
`createGPUProcessConnection`. Today it calls `ensureGPUProcess()` and forwards a
message; replacing that with a direct local `GPUConnectionToWebProcess::create(...)`
call, keeping the handle, is the whole redirect, plus about twenty lines of map to
hold the references. **The web process needs no changes at all.** It already mints
the connection pair itself and hands over a real handle; it never learns which
process the other end lives in.

The ownership chain is `GPUProcess` owning a per-web-process
`GPUConnectionToWebProcess` owning a map of `RemoteRenderingBackend`. What the
rendering backend actually needs from its connection object is five accessors: the
shared resource cache, the shared preferences, the web process identifier, the
terminate hook, and, only under Cocoa with video on, the video frame heap. It
reaches the `GPUProcess` singleton exactly once in the whole 2D path, at
`RemoteRenderingBackend.cpp:226`, for the snapshot API, and that line carries an
upstream FIXME saying the pattern is wrong anyway.

Nothing in the transport is process-bound. `StreamServerConnection::tryCreate` is a
plain object built from a handle with no singleton and no XPC assumption, and the
work queue is a per-backend WTF thread with its own semaphore loop, not a dispatch
queue and not a global. Process-global graphics state is also clear: the capability
flags are set only by the web process, never by the GPU or UI process, so merging
those two changes nothing.

**Three obstacles, and the second is the one that worries me.**

1. *The build graph, not the code.* `GPUConnectionToWebProcess` is 1,901 lines with
   188 conditional blocks, and its constructor unconditionally builds four media
   proxies and calls three codec-availability probes. All of it sits behind flags a
   Tiger port turns off, so this is grinding rather than hard, but 188 blocks is 188
   chances for one not to hold.
2. *CoreGraphics on a non-main thread.* Every rendering member is annotated as
   guarded by the work queue, so replay runs on a graphics thread. In a separate GPU
   process that thread owns the address space. In our UI process it would coexist
   with AppKit drawing on the main thread, and Tiger's CoreGraphics font and colour
   space caches were never audited for that. This is the likely source of
   intermittent crashes. The mitigation is a variant work queue that drains on the
   main run loop, roughly sixty lines, trading latency for safety.
3. *The trust model inverts.* Every message check and release assertion in the 2D
   path currently kills a sacrificial process. In the UI process each one becomes a
   browser crash, and an image-buffer overflow becomes a UI-process memory-safety
   bug. For this port that is an acceptable trade, but it should be written down as
   a trade rather than discovered later. It is written down here.

**A smaller first move exists.** Tiger cannot share a window, but it shares memory
fine. Keeping the GPU process as a separate i386 executable that paints into a
shareable bitmap and sends the handle to the UI process, which blits it in the
view, changes one destination for an existing message instead of relocating six
thousand lines and inverting the threading and trust model. It costs one extra copy
per frame. The allocator for exactly this already exists. Since the seam above is
narrow and will still be there later, deferring it burns no bridge. My
recommendation is to measure the copy before paying for the merge.

### Authoritative shared-flag list

`logs/tiger-ipc-shared-flags.txt` is the generated record from the UI
configuration: every name that appears in a serialization condition, with its final
value. It is produced by the configure itself, so it cannot drift from the tree.
Fifty-five of those names are compile-time macros rather than CMake options,
including the CoreGraphics and CoreText ones, and those have to be made to agree in
the port's platform header instead.

## PORT=Tiger, the port headers, and the wire-flag set

WebKit 9fa48ff6. Also `tools/rm-build-tree.sh` (ae0608f), adopted from ld64fix
unchanged; its self-test passes all nine checks and it is now the only sanctioned
way to remove anything under `build/`.

The x86_64 pair configures under the new port. That was the goal.

| Process | Port | Toolchain | Arch | Result |
|---|---|---|---|---|
| WEB | Tiger | tiger64.cmake | x86_64 | configures |
| NETWORK | Tiger | tiger64.cmake | x86_64 | configures |
| UI | Cocoa | tiger.cmake | i386 | configures, not yet migrated |

In the web tree, the flags that were the whole point now read:
CoreGraphics off, CoreText off, CoreFoundation off, AppKit off, cairo on, curl on,
unix domain sockets on, the coordinated-layer switch on. Under the Cocoa port
those first four were compile-time macros and could not be set at all.

### What it took

Six things the Cocoa port had been providing implicitly. Each was a
target-not-found error rather than anything conceptual: the ICU imported targets,
the rest of the static dependency set as imported targets pointing into the cross
sysroot rather than at the host, the colour-management and WOFF2 libraries turned
off because we have not cross-built them, the inspector front end off because the
directory is not in this sparse checkout, WebKitLegacy off, and WebKit2 off behind
the existing switch because turning it on reaches a Cocoa macro for an XPC service
layout that 10.4 has no launchd for.

One ordering bug worth recording because the check found it rather than review:
the flag record has to be written at the very end of the options file, after the
graphics settings. Written straight after the option block it reports every one of
them as unset, which looks like agreement and is not.

### The wire-flag set

`wtf/PlatformTigerWire.h`. wcplan's measurement is that 293 distinct conditionals
appear across the generator inputs and 69 inputs carry at least one flag the two
sides genuinely disagree about, with PLATFORM(COCOA) alone appearing 166 times.

The distinction the header draws: a condition in a generator input asks whether a
field is on the wire, while the same macro in a source file asks whether this
process has the framework. Upstream never had to separate those, because it never
had two processes of different platform character on one connection. The
TIGER_WIRE_* flags are 1 on both sides and used only in generator inputs, so the
64-bit side encodes and decodes Cocoa-shaped messages without having Cocoa, which
is what a process talking to a Cocoa process must do.

Also confirmed wcplan's cheap win: unix domain sockets appears 8 times and is the
one disagreeable-looking flag that is trivially agreeable. Forced on in both.

### Correction to my own earlier proposal

I proposed hashing the generated IPC sources. wcplan is right that it cannot work:
the generator emits the conditions verbatim, so both sides produce a byte-identical
file and diverge only when each compiler evaluates them. The hash stays as a cheap
second signal, since it does catch a stale generated file or a patch applied to one
tree only, but the primary guard has to be the preprocessor probe. That is next.

### Remaining

1. Migrate the UI process to PORT=Tiger. It still needs the Objective-C and ARC
   setup, the SDK overlay search paths and the platform arguments that live in the
   Cocoa options file. Until it moves, comparing the two trees reports two
   port-specific options as divergent, which is the check working correctly rather
   than a real problem.
2. The preprocessor probe: extract every condition from the shared generator
   inputs, build one probe translation unit, preprocess it per side with each
   side's real flags, diff. Preprocessing only, never execution, because no i386
   binary runs on this host.
3. The mechanical rewrite of the affected conditions in the generator inputs, as a
   patch under `toolchain/patches/` so it survives rebasing.
