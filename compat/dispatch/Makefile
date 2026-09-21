# libtigerdispatch.a: libdispatch + os_log/os_lock polyfill for Mac OS X 10.4.
# ARCH=i386 (default): the whole thing, dispatch.c + os.c, into obj/ and sysroot-i386.
# ARCH=x86_64:         os.c only, into obj-x86_64/ and sysroot-x86_64. See below.
#
# Why 64-bit is os.c only: Tiger ships no x86_64 CoreFoundation (libSystem is the
# only 64-bit library on the box), and dispatch.c needs CFRunLoop to deliver the
# main queue. Its CF headers do not even parse at -target x86_64 -- CarbonCore's
# MacTypes.h opens with `#pragma options align=mac68k`. os.c has no CF dependency:
# it is pthread, mach and stdio, so os_log, os_unfair_lock, os_signpost and
# sys/qos are all available to a 64-bit process.
#
# The 64-bit build therefore installs the os/ and sys/ headers but NOT dispatch/.
# That is deliberate: a 64-bit TU that includes <dispatch/dispatch.h> should fail
# at the #include with "file not found" rather than compile and then die at link
# time on undefined dispatch_* symbols. compat's 64-bit install used to copy this
# whole include tree, which put dispatch/ back; ld64fix removed that in 2c6d1db.
#
# This is the ONLY producer of 64-bit os_log / os_unfair_lock / os_signpost / qos.
# compat's archive deliberately does not carry os.c (2c6d1db). Keep it that way:
# two archives with os.o is what sent people hand-building libtigercompat.a.
WKT := /Users/shg/Developer/WebKitTiger
ARCH ?= i386
AR := $(WKT)/toolchain/bin/tiger-ar
PREFIX := $(WKT)/toolchain/sysroot-$(ARCH)/usr
CFLAGS := -O2 -g -std=gnu99 -fblocks -Wall -Wextra -Wno-unused-parameter -I include -I ../BlocksRuntime

ifeq ($(ARCH),i386)
CC := $(WKT)/toolchain/bin/tiger-clang
OBJDIR := obj
SRCS := dispatch.c os.c
HDRS := dispatch os sys
else
CC := $(WKT)/toolchain/bin/tiger-clang$(subst x86_,,$(ARCH))
OBJDIR := obj-$(ARCH)
SRCS := os.c
HDRS := os sys
endif

LIB := libtigerdispatch$(if $(filter i386,$(ARCH)),,-$(ARCH)).a
OBJS := $(addprefix $(OBJDIR)/,$(SRCS:.c=.o))
DEPS := $(OBJS:.o=.d)
DEPFLAGS = -MMD -MP -MF $(@:.o=.d)

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^

$(OBJDIR)/%.o: %.c internal.h | $(OBJDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

install: $(LIB)
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	cp $(LIB) $(PREFIX)/lib/libtigerdispatch.a
	for d in $(HDRS); do mkdir -p $(PREFIX)/include/$$d && cp -R include/$$d/. $(PREFIX)/include/$$d/; done
	@$(MAKE) --no-print-directory ARCH=$(ARCH) check
	@$(WKT)/compat/check-ownership.sh $(ARCH)

# The staged headers are what every consumer actually gets, since the compiler
# wrappers pass -I$(PREFIX)/include ahead of any -isystem a caller adds. Check
# the installed copies, not the ones in this directory.
check:
	@echo 'int main(void){os_unfair_lock l=OS_UNFAIR_LOCK_INIT;os_unfair_lock_lock(&l);os_unfair_lock_unlock(&l);os_log(OS_LOG_DEFAULT,"x %d",1);return 0;}' \
	  > $(OBJDIR)/hdrcheck.c
	@printf '#include <os/log.h>\n#include <os/lock.h>\n#include <os/signpost.h>\n#include <sys/qos.h>\n' \
	  | cat - $(OBJDIR)/hdrcheck.c > $(OBJDIR)/hdrcheck2.c && mv $(OBJDIR)/hdrcheck2.c $(OBJDIR)/hdrcheck.c
	@$(CC) -fsyntax-only -Wall -Werror $(OBJDIR)/hdrcheck.c && echo "staged $(ARCH) headers compile"

clean:
	rm -rf $(OBJDIR) $(LIB)

-include $(DEPS)
.PHONY: all install clean check
