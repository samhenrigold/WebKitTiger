# 32-bit parent + 64-bit child IPC spike for Tiger.
WKT := /Users/shg/Developer/WebKitTiger
CC32 := $(WKT)/toolchain/bin/tiger-clang
CC64 := $(WKT)/toolchain/bin/tiger-clang64
CFLAGS := -O2 -g -Wall

all: parent32 parent32-nogl child64
parent32: parent32.c glupload.c common.h
	$(CC32) $(CFLAGS) parent32.c glupload.c -framework OpenGL -o $@
child64: child64.c common.h
	$(CC64) $(CFLAGS) child64.c -o $@
parent32-nogl: parent32.c common.h
	$(CC32) $(CFLAGS) -DNO_GL parent32.c -o $@

run: all
	scp -qO parent32 child64 tiger:/tmp/ipc32x64-bin/ 2>/dev/null || (ssh tiger mkdir -p /tmp/ipc32x64-bin && scp -qO parent32 child64 tiger:/tmp/ipc32x64-bin/)
	ssh tiger '/tmp/ipc32x64-bin/parent32 /tmp/ipc32x64-bin/child64'
clean:
	rm -f parent32 parent32-nogl child64
.PHONY: all run clean
