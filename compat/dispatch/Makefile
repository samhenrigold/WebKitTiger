# libtigerdispatch.a: libdispatch + os_log/os_lock polyfill for Mac OS X 10.4 i386.
# Build: make -C compat/dispatch install
WKT := /Users/shg/Developer/WebKitTiger
CC := $(WKT)/toolchain/bin/tiger-clang
AR := $(WKT)/toolchain/bin/tiger-ar
PREFIX := $(WKT)/toolchain/sysroot-i386/usr
CFLAGS := -O2 -g -std=gnu99 -fblocks -Wall -Wextra -Wno-unused-parameter -I include -I ../BlocksRuntime

SRCS := dispatch.c os.c
OBJS := $(addprefix obj/,$(SRCS:.c=.o))

all: libtigerdispatch.a

libtigerdispatch.a: $(OBJS)
	rm -f $@ && $(AR) rcs $@ $^

obj/%.o: %.c internal.h | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj:
	mkdir -p obj

install: libtigerdispatch.a
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	cp libtigerdispatch.a $(PREFIX)/lib/
	cp -R include/. $(PREFIX)/include/

clean:
	rm -rf obj libtigerdispatch.a

.PHONY: all install clean
