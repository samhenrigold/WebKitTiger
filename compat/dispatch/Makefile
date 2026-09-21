# libtigerdispatch.a: libdispatch + os_log/os_lock polyfill for Mac OS X 10.4.
# i386 only. This archive owns os.c on i386.
#
# THE 64-BIT HOME OF os.c IS compat's ARCHIVE, NOT THIS ONE.
# `make -C compat ARCH=x86_64 install` compiles os.c (with this directory's include
# tree) into the 64-bit libtigercompat.a and stages the os/ and sys/ headers. Link
# -ltigercompat for os_log / os_unfair_lock / os_signpost / qos in a 64-bit binary.
# Do not add an ARCH=x86_64 target back here: two archives carrying os.o is exactly
# what sent people hand-building libtigercompat.a and reinstating a broken
# _dyld_find_unwind_sections.
#
# dispatch.c cannot go 64-bit at all, whoever builds it: Tiger ships no x86_64
# CoreFoundation (libSystem is the only 64-bit library on the box) and the main
# queue needs CFRunLoop. Its CF headers do not even parse at -target x86_64 --
# CarbonCore's MacTypes.h opens with `#pragma options align=mac68k`. So a 64-bit
# process gets the os_* surface and no dispatch queues, which is what the 64-bit
# web process needs (PlayStation-style WTF).
WKT := /Users/shg/Developer/WebKitTiger
ARCH := i386
AR := $(WKT)/toolchain/bin/tiger-ar
PREFIX := $(WKT)/toolchain/sysroot-$(ARCH)/usr
CFLAGS := -O2 -g -std=gnu99 -fblocks -Wall -Wextra -Wno-unused-parameter -I include -I ../BlocksRuntime

CC := $(WKT)/toolchain/bin/tiger-clang
OBJDIR := obj
SRCS := dispatch.c os.c
HDRS := dispatch os sys

LIB := libtigerdispatch.a
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
	@$(MAKE) --no-print-directory check

# The staged headers are what every consumer actually gets, since the compiler
# wrappers pass -I$(PREFIX)/include ahead of any -isystem a caller adds. Check
# the installed copies, not the ones in this directory.
check:
	@echo 'int main(void){os_unfair_lock l=OS_UNFAIR_LOCK_INIT;os_unfair_lock_lock(&l);os_unfair_lock_unlock(&l);os_log(OS_LOG_DEFAULT,"x %d",1);return 0;}' \
	  > $(OBJDIR)/hdrcheck.c
	@printf '#include <os/log.h>\n#include <os/lock.h>\n#include <os/signpost.h>\n#include <sys/qos.h>\n' \
	  | cat - $(OBJDIR)/hdrcheck.c > $(OBJDIR)/hdrcheck2.c && mv $(OBJDIR)/hdrcheck2.c $(OBJDIR)/hdrcheck.c
	@$(CC) -fsyntax-only -Wall -Werror $(OBJDIR)/hdrcheck.c && echo "staged headers compile"

clean:
	rm -rf $(OBJDIR) $(LIB)

-include $(DEPS)
.PHONY: all install clean check
