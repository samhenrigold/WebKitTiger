# libtigercompat.a: libSystem/libobjc gaps for Mac OS X 10.4. Build: make -C compat
# ARCH=i386 (default) is the full archive: C, ObjC and ObjC++, into obj/ and toolchain/sysroot-i386.
# ARCH=x86_64 builds only the C sources with tiger-clang64, into obj-x86_64/ and toolchain/sysroot-x86_64.
# There is no 64-bit ObjC on this port (Tiger's libobjc is i386-only), so the .m/.mm half is i386-only,
# and so is the ARC header check, which compiles ObjC.
ARCH ?= i386
WKT := /Users/shg/Developer/WebKitTiger
AR := $(WKT)/toolchain/bin/tiger-ar
OBJCFLAGS := -fobjc-runtime=macosx-fragile-10.4 -fblocks
# TIGER: ApplicationServices' sub-frameworks (ImageIO, CoreText, ATS, ...) are only
# found automatically from inside ApplicationServices' own headers. A plain header of
# ours saying <ImageIO/CGImageSource.h> needs the sub-framework directory on -F.
# sdk-overlay comes first, so an overlaid framework still wins.
APPLICATIONSERVICES_FRAMEWORKS := $(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks
# TIGER: include/sdk-fill holds headers the 10.4u SDK is missing, under their real
# names (some use #include_next to extend the SDK's own); include/ puts
# <TigerCompat/*.h> on the path. Both are needed to build cfcompat.c/nscompat.m.
PREFIX := $(WKT)/toolchain/sysroot-$(ARCH)/usr
CFLAGS := -O2 -g -I BlocksRuntime -I include -I $(PREFIX)/include -I include -isystem sdk-overlay/usr/include -isystem include/sdk-fill -isystem include -Fsdk-overlay -F$(APPLICATIONSERVICES_FRAMEWORKS)
# The 64-bit archive is the arch-neutral libc/libSystem gaps only: the CF/CG/CT shims are
# about Tiger's 32-bit frameworks, which no 64-bit process can load.
# dispatch/os.c (os_log / os_unfair_lock / os_signpost / sys/qos) IS here, by agreement with the
# dispatch track: this archive is the only 64-bit home of that object and compat/dispatch has no
# ARCH=x86_64 target. On i386 os.c belongs to libtigerdispatch.a, so the two never both carry os.o.
# Do not reintroduce a 64-bit libtigerdispatch without dropping os.c here in the same commit.
# dispatch.c stays i386-only: its main queue needs CFRunLoop and Tiger has no x86_64 CoreFoundation,
# so dispatch_* does not exist in 64-bit at all.
# cfcompat.c is in the 64-bit list too: its <execinfo.h> (backtrace) and dyld
# (dyld_image_header_containing_address, _dyld_get_image_uuid) shims are plain
# libSystem, and its notify/CoreFoundation halves are #ifndef __LP64__.
C_SRCS_x86_64 := availability.c libcompat.c tlv.c cfcompat.c dispatch/os.c
# os.c wants its own include tree and the flags libtigerdispatch builds it with.
DISPATCH_CFLAGS := -std=gnu99 -fblocks -I dispatch/include
ifeq ($(ARCH),i386)
CC := $(WKT)/toolchain/bin/tiger-clang
OBJDIR := obj
LIB := libtigercompat.a
SRCS := $(wildcard *.c) $(filter-out archeadercheck.m,$(wildcard *.m)) $(wildcard *.mm) BlocksRuntime/runtime.c
else
CC := $(WKT)/toolchain/bin/tiger-clang$(subst x86_,,$(ARCH))
OBJDIR := obj-$(ARCH)
LIB := libtigercompat-$(ARCH).a
SRCS := $(C_SRCS_$(ARCH)) BlocksRuntime/runtime.c
endif
OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(notdir $(SRCS))))
# TIGER: without these, editing a header under include/ leaves every object stale
# and the build silently keeps the old code. That is hard to spot, because the
# link succeeds and only the behaviour is wrong.
DEPS := $(OBJS:.o=.d)
DEPFLAGS = -MMD -MP -MF $(@:.o=.d)

all: $(LIB)
$(LIB): $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^
$(OBJDIR)/%.c.o: %.c | $(OBJDIR) install-headers
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(OBJDIR)/%.c.o: BlocksRuntime/%.c | $(OBJDIR) install-headers
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
$(OBJDIR)/%.c.o: dispatch/%.c | $(OBJDIR) install-headers
	$(CC) $(CFLAGS) $(DISPATCH_CFLAGS) $(DEPFLAGS) -c $< -o $@
$(OBJDIR)/%.m.o: %.m | $(OBJDIR) install-headers
	$(CC) $(CFLAGS) $(OBJCFLAGS) $(DEPFLAGS) -c $< -o $@
$(OBJDIR)/%.mm.o: %.mm | $(OBJDIR) install-headers
	$(CC) $(CFLAGS) $(OBJCFLAGS) $(DEPFLAGS) -c $< -o $@
$(OBJDIR):
	mkdir -p $(OBJDIR)
# TIGER: the compiler wrappers pass -I$(PREFIX)/include, which comes before this
# directory's own -isystem include. Without refreshing the staged headers first,
# a source file edited here would still be compiled against the previously
# installed copy of its own header.
install-headers:
	mkdir -p $(PREFIX)/include && cp -R include/. $(PREFIX)/include/ && cp BlocksRuntime/Block.h $(PREFIX)/include/
# os.c's headers, since compat/dispatch has no 64-bit install to stage them. os/ and sys/ only:
# dispatch/ is deliberately NOT staged, so a 64-bit TU including <dispatch/dispatch.h> fails at
# the include instead of compiling and then dying on undefined dispatch_* at link time.
ifneq ($(ARCH),i386)
	cp -R dispatch/include/os dispatch/include/sys $(PREFIX)/include/
endif
# Headers must parse under ARC (-fobjc-runtime=macosx-fragile-10.7), the way WTF compiles. This runs
# after install on purpose: tiger-clang's own -I of the staged sysroot outranks any -isystem a caller
# passes, so the installed copy is the one every consumer actually gets, and the one worth checking.
check: archeadercheck.m
ifneq ($(ARCH),i386)
	@echo "skipping the ARC header check: ObjC is i386-only on this port"
else
	@$(CC) -Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7 -fsyntax-only archeadercheck.m \
	  && echo "staged headers are ARC-clean"
endif

install: $(LIB)
	mkdir -p $(PREFIX)/lib $(PREFIX)/include && cp $(LIB) $(PREFIX)/lib/libtigercompat.a && cp -R include/. $(PREFIX)/include/ && cp BlocksRuntime/Block.h $(PREFIX)/include/
	@$(MAKE) --no-print-directory ARCH=$(ARCH) check
clean:
	rm -rf $(OBJDIR) $(LIB)
-include $(DEPS)
.PHONY: all install install-headers clean check
