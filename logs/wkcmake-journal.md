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

## Where it stands

Everything below reproduces from a clean configure and build.

| Target | State |
|---|---|
| `bmalloc` | builds, `libbmalloc.a` |
| `WTF` | builds, all objects, `libWTF.a` |
| `JavaScriptCore` | builds, `libJavaScriptCore.a`, 25 MB |
| `jsc` | links, 51 MB, i386, **runs on Mac OS X 10.4.11** |

### What runs

`/tmp/jsc -e 'print(1+1)'` prints 2, exit 0. A smoke test covering recursion
(`fib(20)`), a 20000-element array of objects, `sort`, `JSON.stringify` and
`JSON.parse`, `RegExp.exec`, string methods, non-ASCII strings and
`encodeURIComponent` (so ICU is working), `Math`, `Date`, `Map`, closures,
`try`/`catch`, ES6 classes, arrow functions and template literals all pass.

A GC stress run — 40 rounds of 3000 short-lived objects with a few survivors,
200 JSON round-trips, and a 20000-entry Map — completes and exits 0.

### Known-imperfect, and left alone

- The Objective-C JavaScriptCore API is still off (`JSC_OBJC_API_ENABLED` 0,
  `SourcesTiger.txt`). Those classes declare instance variables inside
  `@implementation`, which the fragile runtime forbids. `NSMapTable` now exists,
  so restoring it is mechanical: move each class's ivars into its `@interface`.
- `ENABLE_REMOTE_INSPECTOR` is off.
- CoreGraphics is gated out of WTF, which has to be undone for WebCore once
  `<TigerCompat/CGCompat.h>` stops reaching the ApplicationServices umbrella.
- `jsc` prints `_NSAutoreleaseNoPool` warnings at startup. Cosmetic, but those
  objects do leak.
- `aligned_alloc` over-allocates by a full alignment, so every 16 KB MarkedBlock
  costs 32 KB of address space. Fine at this scale; if it ever matters, the fix
  is a VM-backed allocator with its own free list rather than malloc.
