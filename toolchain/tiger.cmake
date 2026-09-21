# TIGER: CMake toolchain file for cross-building to Intel Mac OS X 10.4 (i386).
# Use with: cmake -DCMAKE_TOOLCHAIN_FILE=<this> -DTIGER=ON -DPORT=Cocoa ...
#
# Everything the wrapper scripts in toolchain/bin already pass (-target, -isysroot,
# -mmacosx-version-min, --ld-path=<cctools ld64>, -include tigerprelude.h, -I/-L
# sysroot-i386) is repeated here only where CMake needs to know about it.

set(WKT "/Users/shg/Developer/WebKitTiger" CACHE PATH "WebKitTiger tree root")

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_VERSION 8.11.0)       # xnu-792 / Mac OS X 10.4.11
set(CMAKE_SYSTEM_PROCESSOR i386)

set(CMAKE_OSX_SYSROOT "${WKT}/sdk/MacOSX10.4u.sdk" CACHE PATH "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.4" CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "i386" CACHE STRING "" FORCE)

# TIGER: the Tiger SDK has no SDKSettings.json and xcrun knows nothing about it;
# WebKitXcodeSDK.cmake keys off TIGER to skip its xcrun probing entirely.
set(TIGER ON CACHE BOOL "Build for Mac OS X 10.4 (i386)" FORCE)

set(CMAKE_C_COMPILER      "${WKT}/toolchain/bin/tiger-clang")
set(CMAKE_CXX_COMPILER    "${WKT}/toolchain/bin/tiger-clang++")
set(CMAKE_OBJC_COMPILER   "${WKT}/toolchain/bin/tiger-clang")
set(CMAKE_OBJCXX_COMPILER "${WKT}/toolchain/bin/tiger-clang++")
set(CMAKE_ASM_COMPILER    "${WKT}/toolchain/bin/tiger-clang")

