# libtigercompat.a: libSystem/libobjc gaps for Mac OS X 10.4 i386. Build: make -C compat
WKT := /Users/shg/Developer/WebKitTiger
CC := $(WKT)/toolchain/bin/tiger-clang
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
CFLAGS := -O2 -g -I BlocksRuntime -I include -I $(WKT)/toolchain/sysroot-i386/usr/include -I include -isystem sdk-overlay/usr/include -isystem include/sdk-fill -isystem include -Fsdk-overlay -F$(APPLICATIONSERVICES_FRAMEWORKS)
SRCS := $(wildcard *.c) $(filter-out archeadercheck.m,$(wildcard *.m)) $(wildcard *.mm) BlocksRuntime/runtime.c
OBJS := $(addprefix obj/,$(addsuffix .o,$(notdir $(SRCS))))
PREFIX := $(WKT)/toolchain/sysroot-i386/usr

all: libtigercompat.a
libtigercompat.a: $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^
obj/%.c.o: %.c | obj install-headers
	$(CC) $(CFLAGS) -c $< -o $@
obj/%.c.o: BlocksRuntime/%.c | obj install-headers
	$(CC) $(CFLAGS) -c $< -o $@
obj/%.m.o: %.m | obj install-headers
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c $< -o $@
obj/%.mm.o: %.mm | obj install-headers
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c $< -o $@
obj:
	mkdir -p obj
# TIGER: the compiler wrappers pass -I$(PREFIX)/include, which comes before this
# directory's own -isystem include. Without refreshing the staged headers first,
# a source file edited here would still be compiled against the previously
# installed copy of its own header.
install-headers:
	mkdir -p $(PREFIX)/include && cp -R include/. $(PREFIX)/include/ && cp BlocksRuntime/Block.h $(PREFIX)/include/
# Headers must parse under ARC (-fobjc-runtime=macosx-fragile-10.7), the way WTF compiles. This runs
# after install on purpose: tiger-clang's own -I of the staged sysroot outranks any -isystem a caller
# passes, so the installed copy is the one every consumer actually gets, and the one worth checking.
check: archeadercheck.m
	@$(CC) -Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7 -fsyntax-only archeadercheck.m \
	  && echo "staged headers are ARC-clean"

install: libtigercompat.a
	mkdir -p $(PREFIX)/lib $(PREFIX)/include && cp libtigercompat.a $(PREFIX)/lib/ && cp -R include/. $(PREFIX)/include/ && cp BlocksRuntime/Block.h $(PREFIX)/include/
	@$(MAKE) --no-print-directory check
clean:
	rm -rf obj libtigercompat.a
.PHONY: all install install-headers clean check
