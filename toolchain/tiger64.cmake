# TIGER64: CMake toolchain file for cross-building to Intel Mac OS X 10.4 (x86_64).
#
# Companion to toolchain/tiger.cmake, which targets i386 for the Cocoa port. This one
# exists for the 64-bit JIT spike (logs/jsc64-spike.md): Tiger's x86_64 userland is
# libSystem only -- no CoreFoundation, no Cocoa, no Objective-C runtime -- so the only
# port that can be built for it is JSCOnly, which needs nothing but libc, pthreads,
# libc++ and ICU. Consequently there is no ObjC language setup here and no SDK overlay
# for Foundation/AppKit/CoreGraphics: those frameworks have no x86_64 slice on 10.4.
#
# Use with: cmake -DCMAKE_TOOLCHAIN_FILE=<this> -DPORT=JSCOnly ...

set(WKT "/Users/shg/Developer/WebKitTiger" CACHE PATH "WebKitTiger tree root")

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_VERSION 8.11.0)       # xnu-792 / Mac OS X 10.4.11
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_OSX_SYSROOT "${WKT}/sdk/MacOSX10.4u.sdk" CACHE PATH "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.4" CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)

set(TIGER ON CACHE BOOL "Build for Mac OS X 10.4" FORCE)
set(TIGER64 ON CACHE BOOL "Build for Mac OS X 10.4 (x86_64)" FORCE)

set(CMAKE_C_COMPILER   "${WKT}/toolchain/bin/tiger-clang64")
set(CMAKE_CXX_COMPILER "${WKT}/toolchain/bin/tiger-clang64++")
set(CMAKE_ASM_COMPILER "${WKT}/toolchain/bin/tiger-clang64")

set(CMAKE_AR      "${WKT}/toolchain/bin/tiger-ar"      CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB  "${WKT}/toolchain/bin/tiger-ranlib"  CACHE FILEPATH "" FORCE)
set(CMAKE_NM      "${WKT}/toolchain/bin/tiger-nm"      CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP   "${WKT}/toolchain/bin/tiger-strip"   CACHE FILEPATH "" FORCE)
set(CMAKE_LIBTOOL "${WKT}/toolchain/bin/tiger-libtool" CACHE FILEPATH "" FORCE)
set(CMAKE_INSTALL_NAME_TOOL "${WKT}/toolchain/bin/tiger-install_name_tool" CACHE FILEPATH "" FORCE)
# cctools-port was configured with an i386 target triple, so every tool carries the
# i386- prefix, but ld64 itself is arch-agnostic and writes x86_64 Mach-O fine.
set(CMAKE_LINKER  "${WKT}/toolchain/cctools/bin/i386-apple-darwin8-ld" CACHE FILEPATH "" FORCE)

set(CMAKE_C_COMPILER_ID      Clang)
set(CMAKE_CXX_COMPILER_ID    Clang)
set(CMAKE_C_COMPILER_FORCED   TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_FIND_ROOT_PATH
    "${WKT}/toolchain/sysroot-x86_64/usr"
    "${WKT}/sdk/MacOSX10.4u.sdk"
)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# -femulated-tls: Tiger has no __thread.
# -fno-stack-protector: no __stack_chk_guard in Tiger's libSystem.
# compat/sdk-overlay/usr/include and compat/include/sdk-fill hold headers the 10.4u SDK
# is missing or that had to be modernised (Availability.h, AvailabilityVersions.h,
# os/availability.h, ...). The dispatch polyfill is i386-only
# (it is built on CFRunLoop, which has no x86_64 slice), so it is NOT on the path
# here; JSCOnly must not reach for dispatch.
set(_tiger64_common_flags
    "-femulated-tls -fno-stack-protector -isystem ${WKT}/compat/sdk-overlay/usr/include -isystem ${WKT}/compat/include/sdk-fill")

# U_DISABLE_RENAMING: our ICU is built with unsuffixed exports (see deps/build-c-deps.sh).
set(_tiger64_cxx_flags
    "-stdlib=libc++ -cxx-isystem ${WKT}/toolchain/sysroot-x86_64/usr/include/c++/v1 -D_LIBCPP_DISABLE_AVAILABILITY -DU_DISABLE_RENAMING=1")

# Only WTF_PLATFORM_TIGER64, never WTF_PLATFORM_TIGER: the i386 port's PLATFORM(TIGER)
# blocks in WTF assume CoreFoundation and Foundation (RetainPtr.h pulls in
# <TigerCompat/CFCompat.h>, for instance), and neither framework has an x86_64 slice on
# 10.4. bmalloc and libpas map TIGER64 onto their own BPLATFORM(TIGER)/PAS_PLATFORM(TIGER)
# since those adaptations are about libSystem gaps, which the two architectures share.
string(APPEND _tiger64_common_flags " -DWTF_PLATFORM_TIGER64=1")

set(CMAKE_C_FLAGS_INIT   "${_tiger64_common_flags} -DU_DISABLE_RENAMING=1")
set(CMAKE_CXX_FLAGS_INIT "${_tiger64_common_flags} ${_tiger64_cxx_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_tiger64_common_flags}")

set(CMAKE_EXE_LINKER_FLAGS_INIT    "-L${WKT}/toolchain/sysroot-x86_64/usr/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-L${WKT}/toolchain/sysroot-x86_64/usr/lib")

# The runtime set every link needs. These go in CMAKE_*_STANDARD_LIBRARIES, not in the
# linker flags, because CMake puts the flags before the object files and a static
# archive only satisfies references that the linker has already seen.
# libtigerdispatch is the 64-bit half of the polyfill (os_log, os_unfair_lock,
# os_signpost); libdispatch itself is i386-only because it is built on CFRunLoop.
# Order within the list matters twice: libc++abi and libunwind after libc++, and
# libtigercompat before the compiler-rt builtins archive (both define ___eprintf, and
# the first one wins).
set(_tiger64_libs "-lc++ -lc++abi -lunwind -ltigercompat -ltigerdispatch ${WKT}/toolchain/sysroot-x86_64/usr/lib/libclang_rt.builtins-x86_64.a")
set(CMAKE_C_STANDARD_LIBRARIES   "${_tiger64_libs}" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "${_tiger64_libs}" CACHE STRING "" FORCE)
unset(_tiger64_libs)

unset(_tiger64_common_flags)
unset(_tiger64_cxx_flags)

# Tiger's dyld predates @rpath.
set(CMAKE_SKIP_RPATH ON CACHE BOOL "" FORCE)
set(CMAKE_MACOSX_RPATH OFF CACHE BOOL "" FORCE)