set(CMAKE_AR      "${WKT}/toolchain/bin/tiger-ar"      CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB  "${WKT}/toolchain/bin/tiger-ranlib"  CACHE FILEPATH "" FORCE)
set(CMAKE_NM      "${WKT}/toolchain/bin/tiger-nm"      CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP   "${WKT}/toolchain/bin/tiger-strip"   CACHE FILEPATH "" FORCE)
set(CMAKE_LIBTOOL "${WKT}/toolchain/bin/tiger-libtool" CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${WKT}/toolchain/bin/tiger-install_name_tool" CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER  "${WKT}/toolchain/cctools/bin/i386-apple-darwin8-ld" CACHE FILEPATH "" FORCE)

# The wrappers are shell scripts, so CMake cannot infer the vendor/version from
# the binary name; tell it what it is and skip the ABI probe link, which needs
# the full runtime set that we are only now building.
set(CMAKE_C_COMPILER_ID      Clang)
set(CMAKE_CXX_COMPILER_ID    Clang)
set(CMAKE_C_COMPILER_FORCED   TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH
    "${WKT}/toolchain/sysroot-i386/usr"
    "${WKT}/sdk/MacOSX10.4u.sdk"
)
# Programs run on the host; libraries, headers and packages come from the target.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -femulated-tls: Tiger has no __thread.
# -fno-stack-protector: no __stack_chk_guard in Tiger's libSystem.
# libc++ is the static one we built into sysroot-i386.
# compat/include/sdk-fill holds headers the 10.4u SDK is simply missing, under
# their real names (Availability.h, os/availability.h, ...). compat/include puts
# <TigerCompat/*.h> on the path for our own framework-level shims.
#
# compat/dispatch/include is the libdispatch/os_log polyfill's header tree
# (dispatch/, os/, sys/qos.h). It is listed after the sysroot so the installed
# copy wins once it is staged there.
# compat/sdk-overlay is the SDK overlay: copies of 10.4u SDK headers that had to
# be modernised, and headers the SDK never had, laid out the way the SDK lays
# them out (usr/include/..., <Name>.framework/Headers/...). It comes first so
# its copies win. See compat/sdk-overlay/README.md for the file-by-file record.
# __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0: Carbon's AssertMacros.h
# defines lowercase check/verify/require macros that collide with C++ member
# names. The overlay's copy honours this opt-out; see compat/sdk-overlay/README.md.
set(_tiger_common_flags
    "-femulated-tls -fno-stack-protector -D__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0 -isystem ${WKT}/compat/sdk-overlay/usr/include -isystem ${WKT}/compat/include/sdk-fill -isystem ${WKT}/compat/include -isystem ${WKT}/compat/dispatch/include")

# Tiger has no standalone CoreGraphics.framework: CoreGraphics, CoreText, ATS,
# ImageIO and friends are subframeworks of ApplicationServices, which -F does not
# reach into. Put that directory on the framework search path so that
# <CoreGraphics/CGColor.h> and <CoreText/CoreText.h> resolve the way modern code
# spells them.
# The overlay's frameworks come before the SDK's, so an overlaid Foundation or
# CoreGraphics header wins over the 10.4u one. Tiger has no standalone
# CoreGraphics.framework -- CoreGraphics, CoreText, ATS and ImageIO are
# subframeworks of ApplicationServices, which -F does not descend into -- so
# those directories go on the path explicitly.
string(APPEND _tiger_common_flags
    " -F${WKT}/compat/sdk-overlay"
    " -F${WKT}/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks"
    " -F${WKT}/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/Carbon.framework/Frameworks")

# -stdlib=libc++ makes clang look for the C++ headers under CMAKE_OSX_SYSROOT,
# which is the 10.4u SDK; ours are in the cross sysroot we built them into.
# _LIBCPP_DISABLE_AVAILABILITY: libc++'s headers mark parts of the library
# (<filesystem>, to_chars, aligned new, ...) unavailable below macOS 10.15,
# because Apple's shipped libc++ dylib did not have them. Ours is a static libc++
# we built for this target, so it has everything; the annotations would only
# remove working code.
set(_tiger_cxx_flags
    "-stdlib=libc++ -cxx-isystem ${WKT}/toolchain/sysroot-i386/usr/include/c++/v1 -D_LIBCPP_DISABLE_AVAILABILITY")

set(CMAKE_C_FLAGS_INIT      "${_tiger_common_flags}")
set(CMAKE_CXX_FLAGS_INIT    "${_tiger_common_flags} ${_tiger_cxx_flags}")
set(CMAKE_ASM_FLAGS_INIT    "${_tiger_common_flags}")
# TIGER: MRR Objective-C uses the fragile 10.4 runtime. Targets that the Cocoa
# port compiles with ARC append WEBKIT_OBJC_ARC_OPTIONS (set in OptionsCocoa.cmake),
# which overrides this with -fobjc-runtime=macosx-fragile-10.7 plus the cc1 -fobjc-arc.
# <TigerCompat/FoundationCompat.h> is force-included into every Objective-C
# translation unit: it adds the Foundation classes, methods and types that
# arrived after 10.4 and that WebKit assumes are always present. WebKit sources
# therefore need no include of their own for them.
set(_tiger_objc_flags "-fobjc-runtime=macosx-fragile-10.4 -include ${WKT}/compat/include/TigerCompat/FoundationCompat.h")

set(CMAKE_OBJC_FLAGS_INIT   "${_tiger_common_flags} ${_tiger_objc_flags}")
set(CMAKE_OBJCXX_FLAGS_INIT "${_tiger_common_flags} ${_tiger_cxx_flags} ${_tiger_objc_flags}")

# WebKitCompilerFlags.cmake mirrors its curated C/CXX flags into plain
# CMAKE_OBJC_FLAGS / CMAKE_OBJCXX_FLAGS, and it runs before OptionsCocoa.cmake
# calls enable_language(OBJC OBJCXX). The plain variables it creates then shadow
# the cache entries that _INIT populates, so the _INIT flags never reach the
# ObjC command lines. Seed the plain variables here as well; WebKit prepends to
# them rather than replacing them.
set(CMAKE_OBJC_FLAGS   "${_tiger_common_flags} ${_tiger_objc_flags}")
set(CMAKE_OBJCXX_FLAGS "${_tiger_common_flags} ${_tiger_cxx_flags} ${_tiger_objc_flags}")

set(CMAKE_EXE_LINKER_FLAGS_INIT    "-L${WKT}/toolchain/sysroot-i386/usr/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L${WKT}/toolchain/sysroot-i386/usr/lib")

unset(_tiger_common_flags)
unset(_tiger_cxx_flags)
unset(_tiger_objc_flags)

# Tiger's dyld predates @rpath.
set(CMAKE_SKIP_RPATH ON CACHE BOOL "" FORCE)
set(CMAKE_MACOSX_RPATH OFF CACHE BOOL "" FORCE)
